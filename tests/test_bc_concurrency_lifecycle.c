// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"

static void test_create_destroy_default_config(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    assert_true(bc_allocators_context_create(NULL, &mem));

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));
    assert_non_null(ctx);
    assert_true(bc_concurrency_thread_count(ctx) >= 1);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_create_destroy_default_config_explicit(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    assert_true(bc_allocators_context_create(NULL, &mem));

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));
    assert_true(bc_concurrency_thread_count(ctx) >= 1);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_destroy_without_dispatch(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    assert_true(bc_allocators_context_create(NULL, &mem));

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_dispatch_empty_staging(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    assert_true(bc_allocators_context_create(NULL, &mem));

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    assert_true(bc_concurrency_dispatch_and_wait(ctx));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_destroy_default_config),
        cmocka_unit_test(test_create_destroy_default_config_explicit),
        cmocka_unit_test(test_destroy_without_dispatch),
        cmocka_unit_test(test_dispatch_empty_staging),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
