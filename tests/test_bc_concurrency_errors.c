// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <pthread.h>
#include <stdbool.h>

static int wrap_pool_allocate_call_count = 0;
static int wrap_pool_allocate_fail_at = -1;

bool __real_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out);
bool __wrap_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out)
{
    wrap_pool_allocate_call_count++;
    if (wrap_pool_allocate_fail_at >= 0 && wrap_pool_allocate_call_count > wrap_pool_allocate_fail_at) {
        return false;
    }
    return __real_bc_allocators_pool_allocate(ctx, size, out);
}

static int wrap_pthread_create_fail = 0;

int __real_pthread_create(pthread_t* t, const pthread_attr_t* a, void* (*fn)(void*), void* arg);
int __wrap_pthread_create(pthread_t* t, const pthread_attr_t* a, void* (*fn)(void*), void* arg)
{
    if (wrap_pthread_create_fail) {
        return 1;
    }
    return __real_pthread_create(t, a, fn, arg);
}

static int wrap_pthread_attr_init_fail = 0;

int __real_pthread_attr_init(pthread_attr_t* a);
int __wrap_pthread_attr_init(pthread_attr_t* a)
{
    if (wrap_pthread_attr_init_fail) {
        return 1;
    }
    return __real_pthread_attr_init(a);
}

static int wrap_pthread_attr_setstacksize_fail = 0;

int __real_pthread_attr_setstacksize(pthread_attr_t* a, size_t s);
int __wrap_pthread_attr_setstacksize(pthread_attr_t* a, size_t s)
{
    if (wrap_pthread_attr_setstacksize_fail) {
        return 1;
    }
    return __real_pthread_attr_setstacksize(a, s);
}

static int test_setup(void** state)
{
    (void)state;
    wrap_pool_allocate_call_count = 0;
    wrap_pool_allocate_fail_at = -1;
    wrap_pthread_create_fail = 0;
    wrap_pthread_attr_init_fail = 0;
    wrap_pthread_attr_setstacksize_fail = 0;
    return 0;
}

static void noop_task(void* arg)
{
    (void)arg;
}

static void test_create_fails_on_pool_allocate(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    wrap_pool_allocate_fail_at = 0;

    bc_concurrency_context_t* ctx = NULL;
    assert_false(bc_concurrency_create(mem, NULL, &ctx));
    assert_null(ctx);

    bc_allocators_context_destroy(mem);
}

static void test_dispatch_fails_on_pthread_attr_init(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    bc_concurrency_submit(ctx, noop_task, NULL);
    bc_concurrency_submit(ctx, noop_task, NULL);

    wrap_pthread_attr_init_fail = 1;
    assert_false(bc_concurrency_dispatch_and_wait(ctx));
    wrap_pthread_attr_init_fail = 0;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_dispatch_fails_on_pthread_create(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    bc_concurrency_submit(ctx, noop_task, NULL);
    bc_concurrency_submit(ctx, noop_task, NULL);

    wrap_pthread_create_fail = 1;
    assert_false(bc_concurrency_dispatch_and_wait(ctx));
    wrap_pthread_create_fail = 0;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_create_fails_on_pool_allocate, test_setup),
        cmocka_unit_test_setup(test_dispatch_fails_on_pthread_attr_init, test_setup),
        cmocka_unit_test_setup(test_dispatch_fails_on_pthread_create, test_setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
