// SPDX-License-Identifier: MIT

#ifndef BC_CONCURRENCY_H
#define BC_CONCURRENCY_H

#include <stdbool.h>
#include <stddef.h>

/* ===== ThreadSanitizer happens-before annotations ===== */

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define BC_CONCURRENCY_TSAN_ENABLED 1
#endif
#endif
#if !defined(BC_CONCURRENCY_TSAN_ENABLED) && defined(__SANITIZE_THREAD__)
#define BC_CONCURRENCY_TSAN_ENABLED 1
#endif

#if defined(BC_CONCURRENCY_TSAN_ENABLED) && BC_CONCURRENCY_TSAN_ENABLED
extern void AnnotateHappensBefore(const char* file, int line, const volatile void* cv);
extern void AnnotateHappensAfter(const char* file, int line, const volatile void* cv);
#define BC_CONCURRENCY_TSAN_RELEASE(ptr) AnnotateHappensBefore(__FILE__, __LINE__, (const volatile void*)(ptr))
#define BC_CONCURRENCY_TSAN_ACQUIRE(ptr) AnnotateHappensAfter(__FILE__, __LINE__, (const volatile void*)(ptr))
#else
#define BC_CONCURRENCY_TSAN_RELEASE(ptr) ((void)(ptr))
#define BC_CONCURRENCY_TSAN_ACQUIRE(ptr) ((void)(ptr))
#endif

/* ===== Forward declarations ===== */

typedef struct bc_allocators_context bc_allocators_context_t;

/* ===== Configuration ===== */

typedef struct bc_concurrency_config {
    /* Main thread pinned to physical core core_offset.
     * Background worker i pinned to physical core (core_offset + 1 + i).
     * By default (worker_count_explicit == false) the number of background
     * worker threads is physical_core_count - 1: one thread per physical
     * core, no SMT sharing. Set worker_count_explicit = true and
     * worker_count = N to override with exactly N background worker threads
     * (0 <= N and core_offset + N < physical_core_count). */
    size_t core_offset;
    size_t worker_stack_size; /* 0 = default 1MB. Clamped to PTHREAD_STACK_MIN. */
    bool worker_count_explicit;
    size_t worker_count;
} bc_concurrency_config_t;

/* ===== Per-worker slot configuration ===== */

typedef struct bc_concurrency_slot_config {
    size_t size;
    void (*init)(void* data, size_t worker_index, void* arg);
    void (*destroy)(void* data, size_t worker_index, void* arg);
    void* arg;
} bc_concurrency_slot_config_t;

/* ===== Opaque context ===== */

typedef struct bc_concurrency_context bc_concurrency_context_t;

/* ===== Function types ===== */

typedef void (*bc_concurrency_task_fn_t)(void* argument);
typedef void (*bc_concurrency_iter_fn_t)(size_t index, void* argument);

/* ===== Lifecycle ===== */

bool bc_concurrency_create(bc_allocators_context_t* memory_context, const bc_concurrency_config_t* config,
                           bc_concurrency_context_t** out_context);

void bc_concurrency_destroy(bc_concurrency_context_t* context);

size_t bc_concurrency_thread_count(const bc_concurrency_context_t* context);

/* Number of processors that can execute tasks: background threads + main thread. */
size_t bc_concurrency_effective_worker_count(const bc_concurrency_context_t* context);

/* ===== Per-worker resource slots =====
 *
 * Register extensible per-worker data. Must be called BEFORE the first
 * dispatch_and_wait or parallel_for. Each worker gets its own private instance. */

bool bc_concurrency_register_slot(bc_concurrency_context_t* context, const bc_concurrency_slot_config_t* config, size_t* out_slot_index);

void bc_concurrency_foreach_slot(bc_concurrency_context_t* context, size_t slot_index,
                                 void (*callback)(void* data, size_t worker_index, void* arg), void* arg);

/* ===== Task submission (main thread, accumulates in staging) ===== */

bool bc_concurrency_submit(bc_concurrency_context_t* context, bc_concurrency_task_fn_t task_function, void* task_argument);

bool bc_concurrency_submit_batch(bc_concurrency_context_t* context, bc_concurrency_task_fn_t task_function, void* const* task_arguments,
                                 size_t task_count);

/* Dispatch all staged tasks to workers and block until completion. */
bool bc_concurrency_dispatch_and_wait(bc_concurrency_context_t* context);

/* ===== Parallel iteration (main thread participates as worker 0) ===== */

bool bc_concurrency_for(bc_concurrency_context_t* context, size_t start_index, size_t end_index, size_t step_size,
                        bc_concurrency_iter_fn_t iteration_function, void* user_argument);

/* ===== Worker resources (TLS, called from within a task function) ===== */

bc_allocators_context_t* bc_concurrency_worker_memory(void);
size_t bc_concurrency_worker_index(void);
void* bc_concurrency_worker_slot(size_t slot_index);

/* ===== MPMC bounded queue (Vyukov) ===== */

typedef struct bc_concurrency_queue bc_concurrency_queue_t;

bool bc_concurrency_queue_create(bc_allocators_context_t* memory_context, size_t element_size, size_t capacity,
                                 bc_concurrency_queue_t** out_queue);

void bc_concurrency_queue_destroy(bc_concurrency_queue_t* queue);

bool bc_concurrency_queue_push(bc_concurrency_queue_t* queue, const void* item);

bool bc_concurrency_queue_pop(bc_concurrency_queue_t* queue, void* out_item);

void bc_concurrency_queue_close(bc_concurrency_queue_t* queue);

void bc_concurrency_queue_is_closed(const bc_concurrency_queue_t* queue, bool* out_is_closed);

void bc_concurrency_queue_capacity(const bc_concurrency_queue_t* queue, size_t* out_capacity);

#endif /* BC_CONCURRENCY_H */
