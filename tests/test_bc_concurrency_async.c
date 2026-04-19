// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <stdatomic.h>
#include <stdint.h>

static _Atomic size_t iteration_sum;

static void sum_index(size_t index, void* arg)
{
    (void)arg;
    atomic_fetch_add(&iteration_sum, index);
}

static void test_parallel_for_basic(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    atomic_store(&iteration_sum, 0);
    assert_true(bc_concurrency_for(ctx, 0, 1000, 1, sum_index, NULL));

    size_t expected = 999 * 1000 / 2;
    assert_int_equal(atomic_load(&iteration_sum), expected);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_parallel_for_with_step(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    atomic_store(&iteration_sum, 0);
    assert_true(bc_concurrency_for(ctx, 0, 10, 2, sum_index, NULL));

    size_t expected = 0 + 2 + 4 + 6 + 8;
    assert_int_equal(atomic_load(&iteration_sum), expected);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_parallel_for_empty_range(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    assert_true(bc_concurrency_for(ctx, 10, 10, 1, sum_index, NULL));
    assert_true(bc_concurrency_for(ctx, 10, 5, 1, sum_index, NULL));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_parallel_for_step_zero_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    assert_false(bc_concurrency_for(ctx, 0, 10, 0, sum_index, NULL));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_parallel_for_few_iterations_runs_inline(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    atomic_store(&iteration_sum, 0);
    assert_true(bc_concurrency_for(ctx, 0, 3, 1, sum_index, NULL));
    assert_int_equal(atomic_load(&iteration_sum), 3);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_parallel_for_basic),
        cmocka_unit_test(test_parallel_for_with_step),
        cmocka_unit_test(test_parallel_for_empty_range),
        cmocka_unit_test(test_parallel_for_step_zero_fails),
        cmocka_unit_test(test_parallel_for_few_iterations_runs_inline),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
