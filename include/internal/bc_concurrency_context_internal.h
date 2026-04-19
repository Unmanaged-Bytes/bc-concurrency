// SPDX-License-Identifier: MIT

#ifndef BC_CONCURRENCY_CONTEXT_INTERNAL_H
#define BC_CONCURRENCY_CONTEXT_INTERNAL_H

#include "bc_core.h"
#include "bc_concurrency.h"
#include "bc_concurrency_signal.h"
#include "bc_allocators.h"
#include "bc_allocators_typed_array.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* ===== Task entry ===== */

typedef struct bc_concurrency_task_entry {
    bc_concurrency_task_fn_t task_function;
    void* task_argument;
} bc_concurrency_task_entry_t;

BC_TYPED_ARRAY_DEFINE(bc_concurrency_task_entry_t, bc_concurrency_staging)

/* ===== Work assignment (main sets before wake, worker reads after wake) ===== */

typedef struct bc_concurrency_work {
    bc_concurrency_task_entry_t* tasks;
    size_t count;
} bc_concurrency_work_t;

/* ===== Worker state (no atomics, barriers via eventfd) ===== */

typedef struct bc_concurrency_worker {
    bc_concurrency_context_t* context;
    bc_allocators_context_t* memory;
    size_t index;

    bc_concurrency_work_t* work;
    bool should_stop;

    pthread_mutex_t state_mutex;
    pthread_cond_t ready_cond;
    pthread_cond_t done_cond;
    bool ready_flag;
    bool done_flag;

    void** slots;
} BC_CACHE_LINE_ALIGNED bc_concurrency_worker_t;

/* ===== Context ===== */

struct bc_concurrency_context {
    bc_allocators_context_t* memory_context;

    pthread_t* threads;
    bc_concurrency_worker_t* workers;
    size_t thread_count;
    size_t core_offset;
    size_t worker_stack_size;
    bool workers_spawned;

    bc_concurrency_staging_t staging;

    bc_concurrency_task_entry_t* dispatch_tasks;
    bc_concurrency_work_t* dispatch_work;
    size_t dispatch_capacity;

    bc_concurrency_slot_config_t* slot_configs;
    size_t slot_count;
    size_t slot_capacity;
};

/* ===== Parallel for chunk ===== */

typedef struct bc_concurrency_for_chunk {
    bc_concurrency_iter_fn_t iteration_function;
    void* user_argument;
    size_t start_iteration_index;
    size_t iteration_count;
    size_t step_size;
} bc_concurrency_for_chunk_t;

/* ===== Signal handler ===== */

struct bc_concurrency_signal_handler {
    bc_allocators_context_t* memory_context;
    volatile sig_atomic_t should_stop_flag;
    int installed_signal_numbers[BC_CONCURRENCY_SIGNAL_MAX_INSTALLED_SIGNALS];
    size_t installed_signal_count;
    struct sigaction previous_signal_actions[BC_CONCURRENCY_SIGNAL_MAX_INSTALLED_SIGNALS];
};

/* ===== Per-worker TLS (defined in bc_concurrency.c, shared with bc_concurrency_async.c) ===== */

extern _Thread_local bc_concurrency_worker_t* tls_worker;

/* ===== Internal helpers ===== */

bool bc_concurrency_ensure_workers_spawned(bc_concurrency_context_t* context);
void bc_concurrency_dispatch_to_worker(bc_concurrency_worker_t* worker, bc_concurrency_work_t* work);
void bc_concurrency_wait_for_worker(bc_concurrency_worker_t* worker);

#endif /* BC_CONCURRENCY_CONTEXT_INTERNAL_H */
