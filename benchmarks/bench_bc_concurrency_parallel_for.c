// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define ITERATION_COUNT ((size_t)10000000)
#define WARMUP_COUNT ((size_t)1000000)

static void work_local(size_t index, void* arg)
{
    (void)arg;
    volatile size_t x = index * 6364136223846793005ULL;
    (void)x;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    printf("bench_parallel_for: %zu iterations (local volatile multiply)\n", ITERATION_COUNT);
    printf("  one physical core per effective worker — no SMT sharing\n\n");

    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    size_t bg = bc_concurrency_thread_count(ctx);
    size_t eff = bc_concurrency_effective_worker_count(ctx);

    bc_concurrency_for(ctx, 0, WARMUP_COUNT, 1, work_local, NULL);

    uint64_t start = now_ns();
    bc_concurrency_for(ctx, 0, ITERATION_COUNT, 1, work_local, NULL);
    uint64_t elapsed = now_ns() - start;

    double tps = (double)ITERATION_COUNT / ((double)elapsed / 1e9);

    printf("  bg-threads=%-3zu  effective=%-3zu  %.1f ms  %.0f Miters/sec\n", bg, eff, (double)elapsed / 1e6, tps / 1e6);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
    return 0;
}
