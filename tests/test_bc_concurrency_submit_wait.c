// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"
#include "bc_core.h"

#include <stdatomic.h>
#include <stdbool.h>

static void set_flag(void* arg)
{
    _Atomic bool* flag = arg;
    atomic_store(flag, true);
}

static void increment(void* arg)
{
    _Atomic size_t* counter = arg;
    atomic_fetch_add(counter, 1);
}

static bc_concurrency_context_t* make_ctx(bc_allocators_context_t* mem)
{
    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);
    return ctx;
}

static void test_submit_one_task(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    bc_concurrency_context_t* ctx = make_ctx(mem);

    _Atomic bool flag = false;
    assert_true(bc_concurrency_submit(ctx, set_flag, &flag));
    assert_true(bc_concurrency_dispatch_and_wait(ctx));
    assert_true(atomic_load(&flag));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_submit_many_tasks(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    bc_concurrency_context_t* ctx = make_ctx(mem);

    _Atomic size_t counter = 0;
    size_t task_count = 10000;

    for (size_t i = 0; i < task_count; i++) {
        assert_true(bc_concurrency_submit(ctx, increment, &counter));
    }
    assert_true(bc_concurrency_dispatch_and_wait(ctx));
    assert_int_equal(atomic_load(&counter), task_count);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_submit_batch(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    bc_concurrency_context_t* ctx = make_ctx(mem);

    enum { BATCH = 64 };
    _Atomic bool flags[BATCH];
    void* args[BATCH];
    for (size_t i = 0; i < BATCH; i++) {
        atomic_store(&flags[i], false);
        args[i] = &flags[i];
    }

    assert_true(bc_concurrency_submit_batch(ctx, set_flag, (void* const*)args, BATCH));
    assert_true(bc_concurrency_dispatch_and_wait(ctx));

    for (size_t i = 0; i < BATCH; i++) {
        assert_true(atomic_load(&flags[i]));
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_multiple_dispatch_cycles(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    bc_concurrency_context_t* ctx = make_ctx(mem);

    for (int cycle = 0; cycle < 10; cycle++) {
        _Atomic size_t counter = 0;
        for (size_t i = 0; i < 100; i++) {
            assert_true(bc_concurrency_submit(ctx, increment, &counter));
        }
        assert_true(bc_concurrency_dispatch_and_wait(ctx));
        assert_int_equal(atomic_load(&counter), 100);
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_submit_batch_zero_returns_false(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    bc_concurrency_context_t* ctx = make_ctx(mem);

    assert_false(bc_concurrency_submit_batch(ctx, set_flag, NULL, 0));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void capture_worker_index(void* arg)
{
    size_t* out = arg;
    *out = bc_concurrency_worker_index();
}

static void test_single_task_runs_on_main_worker(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    bc_concurrency_context_t* ctx = make_ctx(mem);

    size_t worker_index = SIZE_MAX;
    assert_true(bc_concurrency_submit(ctx, capture_worker_index, &worker_index));
    assert_true(bc_concurrency_dispatch_and_wait(ctx));

    size_t main_worker_index = bc_concurrency_effective_worker_count(ctx) - 1;
    assert_int_equal(worker_index, main_worker_index);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_staging_growth_multi_wave(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    bc_concurrency_context_t* ctx = make_ctx(mem);

    enum { TASKS_PER_WAVE = 2048, WAVE_COUNT = 4 };

    for (int wave = 0; wave < WAVE_COUNT; wave++) {
        _Atomic size_t counter = 0;
        for (size_t i = 0; i < TASKS_PER_WAVE; i++) {
            assert_true(bc_concurrency_submit(ctx, increment, &counter));
        }
        assert_true(bc_concurrency_dispatch_and_wait(ctx));
        assert_int_equal(atomic_load(&counter), TASKS_PER_WAVE);
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_submit_one_task),
        cmocka_unit_test(test_submit_many_tasks),
        cmocka_unit_test(test_submit_batch),
        cmocka_unit_test(test_multiple_dispatch_cycles),
        cmocka_unit_test(test_submit_batch_zero_returns_false),
        cmocka_unit_test(test_single_task_runs_on_main_worker),
        cmocka_unit_test(test_staging_growth_multi_wave),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
