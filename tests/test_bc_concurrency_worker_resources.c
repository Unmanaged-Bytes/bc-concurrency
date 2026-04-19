// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <stdatomic.h>
#include <stdint.h>

struct worker_info {
    bc_allocators_context_t* memory;
    size_t index;
};

static void capture_worker_resources(void* arg)
{
    struct worker_info* info = arg;
    info->memory = bc_concurrency_worker_memory();
    info->index = bc_concurrency_worker_index();
}

static void test_worker_memory_not_null(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    struct worker_info info = {0};
    bc_concurrency_submit(ctx, capture_worker_resources, &info);
    bc_concurrency_dispatch_and_wait(ctx);

    assert_non_null(info.memory);
    assert_true(info.index < bc_concurrency_effective_worker_count(ctx));

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_worker_index_unique(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    enum { N = 100 };
    struct worker_info infos[N];
    for (size_t i = 0; i < N; i++) {
        bc_concurrency_submit(ctx, capture_worker_resources, &infos[i]);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    size_t eff = bc_concurrency_effective_worker_count(ctx);
    for (size_t i = 0; i < N; i++) {
        assert_true(infos[i].index < eff);
        assert_non_null(infos[i].memory);
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_worker_resources_null_outside_task(void** state)
{
    (void)state;
    assert_null(bc_concurrency_worker_memory());
    assert_int_equal(bc_concurrency_worker_index(), SIZE_MAX);
    assert_null(bc_concurrency_worker_slot(0));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_worker_memory_not_null),
        cmocka_unit_test(test_worker_index_unique),
        cmocka_unit_test(test_worker_resources_null_outside_task),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
