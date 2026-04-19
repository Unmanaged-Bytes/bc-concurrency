// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"
#include "bc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ===== Helpers ===== */

#define MAX_WORKERS 64

static bc_concurrency_context_t* make_ctx(bc_allocators_context_t* mem)
{
    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);
    return ctx;
}

/* ===== Test: effective worker count = thread_count + 1 ===== */

static void test_effective_worker_count_is_thread_count_plus_one(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = make_ctx(mem);
    size_t bg = bc_concurrency_thread_count(ctx);
    size_t eff = bc_concurrency_effective_worker_count(ctx);
    assert_int_equal(eff, bg + 1);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== Test: main participates in dispatch_and_wait ===== */

typedef struct {
    bool used[MAX_WORKERS];
} usage_map_t;

static void record_worker_index(void* arg)
{
    size_t idx = bc_concurrency_worker_index();
    if (idx < MAX_WORKERS) {
        usage_map_t* map = arg;
        map->used[idx] = true;
    }
}

static void test_dispatch_main_worker_participates(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = make_ctx(mem);
    size_t effective = bc_concurrency_effective_worker_count(ctx);
    size_t main_idx = effective - 1;

    usage_map_t map;
    bc_core_zero(&map, sizeof(map));

    /* Submit exactly effective tasks so each worker gets exactly 1. */
    for (size_t i = 0; i < effective; i++) {
        bc_concurrency_submit(ctx, record_worker_index, &map);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    /* All effective workers, including main, should have run a task. */
    for (size_t i = 0; i < effective; i++) {
        assert_true(map.used[i]);
    }
    assert_true(map.used[main_idx]);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== Test: main participates and has worker memory ===== */

typedef struct {
    bc_allocators_context_t* memories[MAX_WORKERS];
} memory_map_t;

static void capture_worker_memory(void* arg)
{
    size_t idx = bc_concurrency_worker_index();
    if (idx < MAX_WORKERS) {
        memory_map_t* map = arg;
        map->memories[idx] = bc_concurrency_worker_memory();
    }
}

static void test_dispatch_main_worker_has_memory(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = make_ctx(mem);
    size_t effective = bc_concurrency_effective_worker_count(ctx);

    memory_map_t map;
    bc_core_zero(&map, sizeof(map));

    for (size_t i = 0; i < effective; i++) {
        bc_concurrency_submit(ctx, capture_worker_memory, &map);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    for (size_t i = 0; i < effective; i++) {
        assert_non_null(map.memories[i]);
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== Test: bc_concurrency_for main iterations have worker memory ===== */

typedef struct {
    bool memory_non_null[MAX_WORKERS];
} for_result_t;

static void iter_check_memory(size_t index, void* arg)
{
    (void)index;
    size_t idx = bc_concurrency_worker_index();
    if (idx < MAX_WORKERS) {
        for_result_t* res = arg;
        res->memory_non_null[idx] = (bc_concurrency_worker_memory() != NULL);
    }
}

static void test_parallel_for_main_has_worker_memory(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = make_ctx(mem);
    size_t effective = bc_concurrency_effective_worker_count(ctx);
    /* Use effective+1 iterations to guarantee the parallel path (> thread_count). */
    size_t iter_count = effective + 1;

    for_result_t res;
    bc_core_zero(&res, sizeof(res));

    bc_concurrency_for(ctx, 0, iter_count, 1, iter_check_memory, &res);

    for (size_t i = 0; i < effective; i++) {
        assert_true(res.memory_non_null[i]);
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== Test: foreach_slot iterates effective_worker_count workers ===== */

static size_t g_slot_callback_count;

static void count_slot_callback(void* data, size_t worker_index, void* arg)
{
    (void)data;
    (void)worker_index;
    (void)arg;
    g_slot_callback_count++;
}

static void test_foreach_slot_includes_main_worker(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = make_ctx(mem);
    size_t effective = bc_concurrency_effective_worker_count(ctx);

    bc_concurrency_slot_config_t slot_cfg = {.size = sizeof(size_t)};
    size_t slot_idx = SIZE_MAX;
    assert_true(bc_concurrency_register_slot(ctx, &slot_cfg, &slot_idx));

    g_slot_callback_count = 0;
    bc_concurrency_foreach_slot(ctx, slot_idx, count_slot_callback, NULL);

    assert_int_equal(g_slot_callback_count, effective);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== Test: slot totals correct with main participation ===== */

typedef struct {
    size_t count;
} slot_counter_t;

static void slot_counter_init(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    (void)arg;
    slot_counter_t* s = data;
    s->count = 0;
}

static size_t g_slot_sum_idx;

static void increment_my_slot(void* arg)
{
    (void)arg;
    slot_counter_t* s = bc_concurrency_worker_slot(g_slot_sum_idx);
    if (s != NULL) {
        s->count++;
    }
}

static size_t g_total_from_slots;

static void sum_slot(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    (void)arg;
    const slot_counter_t* s = data;
    g_total_from_slots += s->count;
}

static void test_slot_total_includes_main_worker_tasks(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = make_ctx(mem);
    size_t effective = bc_concurrency_effective_worker_count(ctx);

    bc_concurrency_slot_config_t slot_cfg = {
        .size = sizeof(slot_counter_t),
        .init = slot_counter_init,
    };
    g_slot_sum_idx = SIZE_MAX;
    assert_true(bc_concurrency_register_slot(ctx, &slot_cfg, &g_slot_sum_idx));

    /* Submit N × effective tasks for even distribution. */
    size_t task_count = effective * 100;
    for (size_t i = 0; i < task_count; i++) {
        bc_concurrency_submit(ctx, increment_my_slot, NULL);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    g_total_from_slots = 0;
    bc_concurrency_foreach_slot(ctx, g_slot_sum_idx, sum_slot, NULL);
    assert_int_equal(g_total_from_slots, task_count);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_effective_worker_count_is_thread_count_plus_one),
        cmocka_unit_test(test_dispatch_main_worker_participates),
        cmocka_unit_test(test_dispatch_main_worker_has_memory),
        cmocka_unit_test(test_parallel_for_main_has_worker_memory),
        cmocka_unit_test(test_foreach_slot_includes_main_worker),
        cmocka_unit_test(test_slot_total_includes_main_worker_tasks),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
