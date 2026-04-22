// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_concurrency.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2) {
        return 0;
    }

    bc_allocators_context_t* memory_context = NULL;
    if (!bc_allocators_context_create(NULL, &memory_context)) {
        return 0;
    }

    const size_t capacity_raw = (size_t)data[0];
    size_t capacity = (capacity_raw % 63) + 2;

    bc_concurrency_queue_t* queue = NULL;
    if (!bc_concurrency_queue_create(memory_context, sizeof(uint32_t), capacity, &queue)) {
        bc_allocators_context_destroy(memory_context);
        return 0;
    }

    size_t position = 1;
    while (position + 5 <= size) {
        const uint8_t operation = data[position++] & 0x7;
        const uint32_t element = (uint32_t)data[position] | ((uint32_t)data[position + 1] << 8) | ((uint32_t)data[position + 2] << 16)
                                 | ((uint32_t)data[position + 3] << 24);
        position += 4;

        switch (operation) {
            case 0:
                (void)bc_concurrency_queue_push(queue, &element);
                break;
            case 1: {
                uint32_t out = 0;
                (void)bc_concurrency_queue_pop(queue, &out);
                break;
            }
            case 2:
                bc_concurrency_queue_close(queue);
                break;
            case 3: {
                bool closed = false;
                bc_concurrency_queue_is_closed(queue, &closed);
                size_t queue_capacity = 0;
                bc_concurrency_queue_capacity(queue, &queue_capacity);
                break;
            }
            default: {
                (void)bc_concurrency_queue_push(queue, &element);
                uint32_t out = 0;
                (void)bc_concurrency_queue_pop(queue, &out);
                break;
            }
        }
    }

    bc_concurrency_queue_destroy(queue);
    bc_allocators_context_destroy(memory_context);
    return 0;
}

#ifndef BC_FUZZ_LIBFUZZER
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 2;
    }
    const unsigned long iterations = strtoul(argv[1], NULL, 10);
    const unsigned long seed = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 0;
    srand((unsigned int)seed);

    uint8_t buffer[2048];
    for (unsigned long i = 0; i < iterations; i++) {
        const size_t length = (size_t)(rand() % (int)sizeof(buffer));
        for (size_t j = 0; j < length; j++) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }
        LLVMFuzzerTestOneInput(buffer, length);
    }
    return 0;
}
#endif
