/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Portable C99 safety helpers. These functions do not assume alignment or
 * compiler-specific overflow intrinsics.
 */
#include "safe.h"
#include <stdlib.h>
#include <string.h>

/* Performs checked size addition. */
int cavs_size_add(size_t left, size_t right, size_t *result) {
    if (result == NULL || right > SIZE_MAX - left) return 0;
    *result = left + right;
    return 1;
}

/* Performs checked size multiplication, including the zero-size case. */
int cavs_size_mul(size_t left, size_t right, size_t *result) {
    if (result == NULL || (left != 0U && right > SIZE_MAX / left)) return 0;
    *result = left * right;
    return 1;
}

/* Allocates and clears an array after validating its total byte size. */
void *cavs_alloc_array(const cavs_decoder_config *config, size_t count, size_t element_size) {
    size_t size;
    void *memory;
    if (config == NULL || config->alloc == NULL ||
        !cavs_size_mul(count, element_size, &size)) return NULL;
    memory = config->alloc(config->allocator_opaque, size == 0U ? 1U : size);
    if (memory != NULL && size != 0U) memset(memory, 0, size);
    return memory;
}

/* Reconstructs a big-endian 16-bit value one byte at a time. */
uint16_t cavs_load_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

/* Reconstructs a big-endian 32-bit value one byte at a time. */
uint32_t cavs_load_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}
