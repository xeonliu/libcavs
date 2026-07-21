/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Basic-entropy transform coefficient decoding.
 */
#ifndef CAVS_COEFFICIENTS_H
#define CAVS_COEFFICIENTS_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_COEFFICIENT_COUNT_8X8 64U

typedef enum cavs_basic_block_kind {
    CAVS_BASIC_INTRA_LUMA = 0,
    CAVS_BASIC_INTER_LUMA = 1,
    CAVS_BASIC_CHROMA = 2
} cavs_basic_block_kind;

typedef struct cavs_basic_coefficients {
    int32_t level[CAVS_COEFFICIENT_COUNT_8X8];
    uint8_t run[CAVS_COEFFICIENT_COUNT_8X8];
    int32_t scan_coefficients[CAVS_COEFFICIENT_COUNT_8X8];
    uint8_t count;
    size_t end_bit_offset;
} cavs_basic_coefficients;

/* Parses one complete 8x8 basic-entropy block, including its EOB symbol. */
cavs_result cavs_decode_basic_coefficients_8x8(
    const uint8_t *data, size_t bit_size, size_t bit_offset,
    cavs_basic_block_kind kind, cavs_basic_coefficients *coefficients);

#ifdef CAVS_TESTING
int cavs_test_basic_vlc_info(cavs_basic_block_kind kind, unsigned table_index,
                             unsigned entry_index, uint32_t *code,
                             uint32_t *run, uint32_t *level,
                             unsigned *entry_count);
#endif

#endif
