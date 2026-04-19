// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_concurrency_context_internal.h"
#include "bc_concurrency_signal.h"
#include "bc_concurrency_platform_internal.h"
#include "bc_allocators.h"
#include "bc_core.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* ===== Wrap: bc_allocators_pool_allocate with exact-call targeting ===== */

static atomic_int pool_allocate_call_count = 0;
static atomic_int pool_allocate_fail_at_call = -1;

bool __real_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out);
bool __wrap_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out)
{
    int current_call = atomic_fetch_add(&pool_allocate_call_count, 1);
    int fail_at = atomic_load(&pool_allocate_fail_at_call);
    if (fail_at >= 0 && current_call == fail_at) {
        return false;
    }
    return __real_bc_allocators_pool_allocate(ctx, size, out);
}

/* ===== Wrap: bc_allocators_context_create ===== */

static atomic_int context_create_call_count = 0;
static atomic_int context_create_fail_at_call = -1;

bool __real_bc_allocators_context_create(const bc_allocators_context_config_t* config, bc_allocators_context_t** out_ctx);
bool __wrap_bc_allocators_context_create(const bc_allocators_context_config_t* config, bc_allocators_context_t** out_ctx)
{
    int current_call = atomic_fetch_add(&context_create_call_count, 1);
    int fail_at = atomic_load(&context_create_fail_at_call);
    if (fail_at >= 0 && current_call == fail_at) {
        return false;
    }
    return __real_bc_allocators_context_create(config, out_ctx);
}

/* ===== Wrap: pthread_attr_setstacksize ===== */

static atomic_int pthread_attr_setstacksize_fail = 0;

int __real_pthread_attr_setstacksize(pthread_attr_t* a, size_t s);
int __wrap_pthread_attr_setstacksize(pthread_attr_t* a, size_t s)
{
    if (atomic_load(&pthread_attr_setstacksize_fail)) {
        return 1;
    }
    return __real_pthread_attr_setstacksize(a, s);
}

/* ===== Wrap: pthread_create with partial-fail support ===== */

static atomic_int pthread_create_call_count = 0;
static atomic_int pthread_create_fail_at_call = -1;

int __real_pthread_create(pthread_t* t, const pthread_attr_t* a, void* (*fn)(void*), void* arg);
int __wrap_pthread_create(pthread_t* t, const pthread_attr_t* a, void* (*fn)(void*), void* arg)
{
    int current_call = atomic_fetch_add(&pthread_create_call_count, 1);
    int fail_at = atomic_load(&pthread_create_fail_at_call);
    if (fail_at >= 0 && current_call == fail_at) {
        return 1;
    }
    return __real_pthread_create(t, a, fn, arg);
}

/* ===== Wrap: sigaction ===== */

static atomic_int sigaction_fail = 0;

int __real_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);
int __wrap_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    if (atomic_load(&sigaction_fail)) {
        return -1;
    }
    return __real_sigaction(signum, act, oldact);
}

/* ===== Wrap: nanosleep ===== */

typedef enum {
    NANOSLEEP_PASSTHROUGH = 0,
    NANOSLEEP_EINTR_ONCE = 1,
    NANOSLEEP_EINVAL = 2,
} nanosleep_mode_t;

static _Atomic nanosleep_mode_t nanosleep_mode = NANOSLEEP_PASSTHROUGH;
static atomic_int nanosleep_eintr_count = 0;

int __real_nanosleep(const struct timespec* req, struct timespec* rem);
int __wrap_nanosleep(const struct timespec* req, struct timespec* rem)
{
    nanosleep_mode_t mode = atomic_load(&nanosleep_mode);
    if (mode == NANOSLEEP_EINTR_ONCE && atomic_load(&nanosleep_eintr_count) == 0) {
        atomic_fetch_add(&nanosleep_eintr_count, 1);
        if (rem != NULL) {
            *rem = *req;
        }
        errno = EINTR;
        return -1;
    }
    if (mode == NANOSLEEP_EINVAL) {
        errno = EINVAL;
        return -1;
    }
    return __real_nanosleep(req, rem);
}

/* ===== Test setup ===== */

static int test_setup(void** state)
{
    (void)state;
    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = -1;
    context_create_call_count = 0;
    context_create_fail_at_call = -1;
    pthread_attr_setstacksize_fail = 0;
    pthread_create_call_count = 0;
    pthread_create_fail_at_call = -1;
    sigaction_fail = 0;
    nanosleep_mode = NANOSLEEP_PASSTHROUGH;
    nanosleep_eintr_count = 0;
    return 0;
}

static void noop_task(void* arg)
{
    (void)arg;
}

/* ===== bc_concurrency_create: allocation failure paths ===== */

static void test_create_fails_on_each_allocation(void** state)
{
    (void)state;

    for (int fail_at = 0; fail_at < 12; fail_at++) {
        bc_allocators_context_t* mem = NULL;
        assert_true(__real_bc_allocators_context_create(NULL, &mem));

        pool_allocate_call_count = 0;
        pool_allocate_fail_at_call = fail_at;

        bc_concurrency_context_t* ctx = NULL;
        bool created = bc_concurrency_create(mem, NULL, &ctx);

        if (created) {
            bc_concurrency_destroy(ctx);
        } else {
            assert_null(ctx);
        }

        pool_allocate_fail_at_call = -1;
        bc_allocators_context_destroy(mem);
    }
}

static void test_create_fails_on_worker_context_create(void** state)
{
    (void)state;

    for (int fail_at = 0; fail_at < 8; fail_at++) {
        bc_allocators_context_t* mem = NULL;
        assert_true(__real_bc_allocators_context_create(NULL, &mem));

        context_create_call_count = 0;
        context_create_fail_at_call = fail_at;

        bc_concurrency_context_t* ctx = NULL;
        bool created = bc_concurrency_create(mem, NULL, &ctx);

        if (created) {
            bc_concurrency_destroy(ctx);
        } else {
            assert_null(ctx);
        }

        context_create_fail_at_call = -1;
        bc_allocators_context_destroy(mem);
    }
}

/* ===== bc_concurrency_create: config variations ===== */

static void test_create_with_explicit_config(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 2 * 1024 * 1024,
    };

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, &config, &ctx));
    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_create_with_tiny_stack_clamped_to_min(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 1024,
    };

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, &config, &ctx));
    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== spawn_workers: pthread failures ===== */

static void test_dispatch_fails_on_pthread_attr_setstacksize(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    bc_concurrency_submit(ctx, noop_task, NULL);
    bc_concurrency_submit(ctx, noop_task, NULL);

    pthread_attr_setstacksize_fail = 1;
    assert_false(bc_concurrency_dispatch_and_wait(ctx));
    pthread_attr_setstacksize_fail = 0;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_dispatch_fails_on_partial_pthread_create(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    if (bc_concurrency_thread_count(ctx) < 2) {
        bc_concurrency_destroy(ctx);
        bc_allocators_context_destroy(mem);
        skip();
        return;
    }

    bc_concurrency_submit(ctx, noop_task, NULL);
    bc_concurrency_submit(ctx, noop_task, NULL);

    pthread_create_call_count = 0;
    pthread_create_fail_at_call = 1;
    assert_false(bc_concurrency_dispatch_and_wait(ctx));
    pthread_create_fail_at_call = -1;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== register_slot: allocation failure paths ===== */

static void slot_init(void* data, size_t worker_index, void* arg)
{
    (void)data;
    (void)worker_index;
    (void)arg;
}

static void test_register_slot_fails_on_configs_allocation(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = 0;

    bc_concurrency_slot_config_t slot_cfg = {.size = 64, .init = slot_init, .destroy = NULL, .arg = NULL};
    size_t slot_idx = 0;
    assert_false(bc_concurrency_register_slot(ctx, &slot_cfg, &slot_idx));

    pool_allocate_fail_at_call = -1;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_register_slot_fails_on_slot_data_allocation(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    bc_concurrency_slot_config_t slot_cfg_ok = {.size = 64, .init = slot_init, .destroy = NULL, .arg = NULL};
    size_t first_slot = 0;
    assert_true(bc_concurrency_register_slot(ctx, &slot_cfg_ok, &first_slot));

    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = 1;

    bc_concurrency_slot_config_t slot_cfg = {.size = 64, .init = slot_init, .destroy = NULL, .arg = NULL};
    size_t slot_idx = 0;
    assert_false(bc_concurrency_register_slot(ctx, &slot_cfg, &slot_idx));

    pool_allocate_fail_at_call = -1;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== submit_batch: empty and failure ===== */

static void test_submit_batch_empty_returns_false(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    assert_false(bc_concurrency_submit_batch(ctx, noop_task, NULL, 0));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== parallel_for: error paths ===== */

static void iter_noop(size_t index, void* arg)
{
    (void)index;
    (void)arg;
}

static void test_parallel_for_zero_step_returns_false(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    assert_false(bc_concurrency_for(ctx, 0, 100, 0, iter_noop, NULL));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_parallel_for_empty_range_returns_true(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    assert_true(bc_concurrency_for(ctx, 10, 10, 1, iter_noop, NULL));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_parallel_for_fails_on_pool_allocate(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    size_t tc = bc_concurrency_thread_count(ctx);
    if (tc == 0) {
        bc_concurrency_destroy(ctx);
        bc_allocators_context_destroy(mem);
        skip();
    }

    bc_concurrency_submit(ctx, noop_task, NULL);
    bc_concurrency_submit(ctx, noop_task, NULL);
    bc_concurrency_dispatch_and_wait(ctx);

    size_t total = (tc + 1) * 100;

    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = 0;

    bool result = bc_concurrency_for(ctx, 0, total, 1, iter_noop, NULL);
    assert_false(result);

    pool_allocate_fail_at_call = -1;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== signal: sigaction failure ===== */

static void test_signal_install_fails_on_sigaction(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));

    sigaction_fail = 1;
    assert_false(bc_concurrency_signal_handler_install(handler, SIGUSR1));
    sigaction_fail = 0;

    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_signal_handler_create_fails_on_pool_allocate(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = 0;

    bc_concurrency_signal_handler_t* handler = NULL;
    assert_false(bc_concurrency_signal_handler_create(mem, &handler));
    assert_null(handler);

    pool_allocate_fail_at_call = -1;
    bc_allocators_context_destroy(mem);
}

/* ===== signal: sleep edge cases ===== */

static void test_sleep_zero_milliseconds(void** state)
{
    (void)state;
    bool interrupted = true;
    assert_true(bc_concurrency_sleep_milliseconds(0, NULL, &interrupted));
    assert_false(interrupted);
}

static void test_sleep_huge_duration_returns_false_on_overflow(void** state)
{
    (void)state;

    bool interrupted = false;
    assert_false(bc_concurrency_sleep_milliseconds(SIZE_MAX, NULL, &interrupted));
}

static void test_sleep_retries_on_eintr_without_handler(void** state)
{
    (void)state;
    nanosleep_mode = NANOSLEEP_EINTR_ONCE;
    nanosleep_eintr_count = 0;

    bool interrupted = false;
    assert_true(bc_concurrency_sleep_milliseconds(1, NULL, &interrupted));
    assert_false(interrupted);

    nanosleep_mode = NANOSLEEP_PASSTHROUGH;
}

static void test_sleep_fails_on_non_eintr_error(void** state)
{
    (void)state;
    nanosleep_mode = NANOSLEEP_EINVAL;

    bool interrupted = false;
    assert_false(bc_concurrency_sleep_milliseconds(1, NULL, &interrupted));

    nanosleep_mode = NANOSLEEP_PASSTHROUGH;
}

static void test_sleep_short_duration_no_interrupt(void** state)
{
    (void)state;
    bool interrupted = true;
    assert_true(bc_concurrency_sleep_milliseconds(1, NULL, &interrupted));
    assert_false(interrupted);
}

/* ===== platform: get_cpu_count direct call ===== */

static void test_platform_get_cpu_count_returns_positive(void** state)
{
    (void)state;
    size_t count = bc_concurrency_platform_get_cpu_count();
    assert_true(count >= 1);
}

static void test_platform_pin_out_of_range_returns_false(void** state)
{
    (void)state;
    size_t physical = bc_concurrency_platform_get_physical_core_count();
    assert_false(bc_concurrency_platform_pin_current_thread_to_physical_core(physical + 1000));
}

/* ===== parallel_for: ensure_workers_spawned failure ===== */

static void test_parallel_for_fails_on_spawn_workers(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    size_t tc = bc_concurrency_thread_count(ctx);
    if (tc == 0) {
        bc_concurrency_destroy(ctx);
        bc_allocators_context_destroy(mem);
        skip();
    }

    pthread_attr_setstacksize_fail = 1;

    size_t total = (tc + 1) * 100;
    bool result = bc_concurrency_for(ctx, 0, total, 1, iter_noop, NULL);
    assert_false(result);

    pthread_attr_setstacksize_fail = 0;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== submit_batch: staging_push failure ===== */

static void test_submit_batch_fails_on_staging_push(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    /* Force staging_push to fail by making allocation fail when staging grows. */
    void* dummy_args[3] = {NULL, NULL, NULL};

    /* First fill initial capacity to force realloc on next push. */
    for (int i = 0; i < 1024; i++) {
        bc_concurrency_submit(ctx, noop_task, NULL);
    }

    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = 0;

    bool result = bc_concurrency_submit_batch(ctx, noop_task, dummy_args, 3);
    assert_false(result);

    pool_allocate_fail_at_call = -1;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== dispatch_and_wait: large staging with allocation failure ===== */

static void test_dispatch_grow_allocation_failure(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    /* First dispatch to spawn workers so ensure_workers_spawned returns true fast. */
    bc_concurrency_submit(ctx, noop_task, NULL);
    bc_concurrency_submit(ctx, noop_task, NULL);
    assert_true(bc_concurrency_dispatch_and_wait(ctx));

    /* Submit enough tasks to exceed dispatch_capacity (1024). */
    for (int i = 0; i < 1100; i++) {
        bc_concurrency_submit(ctx, noop_task, NULL);
    }

    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = 0;

    bool result = bc_concurrency_dispatch_and_wait(ctx);
    assert_false(result);

    pool_allocate_fail_at_call = -1;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== register_slot: realloc path and partial failure ===== */

static void test_register_slot_multiple_triggers_realloc(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    bc_concurrency_slot_config_t slot_cfg = {.size = 32, .init = slot_init, .destroy = NULL, .arg = NULL};

    /* Force capacity growth beyond BC_CONCURRENCY_SLOT_INITIAL_CAPACITY (= 4). */
    for (int i = 0; i < 6; i++) {
        size_t slot_idx = 0;
        assert_true(bc_concurrency_register_slot(ctx, &slot_cfg, &slot_idx));
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_register_slot_fails_in_worker_loop(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    /* First register_slot consumes allocations: 1 for configs, 2 per worker (slots ptr + slot data).
     * For thread_count workers + main worker, we fail on a middle allocation. */
    pool_allocate_call_count = 0;
    pool_allocate_fail_at_call = 3;

    bc_concurrency_slot_config_t slot_cfg = {.size = 32, .init = slot_init, .destroy = NULL, .arg = NULL};
    size_t slot_idx = 0;
    assert_false(bc_concurrency_register_slot(ctx, &slot_cfg, &slot_idx));

    pool_allocate_fail_at_call = -1;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== dispatch_and_wait: grow dispatch_tasks on large staging ===== */

static void test_dispatch_grows_task_array(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    /* BC_CONCURRENCY_STAGING_INITIAL_CAPACITY = 1024. Submit more to force realloc. */
    for (int i = 0; i < 2100; i++) {
        bc_concurrency_submit(ctx, noop_task, NULL);
    }
    assert_true(bc_concurrency_dispatch_and_wait(ctx));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static atomic_int slot_destroy_counter = 0;

static void slot_destroy(void* data, size_t worker_index, void* arg)
{
    (void)data;
    (void)worker_index;
    (void)arg;
    atomic_fetch_add(&slot_destroy_counter, 1);
}

static void test_destroy_invokes_slot_destroy_callback(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    __real_bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    atomic_store(&slot_destroy_counter, 0);

    bc_concurrency_slot_config_t slot_cfg = {.size = 64, .init = slot_init, .destroy = slot_destroy, .arg = NULL};
    size_t slot_idx = 0;
    assert_true(bc_concurrency_register_slot(ctx, &slot_cfg, &slot_idx));

    size_t effective = bc_concurrency_effective_worker_count(ctx);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);

    assert_int_equal(atomic_load(&slot_destroy_counter), (int)effective);
}

/* ===== Entry point ===== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_create_fails_on_each_allocation, test_setup),
        cmocka_unit_test_setup(test_create_fails_on_worker_context_create, test_setup),
        cmocka_unit_test_setup(test_create_with_explicit_config, test_setup),
        cmocka_unit_test_setup(test_create_with_tiny_stack_clamped_to_min, test_setup),
        cmocka_unit_test_setup(test_dispatch_fails_on_pthread_attr_setstacksize, test_setup),
        cmocka_unit_test_setup(test_dispatch_fails_on_partial_pthread_create, test_setup),
        cmocka_unit_test_setup(test_register_slot_fails_on_configs_allocation, test_setup),
        cmocka_unit_test_setup(test_register_slot_fails_on_slot_data_allocation, test_setup),
        cmocka_unit_test_setup(test_submit_batch_empty_returns_false, test_setup),
        cmocka_unit_test_setup(test_parallel_for_zero_step_returns_false, test_setup),
        cmocka_unit_test_setup(test_parallel_for_empty_range_returns_true, test_setup),
        cmocka_unit_test_setup(test_parallel_for_fails_on_pool_allocate, test_setup),
        cmocka_unit_test_setup(test_signal_install_fails_on_sigaction, test_setup),
        cmocka_unit_test_setup(test_signal_handler_create_fails_on_pool_allocate, test_setup),
        cmocka_unit_test_setup(test_sleep_zero_milliseconds, test_setup),
        cmocka_unit_test_setup(test_sleep_huge_duration_returns_false_on_overflow, test_setup),
        cmocka_unit_test_setup(test_sleep_retries_on_eintr_without_handler, test_setup),
        cmocka_unit_test_setup(test_sleep_fails_on_non_eintr_error, test_setup),
        cmocka_unit_test_setup(test_sleep_short_duration_no_interrupt, test_setup),
        cmocka_unit_test_setup(test_platform_get_cpu_count_returns_positive, test_setup),
        cmocka_unit_test_setup(test_platform_pin_out_of_range_returns_false, test_setup),
        cmocka_unit_test_setup(test_destroy_invokes_slot_destroy_callback, test_setup),
        cmocka_unit_test_setup(test_register_slot_multiple_triggers_realloc, test_setup),
        cmocka_unit_test_setup(test_register_slot_fails_in_worker_loop, test_setup),
        cmocka_unit_test_setup(test_dispatch_grows_task_array, test_setup),
        cmocka_unit_test_setup(test_submit_batch_fails_on_staging_push, test_setup),
        cmocka_unit_test_setup(test_dispatch_grow_allocation_failure, test_setup),
        cmocka_unit_test_setup(test_parallel_for_fails_on_spawn_workers, test_setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
