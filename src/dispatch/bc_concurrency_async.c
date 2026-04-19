// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"
#include "bc_core.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_concurrency_context_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void parallel_for_chunk_runner(void* chunk_argument)
{
    bc_concurrency_for_chunk_t* chunk = chunk_argument;

    bc_concurrency_iter_fn_t fn = chunk->iteration_function;
    void* arg = chunk->user_argument;
    size_t start = chunk->start_iteration_index;
    size_t count = chunk->iteration_count;
    size_t step = chunk->step_size;

    for (size_t i = 0; i < count; i++) {
        fn(start + i * step, arg);
    }
}

bool bc_concurrency_for(bc_concurrency_context_t* context, size_t start_index, size_t end_index, size_t step_size,
                        bc_concurrency_iter_fn_t iteration_function, void* user_argument)
{
    if (step_size == 0) {
        return false;
    }

    if (start_index >= end_index) {
        return true;
    }

    size_t total_iterations = 1 + (end_index - start_index - 1) / step_size;

    if (context->thread_count == 0 || total_iterations <= context->thread_count) {
        tls_worker = &context->workers[context->thread_count];
        for (size_t i = 0; i < total_iterations; i++) {
            iteration_function(start_index + i * step_size, user_argument);
        }
        tls_worker = NULL;
        return true;
    }

    if (!bc_concurrency_ensure_workers_spawned(context)) {
        return false;
    }

    size_t thread_count = context->thread_count;
    size_t num_chunks = thread_count + 1;
    size_t chunk_base = total_iterations / num_chunks;
    size_t chunk_remainder = total_iterations % num_chunks;

    size_t main_count = chunk_base + (0 < chunk_remainder ? 1 : 0);

    bc_allocators_context_t* memory = context->memory_context;

    size_t chunks_size = thread_count * sizeof(bc_concurrency_for_chunk_t);
    size_t entries_size = thread_count * sizeof(bc_concurrency_task_entry_t);
    size_t assignments_size = thread_count * sizeof(bc_concurrency_work_t);
    size_t total_alloc = chunks_size + entries_size + assignments_size;

    void* alloc = NULL;
    if (!bc_allocators_pool_allocate(memory, total_alloc, &alloc)) {
        return false;
    }

    bc_concurrency_for_chunk_t* chunks = alloc;
    bc_concurrency_task_entry_t* entries = (bc_concurrency_task_entry_t*)((uint8_t*)alloc + chunks_size);
    bc_concurrency_work_t* assignments = (bc_concurrency_work_t*)((uint8_t*)alloc + chunks_size + entries_size);

    size_t current_offset = main_count;

    for (size_t i = 0; i < thread_count; i++) {
        size_t chunk_idx = i + 1;
        size_t count = chunk_base + (chunk_idx < chunk_remainder ? 1 : 0);

        chunks[i].iteration_function = iteration_function;
        chunks[i].user_argument = user_argument;
        chunks[i].start_iteration_index = start_index + current_offset * step_size;
        chunks[i].iteration_count = count;
        chunks[i].step_size = step_size;
        current_offset += count;

        entries[i].task_function = parallel_for_chunk_runner;
        entries[i].task_argument = &chunks[i];

        assignments[i].tasks = &entries[i];
        assignments[i].count = 1;

        bc_concurrency_dispatch_to_worker(&context->workers[i], &assignments[i]);
    }

    tls_worker = &context->workers[context->thread_count];
    for (size_t i = 0; i < main_count; i++) {
        iteration_function(start_index + i * step_size, user_argument);
    }
    tls_worker = NULL;

    for (size_t i = 0; i < thread_count; i++) {
        bc_concurrency_wait_for_worker(&context->workers[i]);
    }

    bc_allocators_pool_free(memory, alloc);
    return true;
}
