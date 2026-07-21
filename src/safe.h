/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Integer, allocation, and byte-access helpers used by untrusted input paths.
 */
#ifndef CAVS_SAFE_H
#define CAVS_SAFE_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

/* Adds two sizes, returning zero if the result cannot be represented. */
int cavs_size_add(size_t left, size_t right, size_t *result);

/* Multiplies two sizes, returning zero if the result cannot be represented. */
int cavs_size_mul(size_t left, size_t right, size_t *result);

/* Allocates a zeroed array through the configured allocator. */
void *cavs_alloc_array(const cavs_decoder_config *config, size_t count, size_t element_size);

/* Loads an unaligned 16-bit big-endian integer without type-punning. */
uint16_t cavs_load_be16(const uint8_t *data);

/* Loads an unaligned 32-bit big-endian integer without type-punning. */
uint32_t cavs_load_be32(const uint8_t *data);

#endif
