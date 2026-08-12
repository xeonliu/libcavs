/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Basic-entropy coefficient decoding, inverse scan, QP mapping, and inverse
 * quantization from GB/T 20090.2-2013 8.3.1, 9.5.1.2, 9.5.3, 9.7.1-9.7.2,
 * Figures 33-34, Table 70, and normative Annex D Tables D.1-D.20.
 */
#ifndef CAVS_COEFFICIENTS_H
#define CAVS_COEFFICIENTS_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_COEFFICIENT_COUNT_8X8 64U
#define CAVS_BLOCK_8X8_COEFFICIENTS CAVS_COEFFICIENT_COUNT_8X8

typedef enum cavs_scan_mode_8x8 {
    CAVS_SCAN_8X8_FRAME = 0,
    CAVS_SCAN_8X8_FIELD = 1
} cavs_scan_mode_8x8;

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

/* GB/T 20090.2-2013 9.5.3 Figures 33-34 map scan to matrix order. */
cavs_result cavs_inverse_scan_8x8(
    const int32_t scan[CAVS_BLOCK_8X8_COEFFICIENTS],
    cavs_scan_mode_8x8 mode,
    int32_t matrix[CAVS_BLOCK_8X8_COEFFICIENTS]);

/* Applies the Table 70 chroma-QP mapping after adding the signed delta. */
cavs_result cavs_map_chroma_qp(uint8_t luma_qp, int8_t delta,
                               uint8_t *chroma_qp);

/* Implements 9.7.2 inverse quantization for one 8x8 coefficient matrix. */
cavs_result cavs_inverse_quantize_8x8(
    const int32_t quant[CAVS_BLOCK_8X8_COEFFICIENTS],
    const int32_t predicted_quant[CAVS_BLOCK_8X8_COEFFICIENTS],
    const uint8_t weights[CAVS_BLOCK_8X8_COEFFICIENTS], uint8_t qp,
    int32_t coefficients[CAVS_BLOCK_8X8_COEFFICIENTS]);

#ifdef CAVS_TESTING
int cavs_test_basic_vlc_info(cavs_basic_block_kind kind, unsigned table_index,
                             unsigned entry_index, uint32_t *code,
                             uint32_t *run, uint32_t *level,
                             unsigned *entry_count);
#endif

#endif
