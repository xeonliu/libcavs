/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, information technology - Advanced Audio Video
 * Coding Standard - Part 2: Video, 9.8.2-9.8.3 and Table 71.
 */
#include "dsp/transform.h"
#include <limits.h>

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

/* GB/T 20090.2-2013 9.8.3 and Table 71 integer transform matrix. */
static const int8_t transform_4x4[16] = {
     2, 3, 2, 1,
     2, 1,-2,-3,
     2,-1,-2, 3,
     2,-3, 2,-1
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

cavs_result cavs_dsp_inverse_transform_8x8_c(const int32_t coefficients[64],
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

cavs_result cavs_dsp_inverse_transform_4x4_c(
    const int32_t coefficients[16], int16_t residual[16]) {
    int64_t horizontal[16];
    int16_t parsed[16];
    unsigned row;
    unsigned column;
    unsigned term;
    if (coefficients == NULL || residual == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (row = 0U; row < 4U; ++row) {
        for (column = 0U; column < 4U; ++column) {
            int64_t sum = 0;
            for (term = 0U; term < 4U; ++term) {
                int32_t coefficient = coefficients[row * 4U + term];
                if (coefficient < -4096 || coefficient > 4095)
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                sum += (int64_t)coefficient *
                       transform_4x4[column * 4U + term];
            }
            horizontal[row * 4U + column] = sum;
        }
    }
    for (row = 0U; row < 4U; ++row) {
        for (column = 0U; column < 4U; ++column) {
            int64_t sum = 0;
            for (term = 0U; term < 4U; ++term)
                sum += (int64_t)transform_4x4[row * 4U + term] *
                       horizontal[term * 4U + column];
            parsed[row * 4U + column] = (int16_t)floor_shift(
                clip(-8192, 8191, sum + 16), 5U);
        }
    }
    for (row = 0U; row < 16U; ++row) residual[row] = parsed[row];
    return CAVS_OK;
}
