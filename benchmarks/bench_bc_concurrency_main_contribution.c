// SPDX-License-Identifier: MIT

/*
 * bench_main_contribution: CRC32c throughput with all physical cores.
 *
 * With the physical-isolation model, thread count is fixed by hardware:
 * one background worker per physical core, main thread on its own core.
 * This benchmark reports the raw throughput and effective processor count.
 */

#include "bc_concurrency.h"
#include "bc_allocators.h"
#include "bc_core.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TOTAL_TASKS ((size_t)5000000)
#define WAVE_SIZE ((size_t)512)
#define WARMUP_TASKS ((size_t)500000)
#define DATA_SIZE ((size_t)256)

static uint8_t g_data[DATA_SIZE];

static void crc_task(void* arg)
{
    (void)arg;
    uint32_t crc = 0;
    bc_core_crc32c(g_data, DATA_SIZE, &crc);
    (void)crc;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    bc_core_zero(g_data, DATA_SIZE);

    printf("bench_main_contribution: CRC32c(256B) throughput — %zu waves × %zu tasks\n", TOTAL_TASKS / WAVE_SIZE, WAVE_SIZE);
    printf("  one physical core per worker, no SMT sharing\n\n");

    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    size_t bg = bc_concurrency_thread_count(ctx);
    size_t eff = bc_concurrency_effective_worker_count(ctx);

    for (size_t submitted = 0; submitted < WARMUP_TASKS; submitted += WAVE_SIZE) {
        for (size_t i = 0; i < WAVE_SIZE; i++) {
            bc_concurrency_submit(ctx, crc_task, NULL);
        }
        bc_concurrency_dispatch_and_wait(ctx);
    }

    uint64_t start = now_ns();
    for (size_t submitted = 0; submitted < TOTAL_TASKS; submitted += WAVE_SIZE) {
        for (size_t i = 0; i < WAVE_SIZE; i++) {
            bc_concurrency_submit(ctx, crc_task, NULL);
        }
        bc_concurrency_dispatch_and_wait(ctx);
    }
    uint64_t elapsed = now_ns() - start;

    double mtps = (double)TOTAL_TASKS / ((double)elapsed / 1e9) / 1e6;

    printf("  bg-threads=%-3zu  effective=%-3zu  %.2f Mtasks/sec\n", bg, eff, mtps);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
    return 0;
}
