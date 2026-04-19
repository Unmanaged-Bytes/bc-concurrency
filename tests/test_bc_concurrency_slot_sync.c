// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    int* records;
    size_t record_count;
} collector_like_t;

typedef struct {
    collector_like_t* collector;
} pointer_slot_t;

static void pointer_slot_init(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    (void)arg;
    pointer_slot_t* slot = data;
    slot->collector = NULL;
}

static void worker_task_allocate_and_write(void* arg)
{
    bc_allocators_context_t* worker_memory = bc_concurrency_worker_memory();
    pointer_slot_t* slot = bc_concurrency_worker_slot(0);
    if (slot == NULL || worker_memory == NULL) {
        return;
    }
    collector_like_t* collector = NULL;
    if (!bc_allocators_pool_allocate(worker_memory, sizeof(*collector), (void**)&collector)) {
        return;
    }
    memset(collector, 0, sizeof(*collector));
    if (!bc_allocators_pool_allocate(worker_memory, sizeof(int) * 8, (void**)&collector->records)) {
        return;
    }
    for (size_t i = 0; i < 8; ++i) {
        collector->records[i] = (int)(intptr_t)arg + (int)i;
    }
    collector->record_count = 8;
    slot->collector = collector;
}

static _Atomic int foreach_sum;

static void foreach_sum_heap_values(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    (void)arg;
    pointer_slot_t* slot = data;
    if (slot != NULL && slot->collector != NULL) {
        for (size_t i = 0; i < slot->collector->record_count; ++i) {
            atomic_fetch_add(&foreach_sum, slot->collector->records[i]);
        }
    }
}

static void test_foreach_sees_worker_heap_writes(void** state)
{
    (void)state;
    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    bc_concurrency_config_t config = {0};
    config.worker_count_explicit = true;
    config.worker_count = 1;

    bc_concurrency_context_t* concurrency_context = NULL;
    assert_true(bc_concurrency_create(memory_context, &config, &concurrency_context));

    bc_concurrency_slot_config_t slot_config = {
        .size = sizeof(pointer_slot_t),
        .init = pointer_slot_init,
    };
    size_t slot_index = SIZE_MAX;
    assert_true(bc_concurrency_register_slot(concurrency_context, &slot_config, &slot_index));

    size_t effective_worker_count = bc_concurrency_effective_worker_count(concurrency_context);
    for (size_t i = 0; i < effective_worker_count; ++i) {
        assert_true(bc_concurrency_submit(concurrency_context, worker_task_allocate_and_write, (void*)(intptr_t)(i + 1)));
    }
    assert_true(bc_concurrency_dispatch_and_wait(concurrency_context));

    atomic_store(&foreach_sum, 0);
    bc_concurrency_foreach_slot(concurrency_context, slot_index, foreach_sum_heap_values, NULL);

    int expected_sum = 0;
    for (size_t worker = 0; worker < effective_worker_count; ++worker) {
        int base = (int)(worker + 1);
        for (size_t i = 0; i < 8; ++i) {
            expected_sum += base + (int)i;
        }
    }
    assert_int_equal(atomic_load(&foreach_sum), expected_sum);

    bc_concurrency_destroy(concurrency_context);
    bc_allocators_context_destroy(memory_context);
}

static void test_foreach_sees_writes_after_many_dispatches(void** state)
{
    (void)state;
    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    bc_concurrency_config_t config = {0};
    config.worker_count_explicit = true;
    config.worker_count = 1;

    bc_concurrency_context_t* concurrency_context = NULL;
    assert_true(bc_concurrency_create(memory_context, &config, &concurrency_context));

    bc_concurrency_slot_config_t slot_config = {
        .size = sizeof(pointer_slot_t),
        .init = pointer_slot_init,
    };
    size_t slot_index = SIZE_MAX;
    assert_true(bc_concurrency_register_slot(concurrency_context, &slot_config, &slot_index));

    size_t effective_worker_count = bc_concurrency_effective_worker_count(concurrency_context);

    for (size_t round = 0; round < 50; ++round) {
        for (size_t i = 0; i < effective_worker_count; ++i) {
            assert_true(bc_concurrency_submit(concurrency_context, worker_task_allocate_and_write, (void*)(intptr_t)(round * 10 + i + 1)));
        }
        assert_true(bc_concurrency_dispatch_and_wait(concurrency_context));

        atomic_store(&foreach_sum, 0);
        bc_concurrency_foreach_slot(concurrency_context, slot_index, foreach_sum_heap_values, NULL);

        int expected_sum = 0;
        for (size_t i = 0; i < effective_worker_count; ++i) {
            int base = (int)(round * 10 + i + 1);
            for (size_t j = 0; j < 8; ++j) {
                expected_sum += base + (int)j;
            }
        }
        assert_int_equal(atomic_load(&foreach_sum), expected_sum);
    }

    bc_concurrency_destroy(concurrency_context);
    bc_allocators_context_destroy(memory_context);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_foreach_sees_worker_heap_writes),
        cmocka_unit_test(test_foreach_sees_writes_after_many_dispatches),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
