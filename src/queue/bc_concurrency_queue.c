// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"

#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BC_CONCURRENCY_QUEUE_CACHELINE_BYTES ((size_t)64)

typedef struct bc_concurrency_queue {
    alignas(BC_CONCURRENCY_QUEUE_CACHELINE_BYTES) _Atomic uint64_t enqueue_position;
    alignas(BC_CONCURRENCY_QUEUE_CACHELINE_BYTES) _Atomic uint64_t dequeue_position;
    alignas(BC_CONCURRENCY_QUEUE_CACHELINE_BYTES) _Atomic bool closed;
    size_t capacity;
    size_t capacity_mask;
    size_t element_size;
    size_t slot_stride_bytes;
    uint8_t* slot_buffer;
    bc_allocators_context_t* memory_context;
} bc_concurrency_queue_t;

static bool bc_concurrency_queue_is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static size_t bc_concurrency_queue_round_up_power_of_two(size_t value)
{
    if (value <= 1) {
        return 1;
    }
    size_t power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

static size_t bc_concurrency_queue_align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static _Atomic uint64_t* bc_concurrency_queue_slot_sequence(const bc_concurrency_queue_t* queue, uint64_t position)
{
    uint8_t* slot_ptr = queue->slot_buffer + (position & queue->capacity_mask) * queue->slot_stride_bytes;
    return (_Atomic uint64_t*)slot_ptr;
}

static void* bc_concurrency_queue_slot_data(const bc_concurrency_queue_t* queue, uint64_t position)
{
    uint8_t* slot_ptr = queue->slot_buffer + (position & queue->capacity_mask) * queue->slot_stride_bytes;
    return slot_ptr + sizeof(_Atomic uint64_t);
}

bool bc_concurrency_queue_create(bc_allocators_context_t* memory_context, size_t element_size, size_t capacity,
                                 bc_concurrency_queue_t** out_queue)
{
    *out_queue = NULL;
    if (element_size == 0 || capacity == 0) {
        return false;
    }
    size_t effective_capacity = bc_concurrency_queue_is_power_of_two(capacity) ? capacity
                                                                               : bc_concurrency_queue_round_up_power_of_two(capacity);

    bc_concurrency_queue_t* queue = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_concurrency_queue_t), (void**)&queue)) {
        return false;
    }
    bc_core_zero(queue, sizeof(bc_concurrency_queue_t));

    size_t slot_stride_bytes = bc_concurrency_queue_align_up(sizeof(_Atomic uint64_t) + element_size, BC_CONCURRENCY_QUEUE_CACHELINE_BYTES);
    size_t slot_buffer_bytes = slot_stride_bytes * effective_capacity;

    if (!bc_allocators_pool_allocate(memory_context, slot_buffer_bytes, (void**)&queue->slot_buffer)) {
        bc_allocators_pool_free(memory_context, queue);
        return false;
    }
    bc_core_zero(queue->slot_buffer, slot_buffer_bytes);

    queue->memory_context = memory_context;
    queue->capacity = effective_capacity;
    queue->capacity_mask = effective_capacity - 1;
    queue->element_size = element_size;
    queue->slot_stride_bytes = slot_stride_bytes;
    atomic_store_explicit(&queue->enqueue_position, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->dequeue_position, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->closed, false, memory_order_relaxed);

    for (size_t slot_index = 0; slot_index < effective_capacity; ++slot_index) {
        _Atomic uint64_t* sequence_ptr = bc_concurrency_queue_slot_sequence(queue, (uint64_t)slot_index);
        atomic_store_explicit(sequence_ptr, (uint64_t)slot_index, memory_order_relaxed);
    }

    *out_queue = queue;
    return true;
}

void bc_concurrency_queue_destroy(bc_concurrency_queue_t* queue)
{
    bc_allocators_context_t* memory_context = queue->memory_context;
    bc_allocators_pool_free(memory_context, queue->slot_buffer);
    bc_allocators_pool_free(memory_context, queue);
}

bool bc_concurrency_queue_push(bc_concurrency_queue_t* queue, const void* item)
{
    if (atomic_load_explicit(&queue->closed, memory_order_acquire)) {
        return false;
    }
    uint64_t position = atomic_load_explicit(&queue->enqueue_position, memory_order_relaxed);
    for (;;) {
        _Atomic uint64_t* sequence_ptr = bc_concurrency_queue_slot_sequence(queue, position);
        uint64_t sequence = atomic_load_explicit(sequence_ptr, memory_order_acquire);
        int64_t difference = (int64_t)sequence - (int64_t)position;
        if (difference == 0) {
            if (atomic_compare_exchange_weak_explicit(&queue->enqueue_position, &position, position + 1, memory_order_relaxed,
                                                     memory_order_relaxed)) {
                void* slot_data = bc_concurrency_queue_slot_data(queue, position);
                (void)bc_core_copy(slot_data, item, queue->element_size);
                atomic_store_explicit(sequence_ptr, position + 1, memory_order_release);
                return true;
            }
        } else if (difference < 0) {
            return false;
        } else {
            position = atomic_load_explicit(&queue->enqueue_position, memory_order_relaxed);
        }
    }
}

bool bc_concurrency_queue_pop(bc_concurrency_queue_t* queue, void* out_item)
{
    uint64_t position = atomic_load_explicit(&queue->dequeue_position, memory_order_relaxed);
    for (;;) {
        _Atomic uint64_t* sequence_ptr = bc_concurrency_queue_slot_sequence(queue, position);
        uint64_t sequence = atomic_load_explicit(sequence_ptr, memory_order_acquire);
        int64_t difference = (int64_t)sequence - (int64_t)(position + 1);
        if (difference == 0) {
            if (atomic_compare_exchange_weak_explicit(&queue->dequeue_position, &position, position + 1, memory_order_relaxed,
                                                     memory_order_relaxed)) {
                const void* slot_data = bc_concurrency_queue_slot_data(queue, position);
                (void)bc_core_copy(out_item, slot_data, queue->element_size);
                atomic_store_explicit(sequence_ptr, position + queue->capacity, memory_order_release);
                return true;
            }
        } else if (difference < 0) {
            return false;
        } else {
            position = atomic_load_explicit(&queue->dequeue_position, memory_order_relaxed);
        }
    }
}

void bc_concurrency_queue_close(bc_concurrency_queue_t* queue)
{
    atomic_store_explicit(&queue->closed, true, memory_order_release);
}

void bc_concurrency_queue_is_closed(const bc_concurrency_queue_t* queue, bool* out_is_closed)
{
    *out_is_closed = atomic_load_explicit(&queue->closed, memory_order_acquire);
}

void bc_concurrency_queue_capacity(const bc_concurrency_queue_t* queue, size_t* out_capacity)
{
    *out_capacity = queue->capacity;
}
