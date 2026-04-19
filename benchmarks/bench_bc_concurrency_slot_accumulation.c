// SPDX-License-Identifier: MIT

/*
 * bench_slot_accumulation: throughput of the per-worker slot pattern.
 *
 * Models the deduplication scanner: each task writes a result into
 * its worker's private slot; after each wave the main thread merges
 * all slots via bc_concurrency_foreach_slot.
 */

#include "bc_concurrency.h"
#include "bc_allocators.h"
#include "bc_core.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define WAVE_COUNT ((size_t)50000)
#define WAVE_SIZE ((size_t)256)
#define WARMUP_WAVES ((size_t)5000)

typedef struct {
    size_t count;
    uint8_t _pad[56];
} slot_counter_t;

static size_t g_slot_index;

static void slot_init_cb(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    (void)arg;
    slot_counter_t* s = data;
    s->count = 0;
}

static void accumulate_task(void* arg)
{
    (void)arg;
    slot_counter_t* s = bc_concurrency_worker_slot(g_slot_index);
    if (s != NULL) {
        s->count++;
    }
}

typedef struct {
    size_t total;
} merge_state_t;

static void merge_cb(void* data, size_t worker_index, void* arg)
{
    (void)worker_index;
    merge_state_t* ms = arg;
    slot_counter_t* s = data;
    ms->total += s->count;
    s->count = 0;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    printf("bench_slot_accumulation: %zu waves × %zu tasks, slot counter per worker\n", WAVE_COUNT, WAVE_SIZE);
    printf("  each task: slot->count++  |  each wave: foreach_slot merge + reset\n\n");

    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    size_t bg = bc_concurrency_thread_count(ctx);
    size_t eff = bc_concurrency_effective_worker_count(ctx);

    bc_concurrency_slot_config_t slot_cfg = {
        .size = sizeof(slot_counter_t),
        .init = slot_init_cb,
    };
    bc_concurrency_register_slot(ctx, &slot_cfg, &g_slot_index);

    for (size_t w = 0; w < WARMUP_WAVES; w++) {
        for (size_t i = 0; i < WAVE_SIZE; i++) {
            bc_concurrency_submit(ctx, accumulate_task, NULL);
        }
        bc_concurrency_dispatch_and_wait(ctx);
        merge_state_t ms = {0};
        bc_concurrency_foreach_slot(ctx, g_slot_index, merge_cb, &ms);
    }

    uint64_t start = now_ns();
    uint64_t merge_total_ns = 0;
    size_t grand_total = 0;

    for (size_t w = 0; w < WAVE_COUNT; w++) {
        for (size_t i = 0; i < WAVE_SIZE; i++) {
            bc_concurrency_submit(ctx, accumulate_task, NULL);
        }
        bc_concurrency_dispatch_and_wait(ctx);

        uint64_t merge_start = now_ns();
        merge_state_t ms = {0};
        bc_concurrency_foreach_slot(ctx, g_slot_index, merge_cb, &ms);
        merge_total_ns += now_ns() - merge_start;
        grand_total += ms.total;
    }

    uint64_t elapsed = now_ns() - start;
    size_t total_tasks = WAVE_COUNT * WAVE_SIZE;

    double mtps = (double)total_tasks / ((double)elapsed / 1e9) / 1e6;
    double ns_per_merge = (double)merge_total_ns / (double)WAVE_COUNT;

    printf("  bg-threads=%-3zu  effective=%-3zu  %.2f Mtasks/sec  %.0f ns/merge  total=%s\n", bg, eff, mtps, ns_per_merge,
           grand_total == total_tasks ? "OK" : "MISMATCH");

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
    return 0;
}
