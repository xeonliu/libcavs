/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Baseline-profile inverse scan, inverse quantization, and inverse transform.
 */
#ifndef CAVS_RECONSTRUCTION_H
#define CAVS_RECONSTRUCTION_H

#include <cavs/cavs.h>
#include <stdint.h>

#define CAVS_BLOCK_8X8_COEFFICIENTS 64U

typedef enum cavs_scan_mode_8x8 {
    CAVS_SCAN_8X8_FRAME = 0,
    CAVS_SCAN_8X8_FIELD = 1
} cavs_scan_mode_8x8;

/* Maps QuantCoeffArray to the row-major QuantCoeffMatrix from Figure 33/34. */
cavs_result cavs_inverse_scan_8x8(
    const int32_t scan[CAVS_BLOCK_8X8_COEFFICIENTS],
    cavs_scan_mode_8x8 mode,
    int32_t matrix[CAVS_BLOCK_8X8_COEFFICIENTS]);

/* Applies the Table 70 chroma-QP mapping after adding the signed delta. */
cavs_result cavs_map_chroma_qp(uint8_t luma_qp, int8_t delta,
                               uint8_t *chroma_qp);

/* Implements 9.7.2 for one 8x8 matrix. Arrays are row-major. */
cavs_result cavs_inverse_quantize_8x8(
    const int32_t quant[CAVS_BLOCK_8X8_COEFFICIENTS],
    const int32_t predicted_quant[CAVS_BLOCK_8X8_COEFFICIENTS],
    const uint8_t weights[CAVS_BLOCK_8X8_COEFFICIENTS], uint8_t qp,
    int32_t coefficients[CAVS_BLOCK_8X8_COEFFICIENTS]);

/* Implements the 8-bit form of the 9.8.2 separable inverse transform. */
cavs_result cavs_inverse_transform_8x8(
    const int32_t coefficients[CAVS_BLOCK_8X8_COEFFICIENTS],
    int16_t residual[CAVS_BLOCK_8X8_COEFFICIENTS]);

#endif
