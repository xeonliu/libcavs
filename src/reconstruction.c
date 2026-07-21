/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 9.5.3, 9.7.1-9.7.2, Figures 33-34, Tables 70-71, and 9.8.2.
 */
#include "reconstruction.h"
#include <limits.h>

static const uint8_t frame_scan[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

static const uint8_t field_scan[64] = {
     0,  3, 11, 16, 22, 32, 38, 55,
     1,  6, 12, 20, 25, 33, 42, 57,
     2,  7, 15, 21, 28, 37, 43, 58,
     4, 10, 19, 27, 31, 39, 47, 59,
     5, 14, 24, 30, 36, 44, 50, 60,
     8, 17, 26, 35, 41, 48, 52, 61,
     9, 18, 29, 40, 46, 51, 54, 62,
    13, 23, 34, 45, 49, 53, 56, 63
};

static const uint8_t chroma_qp_table[64] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 42, 43, 43, 44, 44,
    45, 45, 46, 46, 47, 47, 48, 48, 48, 49, 49, 49, 50, 50, 50, 51
};

static const uint16_t dequant_table[64] = {
    32768, 36061, 38968, 42495, 46341, 50535, 55437, 60424,
    32932, 35734, 38968, 42495, 46177, 50535, 55109, 59933,
    65535, 35734, 38968, 42577, 46341, 50617, 55027, 60097,
    32809, 35734, 38968, 42454, 46382, 50576, 55109, 60056,
    65535, 35734, 38968, 42495, 46320, 50515, 55109, 60076,
    65535, 35744, 38968, 42495, 46341, 50535, 55099, 60087,
    65535, 35734, 38973, 42500, 46341, 50535, 55109, 60097,
    32771, 35734, 38965, 42497, 46341, 50535, 55109, 60099
};

static const uint8_t dequant_shift[64] = {
    14,14,14,14,14,14,14,14, 13,13,13,13,13,13,13,13,
    13,12,12,12,12,12,12,12, 11,11,11,11,11,11,11,11,
    11,10,10,10,10,10,10,10, 10, 9, 9, 9, 9, 9, 9, 9,
     9, 8, 8, 8, 8, 8, 8, 8,  7, 7, 7, 7, 7, 7, 7, 7
};

static const int8_t transform_8x8[64] = {
     8, 10, 10,  9,  8,  6,  4,  2,
     8,  9,  4, -2, -8,-10,-10, -6,
     8,  6, -4,-10, -8,  2, 10,  9,
     8,  2,-10, -6,  8,  9, -4,-10,
     8, -2,-10,  6,  8, -9, -4, 10,
     8, -6, -4, 10, -8, -2, 10, -9,
     8, -9,  4,  2, -8, 10,-10,  6,
     8,-10, 10, -9,  8, -6,  4, -2
};

static int64_t floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static int64_t clip(int64_t minimum, int64_t maximum, int64_t value) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

cavs_result cavs_inverse_scan_8x8(const int32_t scan[64],
                                  cavs_scan_mode_8x8 mode,
                                  int32_t matrix[64]) {
    const uint8_t *mapping;
    int32_t parsed[64];
    unsigned index;
    if (scan == NULL || matrix == NULL ||
        (mode != CAVS_SCAN_8X8_FRAME && mode != CAVS_SCAN_8X8_FIELD))
        return CAVS_ERR_INVALID_ARGUMENT;
    mapping = mode == CAVS_SCAN_8X8_FRAME ? frame_scan : field_scan;
    for (index = 0U; index < 64U; ++index)
        parsed[index] = scan[mapping[index]];
    for (index = 0U; index < 64U; ++index) matrix[index] = parsed[index];
    return CAVS_OK;
}

cavs_result cavs_map_chroma_qp(uint8_t luma_qp, int8_t delta,
                               uint8_t *chroma_qp) {
    int combined;
    if (chroma_qp == NULL || luma_qp > 63U)
        return CAVS_ERR_INVALID_ARGUMENT;
    combined = (int)luma_qp + (int)delta;
    if (combined < 0 || combined > 63)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *chroma_qp = chroma_qp_table[combined];
    return CAVS_OK;
}

cavs_result cavs_inverse_quantize_8x8(const int32_t quant[64],
                                      const int32_t predicted_quant[64],
                                      const uint8_t weights[64], uint8_t qp,
                                      int32_t coefficients[64]) {
    int32_t parsed[64];
    unsigned index;
    unsigned shift;
    if (quant == NULL || predicted_quant == NULL || weights == NULL ||
        coefficients == NULL || qp > 63U)
        return CAVS_ERR_INVALID_ARGUMENT;
    shift = dequant_shift[qp];
    for (index = 0U; index < 64U; ++index) {
        int64_t difference = (int64_t)quant[index] - predicted_quant[index];
        int64_t value;
        if (difference < -2048 || difference > 2047)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        value = floor_shift(difference * weights[index], 3U);
        value = floor_shift(value * dequant_table[qp], 4U);
        value = floor_shift(value + (INT64_C(1) << (shift - 1U)), shift);
        if (value < -8192 || value > 8191)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed[index] = (int32_t)value;
    }
    for (index = 0U; index < 64U; ++index) coefficients[index] = parsed[index];
    return CAVS_OK;
}

cavs_result cavs_inverse_transform_8x8(const int32_t coefficients[64],
                                       int16_t residual[64]) {
    int64_t horizontal[64];
    int16_t parsed[64];
    unsigned row;
    unsigned column;
    unsigned term;
    if (coefficients == NULL || residual == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (row = 0U; row < 8U; ++row) {
        for (column = 0U; column < 8U; ++column) {
            int64_t sum = 0;
            for (term = 0U; term < 8U; ++term) {
                int32_t coefficient = coefficients[row * 8U + term];
                if (coefficient < -8192 || coefficient > 8191)
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                sum += (int64_t)coefficient *
                       transform_8x8[column * 8U + term];
            }
            horizontal[row * 8U + column] =
                floor_shift(clip(-32768, 32767, sum + 4), 3U);
        }
    }
    for (row = 0U; row < 8U; ++row) {
        for (column = 0U; column < 8U; ++column) {
            int64_t sum = 0;
            for (term = 0U; term < 8U; ++term)
                sum += (int64_t)transform_8x8[row * 8U + term] *
                       horizontal[term * 8U + column];
            parsed[row * 8U + column] = (int16_t)floor_shift(
                clip(-32768, 32767, sum + 64), 7U);
        }
    }
    for (row = 0U; row < 64U; ++row) residual[row] = parsed[row];
    return CAVS_OK;
}
