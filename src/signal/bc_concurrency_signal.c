// SPDX-License-Identifier: MIT

#include "bc_concurrency_signal.h"
#include "bc_core.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_concurrency_context_internal.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

static _Atomic(bc_concurrency_signal_handler_t*) global_signal_handler_pointer = NULL;

static void signal_callback_function(int signal_number)
{
    BC_UNUSED(signal_number);
    bc_concurrency_signal_handler_t* handler =
        atomic_load_explicit(&global_signal_handler_pointer, memory_order_relaxed);
    if (handler != NULL) {
        atomic_store_explicit(&handler->should_stop_flag, 1, memory_order_relaxed);
    }
}

bool bc_concurrency_signal_handler_create(bc_allocators_context_t* memory_context, bc_concurrency_signal_handler_t** out_signal_handler)
{
    *out_signal_handler = NULL;

    bc_concurrency_signal_handler_t* handler = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_concurrency_signal_handler_t), (void**)&handler)) {
        return false;
    }

    handler->memory_context = memory_context;
    atomic_store_explicit(&handler->should_stop_flag, 0, memory_order_relaxed);
    handler->installed_signal_count = 0;
    bc_core_zero(handler->installed_signal_numbers, sizeof(handler->installed_signal_numbers));
    bc_core_zero(handler->previous_signal_actions, sizeof(handler->previous_signal_actions));

    atomic_store_explicit(&global_signal_handler_pointer, handler, memory_order_release);

    *out_signal_handler = handler;
    return true;
}

void bc_concurrency_signal_handler_destroy(bc_concurrency_signal_handler_t* signal_handler)
{
    if (signal_handler == NULL) {
        return;
    }

    for (size_t i = 0; i < signal_handler->installed_signal_count; i++) {
        sigaction(signal_handler->installed_signal_numbers[i], &signal_handler->previous_signal_actions[i], NULL);
    }

    const bc_concurrency_signal_handler_t* current =
        atomic_load_explicit(&global_signal_handler_pointer, memory_order_acquire);
    if (current == signal_handler) {
        atomic_store_explicit(&global_signal_handler_pointer, NULL, memory_order_release);
    }

    bc_allocators_context_t* memory_context = signal_handler->memory_context;
    bc_allocators_pool_free(memory_context, signal_handler);
}

bool bc_concurrency_signal_handler_install(bc_concurrency_signal_handler_t* signal_handler, int signal_number)
{
    if (signal_handler->installed_signal_count >= BC_CONCURRENCY_SIGNAL_MAX_INSTALLED_SIGNALS) {
        return false;
    }

    struct sigaction new_action;
    bc_core_zero(&new_action, sizeof(new_action));
    new_action.sa_handler = signal_callback_function;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;

    size_t slot_index = signal_handler->installed_signal_count;

    if (sigaction(signal_number, &new_action, &signal_handler->previous_signal_actions[slot_index]) != 0) {
        return false;
    }

    signal_handler->installed_signal_numbers[slot_index] = signal_number;
    signal_handler->installed_signal_count++;

    return true;
}

bool bc_concurrency_signal_handler_should_stop(const bc_concurrency_signal_handler_t* signal_handler, bool* out_should_stop)
{
    const int flag = atomic_load_explicit(&signal_handler->should_stop_flag, memory_order_relaxed);
    *out_should_stop = (flag != 0);
    return true;
}

bool bc_concurrency_sleep_milliseconds(size_t duration_milliseconds, const bc_concurrency_signal_handler_t* signal_handler,
                                       bool* out_was_interrupted)
{
    *out_was_interrupted = false;

    if (duration_milliseconds == 0) {
        return true;
    }

    size_t duration_nanoseconds = 0;
    if (!bc_core_safe_multiply(duration_milliseconds, 1000000, &duration_nanoseconds)) {
        return false;
    }

    struct timespec remaining_time;
    remaining_time.tv_sec = (time_t)(duration_nanoseconds / 1000000000u);
    remaining_time.tv_nsec = (long)(duration_nanoseconds % 1000000000u);

    while (remaining_time.tv_sec > 0 || remaining_time.tv_nsec > 0) {
        struct timespec request_time = remaining_time;
        int sleep_result = nanosleep(&request_time, &remaining_time);

        if (sleep_result == 0) {
            break;
        }

        if (errno == EINTR) {
            if (signal_handler != NULL
                && atomic_load_explicit(&signal_handler->should_stop_flag, memory_order_relaxed) != 0) {
                *out_was_interrupted = true;
                return true;
            }

            continue;
        }

        return false;
    }

    return true;
}
