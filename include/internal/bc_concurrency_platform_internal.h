// SPDX-License-Identifier: MIT

#ifndef BC_CONCURRENCY_PLATFORM_INTERNAL_H
#define BC_CONCURRENCY_PLATFORM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

size_t bc_concurrency_platform_get_cpu_count(void);
size_t bc_concurrency_platform_get_physical_core_count(void);

bool bc_concurrency_platform_pin_current_thread_to_physical_core(size_t physical_core_index);

void bc_concurrency_platform_cpu_relax(void);

#endif /* BC_CONCURRENCY_PLATFORM_INTERNAL_H */
