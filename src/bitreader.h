/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bounds-checked bit access and exponential-Golomb parsing.
 */
#ifndef CAVS_BITREADER_H
#define CAVS_BITREADER_H
#include <stddef.h>
#include <stdint.h>
typedef struct cavs_bitreader {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_pos;
} cavs_bitreader;

/* Initializes a reader over complete bytes. */
void cavs_br_init(cavs_bitreader *br, const uint8_t *data, size_t size);

/* Initializes a reader with an exact valid-bit count. */
int cavs_br_init_bits(cavs_bitreader *br, const uint8_t *data, size_t bit_size);

/* Returns the number of unread bits. */
size_t cavs_br_bits_left(const cavs_bitreader *br);

/* Reads up to 32 bits in most-significant-bit-first order. */
int cavs_br_read(cavs_bitreader *br, unsigned count, uint32_t *value);

/* Reads an unsigned order-k exponential-Golomb value. */
int cavs_br_read_ue_k(cavs_bitreader *br, unsigned order, uint32_t *value);

/* Reads an unsigned order-0 exponential-Golomb value. */
int cavs_br_read_ue(cavs_bitreader *br, uint32_t *value);

/* Reads a signed order-0 exponential-Golomb value using Table 43. */
int cavs_br_read_se(cavs_bitreader *br, int32_t *value);
#endif
