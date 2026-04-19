// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"
#include "bc_core.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_concurrency_context_internal.h"
#include "bc_concurrency_platform_internal.h"

#include <limits.h>
#include <pthread.h>

#include <linux/futex.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#define BC_CONCURRENCY_DEFAULT_WORKER_STACK_SIZE ((size_t)(1 * 1024 * 1024))
#define BC_CONCURRENCY_STAGING_INITIAL_CAPACITY ((size_t)1024)
#define BC_CONCURRENCY_SLOT_INITIAL_CAPACITY ((size_t)4)
#define BC_CONCURRENCY_SPIN_COUNT ((int)1000)

_Thread_local bc_concurrency_worker_t* tls_worker = NULL;

bc_allocators_context_t* bc_concurrency_worker_memory(void)
{
    if (tls_worker == NULL) {
        return NULL;
    }
    return tls_worker->memory;
}

size_t bc_concurrency_worker_index(void)
{
    if (tls_worker == NULL) {
        return SIZE_MAX;
    }
    return tls_worker->index;
}

void* bc_concurrency_worker_slot(size_t slot_index)
{
    if (tls_worker == NULL || slot_index >= tls_worker->context->slot_count) {
        return NULL;
    }
    return tls_worker->slots[slot_index];
}

static void bc_futex_wait(atomic_int* addr, int expected)
{
    (void)syscall(SYS_futex, (int*)addr, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0);
}

static void bc_futex_wake(atomic_int* addr, int count)
{
    (void)syscall(SYS_futex, (int*)addr, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
}

void bc_concurrency_dispatch_to_worker(bc_concurrency_worker_t* worker, bc_concurrency_work_t* work)
{
    worker->work = work;
    atomic_store_explicit(&worker->work_done, 0, memory_order_relaxed);
    atomic_store_explicit(&worker->work_ready, 1, memory_order_seq_cst);
    if (atomic_load_explicit(&worker->sleeping, memory_order_seq_cst)) {
        bc_futex_wake(&worker->work_ready, 1);
    }
}

void bc_concurrency_wait_for_worker(bc_concurrency_worker_t* worker)
{
    for (int s = 0; s < BC_CONCURRENCY_SPIN_COUNT; s++) {
        if (atomic_load_explicit(&worker->work_done, memory_order_acquire)) {
            return;
        }
        bc_concurrency_platform_cpu_relax();
    }
    while (!atomic_load_explicit(&worker->work_done, memory_order_acquire)) {
        bc_futex_wait(&worker->work_done, 0);
    }
}

static void* worker_thread_routine(void* arg)
{
    bc_concurrency_worker_t* worker = arg;

    (void)bc_concurrency_platform_pin_current_thread_to_physical_core(worker->context->core_offset + 1 + worker->index);

    sigset_t all_signals;
    sigfillset(&all_signals);
    pthread_sigmask(SIG_BLOCK, &all_signals, NULL);

    for (;;) {
        for (int s = 0; s < BC_CONCURRENCY_SPIN_COUNT; s++) {
            if (atomic_load_explicit(&worker->work_ready, memory_order_acquire)) {
                goto got_work;
            }
            bc_concurrency_platform_cpu_relax();
        }

        atomic_store_explicit(&worker->sleeping, 1, memory_order_seq_cst);
        if (!atomic_load_explicit(&worker->work_ready, memory_order_acquire)) {
            bc_futex_wait(&worker->work_ready, 0);
        }
        atomic_store_explicit(&worker->sleeping, 0, memory_order_relaxed);
        continue;

    got_work:
        if (worker->should_stop) {
            break;
        }

        atomic_store_explicit(&worker->work_ready, 0, memory_order_relaxed);

        bc_concurrency_work_t* work = worker->work;
        worker->work = NULL;

        tls_worker = worker;
        for (size_t i = 0; i < work->count; i++) {
            work->tasks[i].task_function(work->tasks[i].task_argument);
        }
        tls_worker = NULL;

        atomic_store_explicit(&worker->work_done, 1, memory_order_release);
        bc_futex_wake(&worker->work_done, 1);
    }

    return NULL;
}

static bool spawn_workers(bc_concurrency_context_t* context)
{
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        return false;
    }

    if (pthread_attr_setstacksize(&attr, context->worker_stack_size) != 0) {
        pthread_attr_destroy(&attr);
        return false;
    }

    size_t threads_started = 0;
    for (size_t i = 0; i < context->thread_count; i++) {
        if (pthread_create(&context->threads[i], &attr, worker_thread_routine, &context->workers[i]) != 0) {
            pthread_attr_destroy(&attr);

            for (size_t j = 0; j < threads_started; j++) {
                context->workers[j].should_stop = true;
                atomic_store_explicit(&context->workers[j].work_ready, 1, memory_order_seq_cst);
                bc_futex_wake(&context->workers[j].work_ready, 1);
            }
            for (size_t j = 0; j < threads_started; j++) {
                (void)pthread_join(context->threads[j], NULL);
            }
            return false;
        }
        threads_started = i + 1;
    }

    pthread_attr_destroy(&attr);
    context->workers_spawned = true;
    return true;
}

bool bc_concurrency_ensure_workers_spawned(bc_concurrency_context_t* context)
{
    if (context->workers_spawned) {
        return true;
    }
    return spawn_workers(context);
}

bool bc_concurrency_create(bc_allocators_context_t* memory_context, const bc_concurrency_config_t* config,
                           bc_concurrency_context_t** out_context)
{
    *out_context = NULL;

    size_t core_offset = 0;
    size_t worker_stack_size = 0;
    bool worker_count_explicit = false;
    size_t explicit_worker_count = 0;

    if (config != NULL) {
        core_offset = config->core_offset;
        worker_stack_size = config->worker_stack_size;
        worker_count_explicit = config->worker_count_explicit;
        explicit_worker_count = config->worker_count;
    }

    size_t physical_core_count = bc_concurrency_platform_get_physical_core_count();

    if (core_offset >= physical_core_count) {
        return false;
    }

    size_t thread_count;
    if (worker_count_explicit) {
        thread_count = explicit_worker_count;
    } else {
        thread_count = physical_core_count > 1 ? physical_core_count - 1 : 0;
    }

    if (thread_count > 0 && core_offset + thread_count >= physical_core_count) {
        return false;
    }

    if (worker_stack_size == 0) {
        worker_stack_size = BC_CONCURRENCY_DEFAULT_WORKER_STACK_SIZE;
    }
    if (worker_stack_size < (size_t)PTHREAD_STACK_MIN) {
        worker_stack_size = (size_t)PTHREAD_STACK_MIN;
    }

    (void)bc_concurrency_platform_pin_current_thread_to_physical_core(core_offset);

    bc_concurrency_context_t* context = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_concurrency_context_t), (void**)&context)) {
        return false;
    }

    bc_core_zero(context, sizeof(bc_concurrency_context_t));
    context->memory_context = memory_context;
    context->thread_count = thread_count;
    context->core_offset = core_offset;
    context->worker_stack_size = worker_stack_size;
    context->workers_spawned = false;
    context->slot_count = 0;
    context->slot_capacity = 0;
    context->slot_configs = NULL;

    if (!bc_concurrency_staging_reserve(memory_context, &context->staging, BC_CONCURRENCY_STAGING_INITIAL_CAPACITY)) {
        goto fail_context;
    }

    size_t dispatch_tasks_size = BC_CONCURRENCY_STAGING_INITIAL_CAPACITY * sizeof(bc_concurrency_task_entry_t);
    if (!bc_allocators_pool_allocate(memory_context, dispatch_tasks_size, (void**)&context->dispatch_tasks)) {
        goto fail_staging;
    }
    context->dispatch_capacity = BC_CONCURRENCY_STAGING_INITIAL_CAPACITY;

    size_t dispatch_work_size = (thread_count + 1) * sizeof(bc_concurrency_work_t);
    if (!bc_allocators_pool_allocate(memory_context, dispatch_work_size, (void**)&context->dispatch_work)) {
        goto fail_dispatch_tasks;
    }

    if (thread_count > 0) {
        size_t thread_array_size = thread_count * sizeof(pthread_t);
        if (!bc_allocators_pool_allocate(memory_context, thread_array_size, (void**)&context->threads)) {
            goto fail_threads;
        }
    } else {
        context->threads = NULL;
    }

    size_t workers_size = (thread_count + 1) * sizeof(bc_concurrency_worker_t);
    if (!bc_allocators_pool_allocate(memory_context, workers_size, (void**)&context->workers)) {
        goto fail_threads;
    }
    bc_core_zero(context->workers, workers_size);

    size_t worker_memory_contexts_created = 0;

    bc_allocators_context_config_t worker_memory_config = {
        .max_pool_memory = 0,
        .tracking_enabled = false,
    };

    for (size_t i = 0; i < thread_count; i++) {
        context->workers[i].context = context;
        context->workers[i].index = i;
        context->workers[i].work = NULL;
        context->workers[i].should_stop = false;
        context->workers[i].slots = NULL;

        if (!bc_allocators_context_create(&worker_memory_config, &context->workers[i].memory)) {
            goto fail_workers;
        }
        worker_memory_contexts_created = i + 1;
    }

    context->workers[thread_count].context = context;
    context->workers[thread_count].index = thread_count;
    context->workers[thread_count].work = NULL;
    context->workers[thread_count].should_stop = false;
    context->workers[thread_count].slots = NULL;

    if (!bc_allocators_context_create(&worker_memory_config, &context->workers[thread_count].memory)) {
        goto fail_workers;
    }

    *out_context = context;
    return true;

fail_workers:
    for (size_t j = 0; j < worker_memory_contexts_created; j++) {
        bc_allocators_context_destroy(context->workers[j].memory);
    }
    bc_allocators_pool_free(memory_context, context->workers);
fail_threads:
    if (context->threads != NULL) {
        bc_allocators_pool_free(memory_context, context->threads);
    }
    bc_allocators_pool_free(memory_context, context->dispatch_work);
fail_dispatch_tasks:
    bc_allocators_pool_free(memory_context, context->dispatch_tasks);
fail_staging:
    bc_concurrency_staging_destroy(memory_context, &context->staging);
fail_context:
    bc_allocators_pool_free(memory_context, context);
    return false;
}

void bc_concurrency_destroy(bc_concurrency_context_t* context)
{
    if (context->workers_spawned) {
        for (size_t i = 0; i < context->thread_count; i++) {
            context->workers[i].should_stop = true;
            atomic_store_explicit(&context->workers[i].work_ready, 1, memory_order_seq_cst);
            bc_futex_wake(&context->workers[i].work_ready, 1);
        }
        for (size_t i = 0; i < context->thread_count; i++) {
            (void)pthread_join(context->threads[i], NULL);
        }
    }

    bc_allocators_context_t* memory_context = context->memory_context;

    size_t effective_count = context->thread_count + 1;
    for (size_t i = 0; i < effective_count; i++) {
        bc_concurrency_worker_t* worker = &context->workers[i];

        for (size_t s = context->slot_count; s > 0; s--) {
            size_t slot_idx = s - 1;
            void* slot_ptr = worker->slots[slot_idx];
            if (slot_ptr != NULL && context->slot_configs[slot_idx].destroy != NULL) {
                context->slot_configs[slot_idx].destroy(slot_ptr, i, context->slot_configs[slot_idx].arg);
            }
            if (slot_ptr != NULL) {
                bc_allocators_pool_free(memory_context, slot_ptr);
            }
        }

        if (worker->slots != NULL) {
            bc_allocators_pool_free(memory_context, worker->slots);
        }

        bc_allocators_context_destroy(worker->memory);
    }

    if (context->slot_configs != NULL) {
        bc_allocators_pool_free(memory_context, context->slot_configs);
    }

    bc_concurrency_staging_destroy(memory_context, &context->staging);
    bc_allocators_pool_free(memory_context, context->dispatch_tasks);
    bc_allocators_pool_free(memory_context, context->dispatch_work);
    if (context->threads != NULL) {
        bc_allocators_pool_free(memory_context, context->threads);
    }
    bc_allocators_pool_free(memory_context, context->workers);
    bc_allocators_pool_free(memory_context, context);
}

size_t bc_concurrency_thread_count(const bc_concurrency_context_t* context)
{
    return context->thread_count;
}

size_t bc_concurrency_effective_worker_count(const bc_concurrency_context_t* context)
{
    return context->thread_count + 1;
}

bool bc_concurrency_register_slot(bc_concurrency_context_t* context, const bc_concurrency_slot_config_t* config, size_t* out_slot_index)
{
    size_t idx = context->slot_count;

    if (idx >= context->slot_capacity) {
        size_t new_capacity = context->slot_capacity == 0 ? BC_CONCURRENCY_SLOT_INITIAL_CAPACITY : context->slot_capacity * 2;
        size_t new_size = new_capacity * sizeof(bc_concurrency_slot_config_t);
        bc_concurrency_slot_config_t* new_configs = NULL;
        if (!bc_allocators_pool_allocate(context->memory_context, new_size, (void**)&new_configs)) {
            return false;
        }
        if (context->slot_configs != NULL) {
            bc_core_copy(new_configs, context->slot_configs, context->slot_count * sizeof(bc_concurrency_slot_config_t));
            bc_allocators_pool_free(context->memory_context, context->slot_configs);
        }
        context->slot_configs = new_configs;
        context->slot_capacity = new_capacity;
    }

    context->slot_configs[idx] = *config;

    size_t new_slots_count = idx + 1;
    size_t slots_size = new_slots_count * sizeof(void*);

    size_t effective_count = context->thread_count + 1;
    size_t workers_completed = 0;
    for (size_t i = 0; i < effective_count; i++) {
        bc_concurrency_worker_t* worker = &context->workers[i];

        void** new_slots = NULL;
        if (!bc_allocators_pool_allocate(context->memory_context, slots_size, (void**)&new_slots)) {
            goto fail_slots;
        }

        if (worker->slots != NULL) {
            bc_core_copy(new_slots, worker->slots, idx * sizeof(void*));
            bc_allocators_pool_free(context->memory_context, worker->slots);
        }
        worker->slots = new_slots;

        void* slot_data = NULL;
        if (!bc_allocators_pool_allocate(context->memory_context, config->size, &slot_data)) {
            worker->slots[idx] = NULL;
            goto fail_slots;
        }
        bc_core_zero(slot_data, config->size);

        if (config->init != NULL) {
            config->init(slot_data, i, config->arg);
        }

        worker->slots[idx] = slot_data;
        workers_completed = i + 1;
    }

    context->slot_count = idx + 1;
    *out_slot_index = idx;
    return true;

fail_slots:
    for (size_t j = 0; j < workers_completed; j++) {
        if (context->workers[j].slots[idx] != NULL) {
            bc_allocators_pool_free(context->memory_context, context->workers[j].slots[idx]);
            context->workers[j].slots[idx] = NULL;
        }
    }
    return false;
}

void bc_concurrency_foreach_slot(bc_concurrency_context_t* context, size_t slot_index,
                                 void (*callback)(void* data, size_t worker_index, void* arg), void* arg)
{
    if (slot_index >= context->slot_count) {
        return;
    }
    size_t effective_count = context->thread_count + 1;
    for (size_t i = 0; i < context->thread_count; i++) {
        (void)atomic_load_explicit(&context->workers[i].work_done, memory_order_acquire);
    }
    for (size_t i = 0; i < effective_count; i++) {
        callback(context->workers[i].slots[slot_index], i, arg);
    }
}

bool bc_concurrency_submit(bc_concurrency_context_t* context, bc_concurrency_task_fn_t task_function, void* task_argument)
{
    bc_concurrency_task_entry_t entry = {task_function, task_argument};
    return bc_concurrency_staging_push(context->memory_context, &context->staging, entry);
}

bool bc_concurrency_submit_batch(bc_concurrency_context_t* context, bc_concurrency_task_fn_t task_function, void* const* task_arguments,
                                 size_t task_count)
{
    if (task_count == 0) {
        return false;
    }
    for (size_t i = 0; i < task_count; i++) {
        bc_concurrency_task_entry_t entry = {task_function, task_arguments[i]};
        if (!bc_concurrency_staging_push(context->memory_context, &context->staging, entry)) {
            return false;
        }
    }
    return true;
}

bool bc_concurrency_dispatch_and_wait(bc_concurrency_context_t* context)
{
    size_t staging_count = bc_concurrency_staging_length(&context->staging);
    if (staging_count == 0) {
        return true;
    }

    size_t thread_count = context->thread_count;

    if (thread_count == 0 || staging_count == 1) {
        tls_worker = &context->workers[thread_count];
        for (size_t i = 0; i < staging_count; i++) {
            context->staging.data[i].task_function(context->staging.data[i].task_argument);
        }
        tls_worker = NULL;
        bc_concurrency_staging_clear(&context->staging);
        return true;
    }

    if (!bc_concurrency_ensure_workers_spawned(context)) {
        return false;
    }

    size_t effective_count = thread_count + 1;

    if (staging_count > context->dispatch_capacity) {
        size_t new_cap = context->dispatch_capacity;
        while (new_cap < staging_count) {
            new_cap *= 2;
        }
        size_t new_size = new_cap * sizeof(bc_concurrency_task_entry_t);
        bc_concurrency_task_entry_t* new_tasks = NULL;
        if (!bc_allocators_pool_allocate(context->memory_context, new_size, (void**)&new_tasks)) {
            return false;
        }
        bc_allocators_pool_free(context->memory_context, context->dispatch_tasks);
        context->dispatch_tasks = new_tasks;
        context->dispatch_capacity = new_cap;
    }

    bc_concurrency_task_entry_t* task_array = context->dispatch_tasks;
    bc_concurrency_work_t* assignments = context->dispatch_work;

    __builtin_memcpy(task_array, context->staging.data, staging_count * sizeof(bc_concurrency_task_entry_t));

    size_t base = staging_count / effective_count;
    size_t extra = staging_count % effective_count;
    size_t offset = 0;

    for (size_t i = 0; i < effective_count; i++) {
        size_t count = base + (i < extra ? 1 : 0);
        assignments[i].tasks = task_array + offset;
        assignments[i].count = count;
        offset += count;
    }

    for (size_t i = 0; i < thread_count; i++) {
        if (assignments[i].count > 0) {
            bc_concurrency_dispatch_to_worker(&context->workers[i], &assignments[i]);
        }
    }

    if (assignments[thread_count].count > 0) {
        tls_worker = &context->workers[thread_count];
        bc_concurrency_task_entry_t* main_tasks = assignments[thread_count].tasks;
        size_t main_count = assignments[thread_count].count;
        for (size_t i = 0; i < main_count; i++) {
            main_tasks[i].task_function(main_tasks[i].task_argument);
        }
        tls_worker = NULL;
    }

    for (size_t i = 0; i < thread_count; i++) {
        if (assignments[i].count > 0) {
            bc_concurrency_wait_for_worker(&context->workers[i]);
        }
    }

    bc_concurrency_staging_clear(&context->staging);

    return true;
}
