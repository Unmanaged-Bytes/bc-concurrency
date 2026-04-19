// SPDX-License-Identifier: MIT

/*
 * bench_wave_throughput: many short dispatch waves with real compute.
 *
 * Models the filesystem scanner pattern used by storageradar and bc-tools:
 *   for each directory batch:
 *     submit M tasks (one per file) → dispatch_and_wait → merge results
 *
 * Task: CRC32c of a 256-byte static buffer (non-trivial, non-noop).
 * Metric: wave latency (µs/wave) and total task throughput (Mtasks/sec).
 */

#include "bc_concurrency.h"
#include "bc_allocators.h"
#include "bc_core.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define WAVE_COUNT ((size_t)100000)
#define WARMUP_WAVES ((size_t)10000)
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

    printf("bench_wave_throughput: %zu waves × M tasks, CRC32c(256B) per task\n", WAVE_COUNT);
    printf("  models filesystem scanner pattern (readdir batch → hash → merge)\n\n");

    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    size_t bg = bc_concurrency_thread_count(ctx);
    size_t eff = bc_concurrency_effective_worker_count(ctx);

    printf("  bg-threads=%-2zu  effective=%-2zu\n", bg, eff);
    printf("  %-12s  %-18s  %s\n", "wave-size", "µs/wave", "Mtasks/sec");

    static const size_t wave_sizes[] = {1, 8, 32, 128, 512, 2048};
    size_t n_sizes = sizeof(wave_sizes) / sizeof(wave_sizes[0]);

    for (size_t si = 0; si < n_sizes; si++) {
        size_t wave_size = wave_sizes[si];

        for (size_t w = 0; w < WARMUP_WAVES; w++) {
            for (size_t i = 0; i < wave_size; i++) {
                bc_concurrency_submit(ctx, crc_task, NULL);
            }
            bc_concurrency_dispatch_and_wait(ctx);
        }

        uint64_t start = now_ns();
        for (size_t w = 0; w < WAVE_COUNT; w++) {
            for (size_t i = 0; i < wave_size; i++) {
                bc_concurrency_submit(ctx, crc_task, NULL);
            }
            bc_concurrency_dispatch_and_wait(ctx);
        }
        uint64_t elapsed = now_ns() - start;

        size_t total_tasks = WAVE_COUNT * wave_size;
        double wave_us = (double)elapsed / (double)WAVE_COUNT / 1e3;
        double mtps = (double)total_tasks / ((double)elapsed / 1e9) / 1e6;

        printf("  %-12zu  %-18.2f  %.2f\n", wave_size, wave_us, mtps);
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
    return 0;
}
