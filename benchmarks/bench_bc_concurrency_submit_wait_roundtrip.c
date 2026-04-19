// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"
#include "bc_allocators.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define WARMUP_ITERATIONS ((size_t)100000)
#define MEASURE_ITERATIONS ((size_t)1000000)

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
    printf("bench_submit_wait_roundtrip: submit(1 noop) + dispatch_and_wait x%zu\n\n", MEASURE_ITERATIONS);

    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    size_t bg = bc_concurrency_thread_count(ctx);
    size_t eff = bc_concurrency_effective_worker_count(ctx);

    for (size_t i = 0; i < WARMUP_ITERATIONS; i++) {
        bc_concurrency_submit(ctx, noop_task, NULL);
        bc_concurrency_dispatch_and_wait(ctx);
    }

    uint64_t start = now_ns();
    for (size_t i = 0; i < MEASURE_ITERATIONS; i++) {
        bc_concurrency_submit(ctx, noop_task, NULL);
        bc_concurrency_dispatch_and_wait(ctx);
    }
    uint64_t elapsed = now_ns() - start;

    uint64_t ns_per_cycle = elapsed / MEASURE_ITERATIONS;
    double cycles_per_sec = (double)MEASURE_ITERATIONS / ((double)elapsed / 1e9);

    printf("  bg-threads=%-3zu  effective=%-3zu  %6lu ns/cycle  %10.0f cycles/sec\n", bg, eff, (unsigned long)ns_per_cycle, cycles_per_sec);

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
    return 0;
}
