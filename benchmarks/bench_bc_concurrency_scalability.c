// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TOTAL_TASKS ((size_t)2000000)
#define WARMUP_TASKS ((size_t)200000)

static void noop_task(void* arg)
{
    (void)arg;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    printf("bench_scalability: %zu noop tasks, hardware-determined thread count\n", TOTAL_TASKS);
    printf("  one physical core per worker — no SMT sharing\n\n");

    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    size_t bg = bc_concurrency_thread_count(ctx);
    size_t eff = bc_concurrency_effective_worker_count(ctx);

    for (size_t i = 0; i < WARMUP_TASKS; i++) {
        bc_concurrency_submit(ctx, noop_task, NULL);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    uint64_t start = now_ns();
    for (size_t i = 0; i < TOTAL_TASKS; i++) {
        bc_concurrency_submit(ctx, noop_task, NULL);
    }
    bc_concurrency_dispatch_and_wait(ctx);
    uint64_t elapsed = now_ns() - start;

    double tps = (double)TOTAL_TASKS / ((double)elapsed / 1e9);

    printf("  bg-threads=%-3zu  effective=%-3zu  %.2f Mtasks/sec\n", bg, eff, tps / 1e6);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
    return 0;
}
