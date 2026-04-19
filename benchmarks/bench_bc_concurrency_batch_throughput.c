// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TOTAL_TASKS ((size_t)10000000)
#define BATCH_SIZE ((size_t)10000)
#define WARMUP_TASKS ((size_t)100000)

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
    printf("bench_batch_throughput: %zu noop tasks in batches of %zu, one wait_all\n\n", TOTAL_TASKS, BATCH_SIZE);

    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);

    bc_concurrency_context_t* ctx = NULL;
    bc_concurrency_create(mem, NULL, &ctx);

    size_t thread_count = 0;
    thread_count = bc_concurrency_thread_count(ctx);

    void** null_args = NULL;
    bc_allocators_pool_allocate(mem, BATCH_SIZE * sizeof(void*), (void**)&null_args);
    for (size_t i = 0; i < BATCH_SIZE; i++) {
        null_args[i] = NULL;
    }

    for (size_t submitted = 0; submitted < WARMUP_TASKS; submitted += BATCH_SIZE) {
        bc_concurrency_submit_batch(ctx, noop_task, (void* const*)null_args, BATCH_SIZE);
    }
    bc_concurrency_dispatch_and_wait(ctx);

    uint64_t start = now_ns();
    for (size_t submitted = 0; submitted < TOTAL_TASKS; submitted += BATCH_SIZE) {
        bc_concurrency_submit_batch(ctx, noop_task, (void* const*)null_args, BATCH_SIZE);
    }
    bc_concurrency_dispatch_and_wait(ctx);
    uint64_t elapsed = now_ns() - start;

    double tasks_per_sec = (double)TOTAL_TASKS / ((double)elapsed / 1e9);
    double ns_per_task = (double)elapsed / (double)TOTAL_TASKS;

    printf("  threads=%zu  total=%zu  elapsed=%.3f s\n", thread_count, TOTAL_TASKS, (double)elapsed / 1e9);
    printf("  %.1f ns/task  %.0f Mtasks/sec\n", ns_per_task, tasks_per_sec / 1e6);

    bc_allocators_pool_free(mem, null_args);
    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(mem);
    return 0;
}
