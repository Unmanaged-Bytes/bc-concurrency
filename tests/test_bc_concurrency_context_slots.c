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
    size_t count;
} counter_slot_t;

static void slot_init(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    (void)arg;
    counter_slot_t* slot = data;
    slot->count = 0;
}

static void increment_slot(void* arg)
{
    (void)arg;
    counter_slot_t* slot = bc_concurrency_worker_slot(0);
    if (slot != NULL) {
        slot->count++;
    }
}

static _Atomic size_t foreach_total;

static void sum_slots(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    (void)arg;
    counter_slot_t* slot = data;
    atomic_fetch_add(&foreach_total, slot->count);
}

static void test_register_and_use_slot(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    bc_concurrency_slot_config_t slot_cfg = {
        .size = sizeof(counter_slot_t),
        .init = slot_init,
    };
    size_t slot_idx = SIZE_MAX;
    assert_true(bc_concurrency_register_slot(ctx, &slot_cfg, &slot_idx));
    assert_int_equal(slot_idx, 0);

    size_t task_count = 1000;
    for (size_t i = 0; i < task_count; i++) {
        bc_concurrency_submit(ctx, increment_slot, NULL);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    atomic_store(&foreach_total, 0);
    bc_concurrency_foreach_slot(ctx, slot_idx, sum_slots, NULL);
    assert_int_equal(atomic_load(&foreach_total), task_count);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_multiple_slots(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    bc_concurrency_slot_config_t cfg0 = {.size = sizeof(counter_slot_t)};
    bc_concurrency_slot_config_t cfg1 = {.size = sizeof(size_t)};
    size_t idx0 = SIZE_MAX;
    size_t idx1 = SIZE_MAX;
    assert_true(bc_concurrency_register_slot(ctx, &cfg0, &idx0));
    assert_true(bc_concurrency_register_slot(ctx, &cfg1, &idx1));
    assert_int_equal(idx0, 0);
    assert_int_equal(idx1, 1);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

static void test_slot_null_outside_task(void** state)
{
    (void)state;
    assert_null(bc_concurrency_worker_slot(0));
    assert_null(bc_concurrency_worker_slot(99));
}

static void test_foreach_slot_invalid_index(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    bc_concurrency_foreach_slot(ctx, 99, sum_slots, NULL);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_register_and_use_slot),
        cmocka_unit_test(test_multiple_slots),
        cmocka_unit_test(test_slot_null_outside_task),
        cmocka_unit_test(test_foreach_slot_invalid_index),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
