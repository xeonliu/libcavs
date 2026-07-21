/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 9.10.2.1 and 9.10.2.3, Figure 43.
 */
#include "motion.h"
#include <limits.h>

cavs_result cavs_derive_chroma_motion(
    cavs_pixel_format format, int32_t luma_x, int32_t luma_y,
    int32_t *chroma_x, int32_t *chroma_y) {
    int64_t derived_y;
    if (chroma_x == NULL || chroma_y == NULL ||
        (format != CAVS_YUV420P8 && format != CAVS_YUV422P8))
        return CAVS_ERR_INVALID_ARGUMENT;
    derived_y = format == CAVS_YUV422P8 ? 2 * (int64_t)luma_y : luma_y;
    if (derived_y < INT32_MIN || derived_y > INT32_MAX)
        return CAVS_ERR_INVALID_ARGUMENT;
    *chroma_x = luma_x;
    *chroma_y = (int32_t)derived_y;
    return CAVS_OK;
}

static void split_motion(int32_t motion, int32_t denominator,
                         int32_t *integer, uint32_t *fraction) {
    int32_t quotient = motion / denominator;
    int32_t remainder = motion % denominator;
    if (remainder < 0) {
        --quotient;
        remainder += denominator;
    }
    *integer = quotient;
    *fraction = (uint32_t)remainder;
}

static size_t clamped_coordinate(size_t base, int32_t displacement,
                                 size_t limit) {
    if (displacement < 0) {
        uint32_t magnitude = (uint32_t)(-(int64_t)displacement);
        return (size_t)magnitude > base ? 0U : base - magnitude;
    }
    return (size_t)displacement > limit - base
               ? limit
               : base + (size_t)displacement;
}

cavs_result cavs_interpolate_chroma_block(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    cavs_chroma_motion_precision precision,
    uint8_t *prediction, size_t prediction_stride) {
    uint8_t parsed[CAVS_MAX_MOTION_BLOCK_DIMENSION *
                   CAVS_MAX_MOTION_BLOCK_DIMENSION];
    int32_t integer_x;
    int32_t integer_y;
    uint32_t fraction_x;
    uint32_t fraction_y;
    uint32_t denominator;
    uint32_t scale;
    size_t x;
    size_t y;
    if (plane == NULL || prediction == NULL || width == 0U || height == 0U ||
        stride < width || block_width == 0U || block_height == 0U ||
        block_width > CAVS_MAX_MOTION_BLOCK_DIMENSION ||
        block_height > CAVS_MAX_MOTION_BLOCK_DIMENSION ||
        x0 >= width || y0 >= height || width - x0 < block_width ||
        height - y0 < block_height || prediction_stride < block_width ||
        width > (size_t)INT64_MAX || height > (size_t)INT64_MAX ||
        height - 1U > (SIZE_MAX - (width - 1U)) / stride ||
        (block_height - 1U) >
            (SIZE_MAX - (block_width - 1U)) / prediction_stride ||
        (precision != CAVS_CHROMA_MOTION_EIGHTH &&
         precision != CAVS_CHROMA_MOTION_SIXTEENTH))
        return CAVS_ERR_INVALID_ARGUMENT;

    denominator = UINT32_C(1) << (unsigned)precision;
    scale = denominator * denominator;
    split_motion(motion_x, (int32_t)denominator, &integer_x, &fraction_x);
    split_motion(motion_y, (int32_t)denominator, &integer_y, &fraction_y);
    for (y = 0U; y < block_height; ++y) {
        size_t sample_y = clamped_coordinate(y0 + y, integer_y, height - 1U);
        size_t next_y = clamped_coordinate(y0 + y, integer_y + 1,
                                           height - 1U);
        for (x = 0U; x < block_width; ++x) {
            size_t sample_x = clamped_coordinate(x0 + x, integer_x, width - 1U);
            size_t next_x = clamped_coordinate(x0 + x, integer_x + 1,
                                               width - 1U);
            uint32_t a = plane[sample_y * stride + sample_x];
            uint32_t b = plane[sample_y * stride + next_x];
            uint32_t c = plane[next_y * stride + sample_x];
            uint32_t d = plane[next_y * stride + next_x];
            uint32_t sum = (denominator - fraction_x) *
                               (denominator - fraction_y) * a +
                           fraction_x * (denominator - fraction_y) * b +
                           (denominator - fraction_x) * fraction_y * c +
                           fraction_x * fraction_y * d;
            parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x] =
                (uint8_t)((sum + scale / 2U) / scale);
        }
    }
    for (y = 0U; y < block_height; ++y)
        for (x = 0U; x < block_width; ++x)
            prediction[y * prediction_stride + x] =
                parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x];
    return CAVS_OK;
}
