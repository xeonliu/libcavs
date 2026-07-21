/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 9.9.1-9.9.4, Tables 65-66, Figure 20, and 9.11.
 */
#include "prediction.h"

#define REFERENCE_MASK_8X8 UINT32_C(0x1ffff)

static int range_available(uint32_t mask, unsigned first, unsigned last) {
    unsigned index;
    for (index = first; index <= last; ++index) {
        if ((mask & (UINT32_C(1) << index)) == 0U) return 0;
    }
    return 1;
}

static int references_valid(const cavs_intra_references_8x8 *references) {
    if (references == NULL ||
        (references->top_available & ~REFERENCE_MASK_8X8) != 0U ||
        (references->left_available & ~REFERENCE_MASK_8X8) != 0U)
        return 0;
    if (((references->top_available ^ references->left_available) & 1U) != 0U)
        return 0;
    if ((references->top_available & references->left_available & 1U) != 0U &&
        references->top[0] != references->left[0])
        return 0;
    return 1;
}

static uint8_t filtered(const uint8_t samples[17], unsigned index) {
    unsigned center = index > 16U ? 16U : index;
    unsigned before = center == 0U ? 0U : center - 1U;
    unsigned after = center >= 16U ? 16U : center + 1U;
    return (uint8_t)((samples[before] + 2U * samples[center] +
                      samples[after] + 2U) >> 2U);
}

static int64_t floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static uint8_t clip_sample(int64_t value) {
    if (value < 0) return 0U;
    if (value > 255) return 255U;
    return (uint8_t)value;
}

static void predict_dc(const cavs_intra_references_8x8 *references,
                       uint8_t prediction[64]) {
    int top = range_available(references->top_available, 0U, 9U);
    int left = range_available(references->left_available, 0U, 9U);
    unsigned x;
    unsigned y;
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x) {
            uint8_t value;
            if (top && left) {
                value = (uint8_t)((filtered(references->top, x + 1U) +
                                   filtered(references->left, y + 1U)) >> 1U);
            } else if (top) {
                value = filtered(references->top, x + 1U);
            } else if (left) {
                value = filtered(references->left, y + 1U);
            } else {
                value = 128U;
            }
            prediction[y * 8U + x] = value;
        }
    }
}

cavs_result cavs_predict_intra_luma_8x8(
    const cavs_intra_references_8x8 *references,
    cavs_intra_luma_mode_8x8 mode, uint8_t prediction[64]) {
    uint8_t parsed[64];
    unsigned x;
    unsigned y;
    if (!references_valid(references) || prediction == NULL ||
        mode < CAVS_INTRA_LUMA_VERTICAL_8X8 ||
        mode > CAVS_INTRA_LUMA_DOWN_RIGHT_8X8)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (mode == CAVS_INTRA_LUMA_VERTICAL_8X8) {
        if (!range_available(references->top_available, 1U, 8U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (y = 0U; y < 8U; ++y)
            for (x = 0U; x < 8U; ++x)
                parsed[y * 8U + x] = references->top[x + 1U];
    } else if (mode == CAVS_INTRA_LUMA_HORIZONTAL_8X8) {
        if (!range_available(references->left_available, 1U, 8U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (y = 0U; y < 8U; ++y)
            for (x = 0U; x < 8U; ++x)
                parsed[y * 8U + x] = references->left[y + 1U];
    } else if (mode == CAVS_INTRA_LUMA_DC_8X8) {
        predict_dc(references, parsed);
    } else if (mode == CAVS_INTRA_LUMA_DOWN_LEFT_8X8) {
        if (!range_available(references->top_available, 1U, 16U) ||
            !range_available(references->left_available, 1U, 16U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (y = 0U; y < 8U; ++y) {
            for (x = 0U; x < 8U; ++x) {
                unsigned index = x + y + 2U;
                parsed[y * 8U + x] = (uint8_t)(
                    (filtered(references->top, index) +
                     filtered(references->left, index)) >> 1U);
            }
        }
    } else {
        if (!range_available(references->top_available, 0U, 16U) ||
            !range_available(references->left_available, 0U, 16U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (y = 0U; y < 8U; ++y) {
            for (x = 0U; x < 8U; ++x) {
                const uint8_t *samples;
                unsigned distance;
                if (x == y) {
                    parsed[y * 8U + x] = (uint8_t)(
                        (references->left[1] + 2U * references->top[0] +
                         references->top[1] + 2U) >> 2U);
                    continue;
                }
                samples = x > y ? references->top : references->left;
                distance = x > y ? x - y : y - x;
                parsed[y * 8U + x] = (uint8_t)(
                    (samples[distance + 1U] + 2U * samples[distance] +
                     samples[distance - 1U] + 2U) >> 2U);
            }
        }
    }
    for (x = 0U; x < 64U; ++x) prediction[x] = parsed[x];
    return CAVS_OK;
}

cavs_result cavs_predict_intra_chroma_8x8(
    const cavs_intra_references_8x8 *references,
    cavs_intra_chroma_mode_8x8 mode, uint8_t prediction[64]) {
    uint8_t parsed[64];
    unsigned x;
    unsigned y;
    if (!references_valid(references) || prediction == NULL ||
        mode < CAVS_INTRA_CHROMA_DC_8X8 ||
        mode > CAVS_INTRA_CHROMA_PLANE_8X8)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (mode == CAVS_INTRA_CHROMA_DC_8X8) {
        predict_dc(references, parsed);
    } else if (mode == CAVS_INTRA_CHROMA_HORIZONTAL_8X8) {
        if (!range_available(references->left_available, 1U, 8U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (y = 0U; y < 8U; ++y)
            for (x = 0U; x < 8U; ++x)
                parsed[y * 8U + x] = references->left[y + 1U];
    } else if (mode == CAVS_INTRA_CHROMA_VERTICAL_8X8) {
        if (!range_available(references->top_available, 1U, 8U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (y = 0U; y < 8U; ++y)
            for (x = 0U; x < 8U; ++x)
                parsed[y * 8U + x] = references->top[x + 1U];
    } else {
        int horizontal = 0;
        int vertical = 0;
        int a;
        int b;
        int c;
        unsigned index;
        if (!range_available(references->top_available, 1U, 8U) ||
            !range_available(references->left_available, 1U, 8U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (index = 0U; index < 4U; ++index) {
            horizontal += (int)(index + 1U) *
                ((int)references->top[5U + index] -
                 references->top[3U - index]);
            vertical += (int)(index + 1U) *
                ((int)references->left[5U + index] -
                 references->left[3U - index]);
        }
        a = ((int)references->top[8] + references->left[8]) << 4;
        b = (int)floor_shift(17 * (int64_t)horizontal + 16, 5U);
        c = (int)floor_shift(17 * (int64_t)vertical + 16, 5U);
        for (y = 0U; y < 8U; ++y) {
            for (x = 0U; x < 8U; ++x) {
                int64_t value = a + ((int)x - 3) * (int64_t)b +
                                ((int)y - 3) * (int64_t)c + 16;
                parsed[y * 8U + x] = clip_sample(floor_shift(value, 5U));
            }
        }
    }
    for (x = 0U; x < 64U; ++x) prediction[x] = parsed[x];
    return CAVS_OK;
}

cavs_result cavs_reconstruct_samples_8x8(const uint8_t forward[64],
                                          const uint8_t backward[64],
                                          const int16_t residual[64],
                                          uint8_t reconstructed[64]) {
    uint8_t parsed[64];
    unsigned index;
    if (forward == NULL || residual == NULL || reconstructed == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (index = 0U; index < 64U; ++index) {
        int prediction = forward[index];
        if (backward != NULL)
            prediction = (prediction + backward[index] + 1) >> 1;
        parsed[index] = clip_sample((int64_t)prediction + residual[index]);
    }
    for (index = 0U; index < 64U; ++index) reconstructed[index] = parsed[index];
    return CAVS_OK;
}

#undef REFERENCE_MASK_8X8
