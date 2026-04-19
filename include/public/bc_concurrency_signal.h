// SPDX-License-Identifier: MIT

#ifndef BC_CONCURRENCY_SIGNAL_H
#define BC_CONCURRENCY_SIGNAL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct bc_allocators_context bc_allocators_context_t;

/* ===== Constants ===== */

#define BC_CONCURRENCY_SIGNAL_MAX_INSTALLED_SIGNALS ((size_t)8)

/* ===== Signal handler ===== */

typedef struct bc_concurrency_signal_handler bc_concurrency_signal_handler_t;

bool bc_concurrency_signal_handler_create(bc_allocators_context_t* memory_context, bc_concurrency_signal_handler_t** out_signal_handler);

void bc_concurrency_signal_handler_destroy(bc_concurrency_signal_handler_t* signal_handler);

bool bc_concurrency_signal_handler_install(bc_concurrency_signal_handler_t* signal_handler, int signal_number);

bool bc_concurrency_signal_handler_should_stop(const bc_concurrency_signal_handler_t* signal_handler, bool* out_should_stop);

/* ===== Timer ===== */

bool bc_concurrency_sleep_milliseconds(size_t duration_milliseconds, const bc_concurrency_signal_handler_t* signal_handler,
                                       bool* out_was_interrupted);

#endif /* BC_CONCURRENCY_SIGNAL_H */
