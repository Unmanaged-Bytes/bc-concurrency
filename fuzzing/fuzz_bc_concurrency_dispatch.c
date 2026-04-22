// SPDX-License-Identifier: MIT

#include "bc_concurrency.h"

#include "bc_allocators.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t value;
} fuzz_task_data_t;

static void noop_task(void* argument)
{
    fuzz_task_data_t* data = (fuzz_task_data_t*)argument;
    data->value += 1;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2) {
        return 0;
    }

    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(NULL, &memory)) {
        return 0;
    }

    bc_concurrency_config_t config = {
        .core_offset = 0,
        .worker_stack_size = 0,
    };

    bc_concurrency_context_t* ctx = NULL;
    if (!bc_concurrency_create(memory, &config, &ctx)) {
        bc_allocators_context_destroy(memory);
        return 0;
    }

    size_t task_count = (size_t)(data[1]) + 1;
    if (task_count > 64) {
        task_count = 64;
    }

    fuzz_task_data_t* task_data = calloc(task_count, sizeof(*task_data));
    if (task_data != NULL) {
        for (size_t i = 0; i < task_count; i++) {
            bc_concurrency_submit(ctx, noop_task, &task_data[i]);
        }
        bc_concurrency_dispatch_and_wait(ctx);
        free(task_data);
    }

    bc_concurrency_destroy(ctx);
    bc_allocators_context_destroy(memory);
    return 0;
}

#ifndef BC_FUZZ_LIBFUZZER
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 2;
    }
    unsigned long iterations = strtoul(argv[1], NULL, 10);
    unsigned long seed = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 0;
    srand((unsigned int)seed);

    uint8_t buffer[256];
    for (unsigned long i = 0; i < iterations; i++) {
        size_t len = (size_t)(rand() % (int)sizeof(buffer));
        for (size_t j = 0; j < len; j++) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }
        LLVMFuzzerTestOneInput(buffer, len);
    }
    return 0;
}
#endif
