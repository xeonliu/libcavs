/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, information technology - Advanced Audio Video Coding
 * Standard - Part 2: Video, 9.10.1-9.10.2 and Figures 36-43.
 * Scalar fractional-sample interpolation kernels shared by the supported
 * baseline and broadcast profiles.
 */
#include "dsp/motion_compensation.h"
#include <limits.h>

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

static int block_arguments_valid(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    const uint8_t *prediction, size_t prediction_stride) {
    if (plane == NULL || prediction == NULL || width == 0U || height == 0U ||
        block_width == 0U || block_height == 0U)
        return 0;
    if (stride < width || prediction_stride < block_width ||
        block_width > CAVS_MAX_MOTION_BLOCK_DIMENSION ||
        block_height > CAVS_MAX_MOTION_BLOCK_DIMENSION ||
        x0 >= width || y0 >= height || width - x0 < block_width ||
        height - y0 < block_height)
        return 0;
    if (height - 1U > (SIZE_MAX - (width - 1U)) / stride ||
        block_height - 1U >
            (SIZE_MAX - (block_width - 1U)) / prediction_stride)
        return 0;
    return 1;
}

cavs_result cavs_dsp_interpolate_chroma_block_c(
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
    if (!block_arguments_valid(
            plane, width, height, stride, x0, y0, block_width, block_height,
            prediction, prediction_stride) ||
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

typedef struct cavs_luma_source {
    const uint8_t *plane;
    size_t width;
    size_t height;
    size_t stride;
    size_t base_x;
    size_t base_y;
    int32_t integer_x;
    int32_t integer_y;
} cavs_luma_source;

static int32_t luma_integer(const cavs_luma_source *source,
                            int32_t offset_x, int32_t offset_y) {
    size_t x = clamped_coordinate(source->base_x,
                                  source->integer_x + offset_x,
                                  source->width - 1U);
    size_t y = clamped_coordinate(source->base_y,
                                  source->integer_y + offset_y,
                                  source->height - 1U);
    return source->plane[y * source->stride + x];
}

static int32_t luma_horizontal_half(const cavs_luma_source *source,
                                    int32_t anchor_x, int32_t row) {
    return -luma_integer(source, anchor_x - 1, row) +
           5 * luma_integer(source, anchor_x, row) +
           5 * luma_integer(source, anchor_x + 1, row) -
           luma_integer(source, anchor_x + 2, row);
}

static int32_t luma_vertical_half(const cavs_luma_source *source,
                                  int32_t column, int32_t anchor_y) {
    return -luma_integer(source, column, anchor_y - 1) +
           5 * luma_integer(source, column, anchor_y) +
           5 * luma_integer(source, column, anchor_y + 1) -
           luma_integer(source, column, anchor_y + 2);
}

static int32_t luma_half_half(const cavs_luma_source *source,
                              int32_t anchor_x, int32_t anchor_y) {
    return -luma_horizontal_half(source, anchor_x, anchor_y - 1) +
           5 * luma_horizontal_half(source, anchor_x, anchor_y) +
           5 * luma_horizontal_half(source, anchor_x, anchor_y + 1) -
           luma_horizontal_half(source, anchor_x, anchor_y + 2);
}

static int64_t floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static uint8_t rounded_luma(int32_t value, int32_t bias, unsigned shift) {
    int64_t rounded = floor_shift((int64_t)value + bias, shift);
    if (rounded < 0) return 0U;
    if (rounded > 255) return 255U;
    return (uint8_t)rounded;
}

static uint8_t luma_quarter_sample(const cavs_luma_source *source,
                                   uint32_t fraction_x,
                                   uint32_t fraction_y) {
    int32_t center;
    if (fraction_x == 0U && fraction_y == 0U)
        return (uint8_t)luma_integer(source, 0, 0);
    /* Axis-aligned a/b/c and d/h/n positions from Figure 42. */
    if (fraction_y == 0U) {
        if (fraction_x == 2U)
            return rounded_luma(luma_horizontal_half(source, 0, 0), 4, 3U);
        if (fraction_x == 1U)
            center = luma_horizontal_half(source, -1, 0) +
                     56 * luma_integer(source, 0, 0) +
                     7 * luma_horizontal_half(source, 0, 0) +
                     8 * luma_integer(source, 1, 0);
        else
            center = 8 * luma_integer(source, 0, 0) +
                     7 * luma_horizontal_half(source, 0, 0) +
                     56 * luma_integer(source, 1, 0) +
                     luma_horizontal_half(source, 1, 0);
        return rounded_luma(center, 64, 7U);
    }
    if (fraction_x == 0U) {
        if (fraction_y == 2U)
            return rounded_luma(luma_vertical_half(source, 0, 0), 4, 3U);
        if (fraction_y == 1U)
            center = luma_vertical_half(source, 0, -1) +
                     56 * luma_integer(source, 0, 0) +
                     7 * luma_vertical_half(source, 0, 0) +
                     8 * luma_integer(source, 0, 1);
        else
            center = 8 * luma_integer(source, 0, 0) +
                     7 * luma_vertical_half(source, 0, 0) +
                     56 * luma_integer(source, 0, 1) +
                     luma_vertical_half(source, 0, 1);
        return rounded_luma(center, 64, 7U);
    }

    center = luma_half_half(source, 0, 0);
    if (fraction_x == 2U && fraction_y == 2U)
        return rounded_luma(center, 32, 6U);
    /* One half-sample axis: i/k horizontally and f/q vertically. */
    if (fraction_y == 2U) {
        if (fraction_x == 1U)
            center = luma_half_half(source, -1, 0) +
                     56 * luma_vertical_half(source, 0, 0) + 7 * center +
                     8 * luma_vertical_half(source, 1, 0);
        else
            center = 8 * luma_vertical_half(source, 0, 0) + 7 * center +
                     56 * luma_vertical_half(source, 1, 0) +
                     luma_half_half(source, 1, 0);
        return rounded_luma(center, 512, 10U);
    }
    if (fraction_x == 2U) {
        if (fraction_y == 1U)
            center = luma_half_half(source, 0, -1) +
                     56 * luma_horizontal_half(source, 0, 0) + 7 * center +
                     8 * luma_horizontal_half(source, 0, 1);
        else
            center = 8 * luma_horizontal_half(source, 0, 0) + 7 * center +
                     56 * luma_horizontal_half(source, 0, 1) +
                     luma_half_half(source, 0, 1);
        return rounded_luma(center, 512, 10U);
    }

    /* Diagonal quarter positions e/g/p/r average j' with the nearest corner. */
    return rounded_luma(
        64 * luma_integer(source, fraction_x == 3U ? 1 : 0,
                          fraction_y == 3U ? 1 : 0) + center,
        64, 7U);
}

static int32_t half_grid_anchor(int32_t index) {
    int32_t anchor = index / 2;
    if (index < 0 && index % 2 != 0) --anchor;
    return anchor;
}

/* Returns an unrounded half-sample-grid value scaled by 64. */
static int32_t luma_half_grid64(const cavs_luma_source *source,
                                int32_t grid_x, int32_t grid_y) {
    int odd_x = grid_x % 2 != 0;
    int odd_y = grid_y % 2 != 0;
    int32_t anchor_x = half_grid_anchor(grid_x);
    int32_t anchor_y = half_grid_anchor(grid_y);
    if (!odd_x && !odd_y)
        return 64 * luma_integer(source, anchor_x, anchor_y);
    if (odd_x && !odd_y)
        return 8 * luma_horizontal_half(source, anchor_x, anchor_y);
    if (!odd_x && odd_y)
        return 8 * luma_vertical_half(source, anchor_x, anchor_y);
    return luma_half_half(source, anchor_x, anchor_y);
}

static uint8_t luma_eighth_axis_sample(const cavs_luma_source *source,
                                        uint32_t fraction,
                                        uint32_t fixed_fraction,
                                        int horizontal) {
    static const int8_t filters[2][4] = {
        {-6, 56, 15, -1},
        {-1, 15, 56, -6}
    };
    const int8_t *filter = filters[fraction % 4U == 1U ? 0 : 1];
    int32_t segment = (int32_t)(fraction / 4U);
    int32_t fixed = (int32_t)(fixed_fraction / 4U);
    int32_t sum = 0;
    unsigned index;
    for (index = 0U; index < 4U; ++index) {
        int32_t varying = segment + (int32_t)index - 1;
        int32_t value = horizontal
                            ? luma_half_grid64(source, varying, fixed)
                            : luma_half_grid64(source, fixed, varying);
        sum += filter[index] * value;
    }
    return rounded_luma(sum, 2048, 12U);
}

static uint8_t luma_eighth_interior_sample(const cavs_luma_source *source,
                                            uint32_t fraction_x,
                                            uint32_t fraction_y) {
    uint32_t cell_x = fraction_x / 4U;
    uint32_t cell_y = fraction_y / 4U;
    uint32_t local_x = fraction_x % 4U;
    uint32_t local_y = fraction_y % 4U;
    int32_t top_left = luma_half_grid64(
        source, (int32_t)cell_x, (int32_t)cell_y);
    int32_t top_right = luma_half_grid64(
        source, (int32_t)cell_x + 1, (int32_t)cell_y);
    int32_t bottom_left = luma_half_grid64(
        source, (int32_t)cell_x, (int32_t)cell_y + 1);
    int32_t bottom_right = luma_half_grid64(
        source, (int32_t)cell_x + 1, (int32_t)cell_y + 1);
    int32_t sum = (int32_t)((4U - local_x) * (4U - local_y)) * top_left +
                  (int32_t)(local_x * (4U - local_y)) * top_right +
                  (int32_t)((4U - local_x) * local_y) * bottom_left +
                  (int32_t)(local_x * local_y) * bottom_right;
    return rounded_luma(sum, 512, 10U);
}

static uint8_t luma_eighth_sample(const cavs_luma_source *source,
                                  uint32_t fraction_x,
                                  uint32_t fraction_y) {
    if (fraction_x % 2U == 0U && fraction_y % 2U == 0U)
        return luma_quarter_sample(source, fraction_x / 2U, fraction_y / 2U);
    if (fraction_x % 2U != 0U && fraction_y % 4U == 0U)
        return luma_eighth_axis_sample(
            source, fraction_x, fraction_y, 1);
    if (fraction_y % 2U != 0U && fraction_x % 4U == 0U)
        return luma_eighth_axis_sample(
            source, fraction_y, fraction_x, 0);
    return luma_eighth_interior_sample(source, fraction_x, fraction_y);
}

cavs_result cavs_dsp_interpolate_luma_block_quarter_c(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride) {
    uint8_t parsed[CAVS_MAX_MOTION_BLOCK_DIMENSION *
                   CAVS_MAX_MOTION_BLOCK_DIMENSION];
    cavs_luma_source source;
    uint32_t fraction_x;
    uint32_t fraction_y;
    size_t x;
    size_t y;
    if (!block_arguments_valid(
            plane, width, height, stride, x0, y0, block_width, block_height,
            prediction, prediction_stride))
        return CAVS_ERR_INVALID_ARGUMENT;
    source.plane = plane;
    source.width = width;
    source.height = height;
    source.stride = stride;
    split_motion(motion_x, 4, &source.integer_x, &fraction_x);
    split_motion(motion_y, 4, &source.integer_y, &fraction_y);
    for (y = 0U; y < block_height; ++y) {
        source.base_y = y0 + y;
        for (x = 0U; x < block_width; ++x) {
            source.base_x = x0 + x;
            parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x] =
                luma_quarter_sample(&source, fraction_x, fraction_y);
        }
    }
    for (y = 0U; y < block_height; ++y)
        for (x = 0U; x < block_width; ++x)
            prediction[y * prediction_stride + x] =
                parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x];
    return CAVS_OK;
}

cavs_result cavs_dsp_interpolate_luma_block_eighth_c(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride) {
    uint8_t parsed[CAVS_MAX_MOTION_BLOCK_DIMENSION *
                   CAVS_MAX_MOTION_BLOCK_DIMENSION];
    cavs_luma_source source;
    uint32_t fraction_x;
    uint32_t fraction_y;
    size_t x;
    size_t y;
    if (!block_arguments_valid(
            plane, width, height, stride, x0, y0, block_width, block_height,
            prediction, prediction_stride))
        return CAVS_ERR_INVALID_ARGUMENT;
    source.plane = plane;
    source.width = width;
    source.height = height;
    source.stride = stride;
    split_motion(motion_x, 8, &source.integer_x, &fraction_x);
    split_motion(motion_y, 8, &source.integer_y, &fraction_y);
    for (y = 0U; y < block_height; ++y) {
        source.base_y = y0 + y;
        for (x = 0U; x < block_width; ++x) {
            source.base_x = x0 + x;
            parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x] =
                luma_eighth_sample(&source, fraction_x, fraction_y);
        }
    }
    for (y = 0U; y < block_height; ++y)
        for (x = 0U; x < block_width; ++x)
            prediction[y * prediction_stride + x] =
                parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x];
    return CAVS_OK;
}
