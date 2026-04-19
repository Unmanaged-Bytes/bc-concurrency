// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include <stdatomic.h>
#include <stdbool.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"
#include "bc_concurrency_platform_internal.h"

static void test_default_config_auto_worker_count_matches_physical_core_count_minus_one(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));

    bc_concurrency_context_t* context = NULL;
    assert_true(bc_concurrency_create(memory, NULL, &context));

    size_t physical_core_count = bc_concurrency_platform_get_physical_core_count();
    size_t expected_thread_count = physical_core_count > 1 ? physical_core_count - 1 : 0;
    assert_int_equal(bc_concurrency_thread_count(context), expected_thread_count);
    assert_int_equal(bc_concurrency_effective_worker_count(context), expected_thread_count + 1);

    bc_concurrency_destroy(context);
    bc_allocators_context_destroy(memory);
}

static void test_explicit_worker_count_zero_produces_single_effective_worker(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 0,
        .worker_count_explicit = true,
        .worker_count = 0,
    };
    bc_concurrency_context_t* context = NULL;
    assert_true(bc_concurrency_create(memory, &config, &context));

    assert_int_equal(bc_concurrency_thread_count(context), 0);
    assert_int_equal(bc_concurrency_effective_worker_count(context), 1);

    bc_concurrency_destroy(context);
    bc_allocators_context_destroy(memory);
}

static void test_explicit_worker_count_n_produces_n_plus_one_effective_workers(void** state)
{
    (void)state;
    size_t physical_core_count = bc_concurrency_platform_get_physical_core_count();
    if (physical_core_count < 3) {
        skip();
    }
    size_t requested = 2;

    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 0,
        .worker_count_explicit = true,
        .worker_count = requested,
    };
    bc_concurrency_context_t* context = NULL;
    assert_true(bc_concurrency_create(memory, &config, &context));

    assert_int_equal(bc_concurrency_thread_count(context), requested);
    assert_int_equal(bc_concurrency_effective_worker_count(context), requested + 1);

    bc_concurrency_destroy(context);
    bc_allocators_context_destroy(memory);
}

static void test_explicit_worker_count_exceeding_physical_cores_is_rejected(void** state)
{
    (void)state;
    size_t physical_core_count = bc_concurrency_platform_get_physical_core_count();

    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 0,
        .worker_count_explicit = true,
        .worker_count = physical_core_count,
    };
    bc_concurrency_context_t* context = NULL;
    assert_false(bc_concurrency_create(memory, &config, &context));
    assert_null(context);

    bc_allocators_context_destroy(memory);
}

static void test_core_offset_beyond_physical_cores_is_rejected(void** state)
{
    (void)state;
    size_t physical_core_count = bc_concurrency_platform_get_physical_core_count();

    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));

    bc_concurrency_config_t config = {
        .core_offset = physical_core_count,
        .worker_stack_size = 0,
        .worker_count_explicit = true,
        .worker_count = 0,
    };
    bc_concurrency_context_t* context = NULL;
    assert_false(bc_concurrency_create(memory, &config, &context));
    assert_null(context);

    bc_allocators_context_destroy(memory);
}

static atomic_int bc_concurrency_worker_count_test_iteration_counter;

static void bc_concurrency_worker_count_test_iteration(size_t index, void* argument)
{
    (void)index;
    (void)argument;
    atomic_fetch_add_explicit(&bc_concurrency_worker_count_test_iteration_counter, 1, memory_order_relaxed);
}

static void test_parallel_for_with_worker_count_zero_runs_on_main_thread_only(void** state)
{
    (void)state;

    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 0,
        .worker_count_explicit = true,
        .worker_count = 0,
    };
    bc_concurrency_context_t* context = NULL;
    assert_true(bc_concurrency_create(memory, &config, &context));

    atomic_store_explicit(&bc_concurrency_worker_count_test_iteration_counter, 0, memory_order_relaxed);
    assert_true(bc_concurrency_for(context, 0, 128, 1, bc_concurrency_worker_count_test_iteration, NULL));
    assert_int_equal(atomic_load_explicit(&bc_concurrency_worker_count_test_iteration_counter, memory_order_relaxed), 128);

    bc_concurrency_destroy(context);
    bc_allocators_context_destroy(memory);
}

static atomic_int bc_concurrency_worker_count_test_task_counter;

static void bc_concurrency_worker_count_test_task(void* argument)
{
    (void)argument;
    atomic_fetch_add_explicit(&bc_concurrency_worker_count_test_task_counter, 1, memory_order_relaxed);
}

static void test_submit_and_dispatch_and_wait_with_worker_count_zero_runs_all_tasks_on_main(void** state)
{
    (void)state;

    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 0,
        .worker_count_explicit = true,
        .worker_count = 0,
    };
    bc_concurrency_context_t* context = NULL;
    assert_true(bc_concurrency_create(memory, &config, &context));

    atomic_store_explicit(&bc_concurrency_worker_count_test_task_counter, 0, memory_order_relaxed);
    for (size_t task_index = 0; task_index < 16; ++task_index) {
        assert_true(bc_concurrency_submit(context, bc_concurrency_worker_count_test_task, NULL));
    }
    assert_true(bc_concurrency_dispatch_and_wait(context));
    assert_int_equal(atomic_load_explicit(&bc_concurrency_worker_count_test_task_counter, memory_order_relaxed), 16);

    bc_concurrency_destroy(context);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_default_config_auto_worker_count_matches_physical_core_count_minus_one),
        cmocka_unit_test(test_explicit_worker_count_zero_produces_single_effective_worker),
        cmocka_unit_test(test_explicit_worker_count_n_produces_n_plus_one_effective_workers),
        cmocka_unit_test(test_explicit_worker_count_exceeding_physical_cores_is_rejected),
        cmocka_unit_test(test_core_offset_beyond_physical_cores_is_rejected),
        cmocka_unit_test(test_parallel_for_with_worker_count_zero_runs_on_main_thread_only),
        cmocka_unit_test(test_submit_and_dispatch_and_wait_with_worker_count_zero_runs_all_tasks_on_main),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
