// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "bc_allocators.h"
#include "bc_concurrency.h"

#define BC_CONCURRENCY_QUEUE_STRESS_PRODUCER_COUNT 8
#define BC_CONCURRENCY_QUEUE_STRESS_CONSUMER_COUNT 8
#define BC_CONCURRENCY_QUEUE_STRESS_ITEMS_PER_PRODUCER 12500
#define BC_CONCURRENCY_QUEUE_STRESS_QUEUE_CAPACITY 256

static void test_create_destroy_succeeds_with_valid_parameters(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), 16, &queue));
    assert_non_null(queue);

    size_t capacity = 0;
    bc_concurrency_queue_capacity(queue, &capacity);
    assert_int_equal(capacity, 16);

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

static void test_create_rounds_capacity_up_to_power_of_two(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), 10, &queue));

    size_t capacity = 0;
    bc_concurrency_queue_capacity(queue, &capacity);
    assert_int_equal(capacity, 16);

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

static void test_create_rejects_zero_element_size(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_false(bc_concurrency_queue_create(memory, 0, 16, &queue));
    assert_null(queue);
    bc_allocators_context_destroy(memory);
}

static void test_create_rejects_zero_capacity(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_false(bc_concurrency_queue_create(memory, sizeof(uint64_t), 0, &queue));
    assert_null(queue);
    bc_allocators_context_destroy(memory);
}

static void test_push_then_pop_preserves_fifo_order(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), 8, &queue));

    for (uint64_t value = 100; value < 108; ++value) {
        assert_true(bc_concurrency_queue_push(queue, &value));
    }
    for (uint64_t expected = 100; expected < 108; ++expected) {
        uint64_t actual = 0;
        assert_true(bc_concurrency_queue_pop(queue, &actual));
        assert_int_equal(actual, expected);
    }

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

static void test_push_returns_false_when_queue_is_full(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), 4, &queue));

    for (uint64_t value = 0; value < 4; ++value) {
        assert_true(bc_concurrency_queue_push(queue, &value));
    }
    uint64_t extra_value = 999;
    assert_false(bc_concurrency_queue_push(queue, &extra_value));

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

static void test_pop_returns_false_when_queue_is_empty(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), 8, &queue));

    uint64_t value = 0;
    assert_false(bc_concurrency_queue_pop(queue, &value));

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

static void test_close_rejects_subsequent_push_and_marks_closed_flag(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), 8, &queue));

    bool closed = true;
    bc_concurrency_queue_is_closed(queue, &closed);
    assert_false(closed);

    bc_concurrency_queue_close(queue);

    bc_concurrency_queue_is_closed(queue, &closed);
    assert_true(closed);

    uint64_t value = 42;
    assert_false(bc_concurrency_queue_push(queue, &value));

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

static void test_close_allows_pop_of_already_pushed_items(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), 8, &queue));

    for (uint64_t value = 1; value <= 3; ++value) {
        assert_true(bc_concurrency_queue_push(queue, &value));
    }
    bc_concurrency_queue_close(queue);

    for (uint64_t expected = 1; expected <= 3; ++expected) {
        uint64_t actual = 0;
        assert_true(bc_concurrency_queue_pop(queue, &actual));
        assert_int_equal(actual, expected);
    }
    uint64_t drained = 0;
    assert_false(bc_concurrency_queue_pop(queue, &drained));

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

typedef struct bc_concurrency_queue_stress_producer_arg {
    bc_concurrency_queue_t* queue;
    uint64_t producer_id;
    size_t item_count;
} bc_concurrency_queue_stress_producer_arg_t;

typedef struct bc_concurrency_queue_stress_consumer_arg {
    bc_concurrency_queue_t* queue;
    atomic_uint_fast64_t* total_sum;
    atomic_size_t* total_popped;
} bc_concurrency_queue_stress_consumer_arg_t;

static void* bc_concurrency_queue_stress_producer(void* raw_argument)
{
    bc_concurrency_queue_stress_producer_arg_t* argument = raw_argument;
    for (size_t item_index = 0; item_index < argument->item_count; ++item_index) {
        uint64_t value = (argument->producer_id << 32) | (uint64_t)item_index;
        while (!bc_concurrency_queue_push(argument->queue, &value)) {
            for (volatile int spin = 0; spin < 16; ++spin) {
            }
        }
    }
    return NULL;
}

static void* bc_concurrency_queue_stress_consumer(void* raw_argument)
{
    bc_concurrency_queue_stress_consumer_arg_t* argument = raw_argument;
    for (;;) {
        uint64_t value = 0;
        if (bc_concurrency_queue_pop(argument->queue, &value)) {
            atomic_fetch_add_explicit(argument->total_sum, (uint_fast64_t)value, memory_order_relaxed);
            atomic_fetch_add_explicit(argument->total_popped, 1, memory_order_relaxed);
            continue;
        }
        bool is_closed = false;
        bc_concurrency_queue_is_closed(argument->queue, &is_closed);
        if (is_closed) {
            if (!bc_concurrency_queue_pop(argument->queue, &value)) {
                return NULL;
            }
            atomic_fetch_add_explicit(argument->total_sum, (uint_fast64_t)value, memory_order_relaxed);
            atomic_fetch_add_explicit(argument->total_popped, 1, memory_order_relaxed);
            continue;
        }
        for (volatile int spin = 0; spin < 16; ++spin) {
        }
    }
}

static void test_stress_eight_producers_and_eight_consumers_preserve_all_items(void** state)
{
    (void)state;
    if (getenv("BC_TEST_SKIP_STRESS") != NULL) {
        skip();
        return;
    }
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory));
    bc_concurrency_queue_t* queue = NULL;
    assert_true(bc_concurrency_queue_create(memory, sizeof(uint64_t), BC_CONCURRENCY_QUEUE_STRESS_QUEUE_CAPACITY, &queue));

    atomic_uint_fast64_t total_sum = 0;
    atomic_size_t total_popped = 0;

    pthread_t producer_threads[BC_CONCURRENCY_QUEUE_STRESS_PRODUCER_COUNT];
    pthread_t consumer_threads[BC_CONCURRENCY_QUEUE_STRESS_CONSUMER_COUNT];
    bc_concurrency_queue_stress_producer_arg_t producer_arguments[BC_CONCURRENCY_QUEUE_STRESS_PRODUCER_COUNT];
    bc_concurrency_queue_stress_consumer_arg_t consumer_arguments[BC_CONCURRENCY_QUEUE_STRESS_CONSUMER_COUNT];

    for (size_t consumer_index = 0; consumer_index < BC_CONCURRENCY_QUEUE_STRESS_CONSUMER_COUNT; ++consumer_index) {
        consumer_arguments[consumer_index].queue = queue;
        consumer_arguments[consumer_index].total_sum = &total_sum;
        consumer_arguments[consumer_index].total_popped = &total_popped;
        assert_int_equal(pthread_create(&consumer_threads[consumer_index], NULL, bc_concurrency_queue_stress_consumer,
                                        &consumer_arguments[consumer_index]),
                         0);
    }
    for (size_t producer_index = 0; producer_index < BC_CONCURRENCY_QUEUE_STRESS_PRODUCER_COUNT; ++producer_index) {
        producer_arguments[producer_index].queue = queue;
        producer_arguments[producer_index].producer_id = (uint64_t)(producer_index + 1);
        producer_arguments[producer_index].item_count = BC_CONCURRENCY_QUEUE_STRESS_ITEMS_PER_PRODUCER;
        assert_int_equal(pthread_create(&producer_threads[producer_index], NULL, bc_concurrency_queue_stress_producer,
                                        &producer_arguments[producer_index]),
                         0);
    }

    for (size_t producer_index = 0; producer_index < BC_CONCURRENCY_QUEUE_STRESS_PRODUCER_COUNT; ++producer_index) {
        pthread_join(producer_threads[producer_index], NULL);
    }
    bc_concurrency_queue_close(queue);
    for (size_t consumer_index = 0; consumer_index < BC_CONCURRENCY_QUEUE_STRESS_CONSUMER_COUNT; ++consumer_index) {
        pthread_join(consumer_threads[consumer_index], NULL);
    }

    size_t expected_total = BC_CONCURRENCY_QUEUE_STRESS_PRODUCER_COUNT * BC_CONCURRENCY_QUEUE_STRESS_ITEMS_PER_PRODUCER;
    assert_int_equal(atomic_load_explicit(&total_popped, memory_order_relaxed), expected_total);

    uint_fast64_t expected_sum = 0;
    for (uint64_t producer_id = 1; producer_id <= BC_CONCURRENCY_QUEUE_STRESS_PRODUCER_COUNT; ++producer_id) {
        for (size_t item_index = 0; item_index < BC_CONCURRENCY_QUEUE_STRESS_ITEMS_PER_PRODUCER; ++item_index) {
            expected_sum += (producer_id << 32) | (uint64_t)item_index;
        }
    }
    assert_int_equal(atomic_load_explicit(&total_sum, memory_order_relaxed), expected_sum);

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_destroy_succeeds_with_valid_parameters),
        cmocka_unit_test(test_create_rounds_capacity_up_to_power_of_two),
        cmocka_unit_test(test_create_rejects_zero_element_size),
        cmocka_unit_test(test_create_rejects_zero_capacity),
        cmocka_unit_test(test_push_then_pop_preserves_fifo_order),
        cmocka_unit_test(test_push_returns_false_when_queue_is_full),
        cmocka_unit_test(test_pop_returns_false_when_queue_is_empty),
        cmocka_unit_test(test_close_rejects_subsequent_push_and_marks_closed_flag),
        cmocka_unit_test(test_close_allows_pop_of_already_pushed_items),
        cmocka_unit_test(test_stress_eight_producers_and_eight_consumers_preserve_all_items),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
