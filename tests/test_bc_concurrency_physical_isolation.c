// SPDX-License-Identifier: MIT

/*
 * test_bc_concurrency_physical_isolation: verify that bc_concurrency_create pins
 * the calling thread (main) to exactly one physical core, and that every
 * background worker runs on a distinct physical core.
 *
 * Before the fix: the calling thread is not pinned (affinity = all CPUs),
 * so the OS may schedule it on the SMT sibling of worker 0, causing
 * contention on the same physical core.
 *
 * After the fix: bc_concurrency_create pins the calling thread to physical
 * core 0 (core_offset), and each worker i to physical core 1+i. No two
 * threads share a physical core.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <sched.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WORKERS 64

/* ===== Platform helpers ===== */

static size_t logical_cpu_to_physical_core(size_t cpu)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%zu/topology/core_id", cpu);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return cpu; /* fallback: cpu == core */
    }
    long core_id = -1;
    int rc = fscanf(f, "%ld", &core_id);
    fclose(f);
    if (rc != 1) {
        return cpu;
    }
    return core_id >= 0 ? (size_t)core_id : cpu;
}

static int cpu_set_count_bits(const cpu_set_t* set)
{
    int count = 0;
    for (size_t i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, set)) {
            count++;
        }
    }
    return count;
}

static size_t cpu_set_first_cpu(const cpu_set_t* set)
{
    for (size_t i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, set)) {
            return i;
        }
    }
    return SIZE_MAX;
}

/* ===== Test: calling thread is pinned to exactly 1 CPU after create ===== */

static void test_calling_thread_pinned_after_create(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    assert_int_equal(sched_getaffinity(0, sizeof(affinity), &affinity), 0);

    /* Main thread must be pinned to exactly 1 logical CPU. */
    assert_int_equal(cpu_set_count_bits(&affinity), 1);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== Test: each task reports a different physical core ===== */

typedef struct {
    size_t physical_cores[MAX_WORKERS];
} core_map_t;

static void record_physical_core(void* arg)
{
    size_t idx = bc_concurrency_worker_index();
    if (idx < MAX_WORKERS) {
        int logical_cpu = sched_getcpu();
        if (logical_cpu >= 0) {
            core_map_t* map = arg;
            map->physical_cores[idx] = logical_cpu_to_physical_core((size_t)logical_cpu);
        }
    }
}

static void test_workers_on_distinct_physical_cores(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    size_t effective = bc_concurrency_effective_worker_count(ctx);

    core_map_t map;
    for (size_t i = 0; i < MAX_WORKERS; i++) {
        map.physical_cores[i] = SIZE_MAX;
    }

    /* Submit exactly effective tasks — each worker gets 1 task. */
    for (size_t i = 0; i < effective; i++) {
        bc_concurrency_submit(ctx, record_physical_core, &map);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    /* Verify all recorded physical cores are distinct. */
    bool seen[MAX_WORKERS] = {false};
    for (size_t i = 0; i < effective; i++) {
        size_t core = map.physical_cores[i];
        assert_true(core < MAX_WORKERS);
        assert_false(seen[core]); /* duplicate physical core = SMT contention */
        seen[core] = true;
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

/* ===== Test: main thread and workers on distinct physical cores ===== */

static void test_main_not_on_same_core_as_worker(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    assert_true(bc_concurrency_create(mem, NULL, &ctx));

    /* Get main thread's current CPU (after pinning). */
    cpu_set_t main_affinity;
    CPU_ZERO(&main_affinity);
    sched_getaffinity(0, sizeof(main_affinity), &main_affinity);
    size_t main_logical_cpu = cpu_set_first_cpu(&main_affinity);
    assert_true(main_logical_cpu < CPU_SETSIZE);
    size_t main_physical_core = logical_cpu_to_physical_core(main_logical_cpu);

    /* Dispatch tasks and collect physical cores of all workers. */
    size_t effective = bc_concurrency_effective_worker_count(ctx);
    core_map_t map;
    for (size_t i = 0; i < MAX_WORKERS; i++) {
        map.physical_cores[i] = SIZE_MAX;
    }

    /* Background workers only (not main): submit thread_count tasks. */
    size_t bg_count = bc_concurrency_thread_count(ctx);
    for (size_t i = 0; i < bg_count; i++) {
        bc_concurrency_submit(ctx, record_physical_core, &map);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    /* No background worker should be on the same physical core as main. */
    for (size_t i = 0; i < bg_count; i++) {
        if (map.physical_cores[i] != SIZE_MAX) {
            assert_true(map.physical_cores[i] != main_physical_core);
        }
    }

    (void)effective;

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_calling_thread_pinned_after_create),
        cmocka_unit_test(test_workers_on_distinct_physical_cores),
        cmocka_unit_test(test_main_not_on_same_core_as_worker),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
