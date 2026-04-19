// SPDX-License-Identifier: MIT

#include "bc_concurrency_platform_internal.h"

#include <pthread.h>
#include <sched.h>

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define BC_CONCURRENCY_MAX_CPUS 1024

size_t bc_concurrency_platform_get_cpu_count(void)
{
    long detected_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    /* GCOVR_EXCL_START -- platform-defensive: sysconf always returns >= 1 on standard Linux */
    if (detected_cpu_count < 1) {
        return 1;
    }
    /* GCOVR_EXCL_STOP */
    return (size_t)detected_cpu_count;
}

size_t bc_concurrency_platform_get_physical_core_count(void)
{
    bool seen_core_ids[BC_CONCURRENCY_MAX_CPUS] = {false};
    size_t physical_core_count = 0;
    char path_buffer[128];

    for (size_t cpu_index = 0; cpu_index < BC_CONCURRENCY_MAX_CPUS; cpu_index++) { /* GCOVR_EXCL_BR_LINE */
        int written = snprintf(path_buffer, sizeof(path_buffer), "/sys/devices/system/cpu/cpu%zu/topology/core_id", cpu_index);
        /* GCOVR_EXCL_START -- platform-defensive: path buffer always fits for cpu index < 1024 */
        if (written < 0 || (size_t)written >= sizeof(path_buffer)) {
            break;
        }
        /* GCOVR_EXCL_STOP */

        FILE* topology_file = fopen(path_buffer, "r");
        if (topology_file == NULL) {
            break;
        }

        long core_id = -1;
        /* GCOVR_EXCL_START -- platform-defensive: topology files always contain valid core_id on standard Linux */
        if (fscanf(topology_file, "%ld", &core_id) == 1 && core_id >= 0 && (size_t)core_id < BC_CONCURRENCY_MAX_CPUS) {
            if (!seen_core_ids[core_id]) {
                seen_core_ids[core_id] = true;
                physical_core_count++;
            }
        }
        /* GCOVR_EXCL_STOP */
        fclose(topology_file);
    }

    /* GCOVR_EXCL_START -- platform-defensive: fallback when topology sysfs is unavailable */
    if (physical_core_count == 0) {
        return bc_concurrency_platform_get_cpu_count();
    }
    /* GCOVR_EXCL_STOP */

    return physical_core_count;
}

bool bc_concurrency_platform_pin_current_thread_to_physical_core(size_t physical_core_index)
{
    bool seen_core_ids[BC_CONCURRENCY_MAX_CPUS] = {false};
    size_t physical_cores_found = 0;
    char path_buffer[128];

    for (size_t cpu_index = 0; cpu_index < BC_CONCURRENCY_MAX_CPUS; cpu_index++) { /* GCOVR_EXCL_BR_LINE */
        int written = snprintf(path_buffer, sizeof(path_buffer), "/sys/devices/system/cpu/cpu%zu/topology/core_id", cpu_index);
        /* GCOVR_EXCL_START -- platform-defensive: path buffer always fits for cpu index < 1024 */
        if (written < 0 || (size_t)written >= sizeof(path_buffer)) {
            break;
        }
        /* GCOVR_EXCL_STOP */

        FILE* topology_file = fopen(path_buffer, "r");
        if (topology_file == NULL) {
            break;
        }

        long core_id = -1;
        /* GCOVR_EXCL_START -- platform-defensive: topology files always contain valid core_id on standard Linux */
        if (fscanf(topology_file, "%ld", &core_id) != 1 || core_id < 0 || (size_t)core_id >= BC_CONCURRENCY_MAX_CPUS) {
            fclose(topology_file);
            continue;
        }
        /* GCOVR_EXCL_STOP */
        fclose(topology_file);

        if (!seen_core_ids[core_id]) {
            seen_core_ids[core_id] = true;
            if (physical_cores_found == physical_core_index) {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_index, &cpuset); /* GCOVR_EXCL_BR_LINE -- platform-defensive: CPU_SET macro generates unreachable branch */
                /* GCOVR_EXCL_START -- platform-defensive: pthread_setaffinity_np failure */
                return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
                /* GCOVR_EXCL_STOP */
            }
            physical_cores_found++;
        }
    }

    return false;
}

void bc_concurrency_platform_cpu_relax(void)
{
    __asm__ volatile("pause" ::: "memory");
}
