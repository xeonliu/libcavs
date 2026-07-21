/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Fractional-sample interpolation and motion-compensation primitives.
 */
#ifndef CAVS_MOTION_H
#define CAVS_MOTION_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_MAX_MOTION_BLOCK_DIMENSION 16U

typedef enum cavs_chroma_motion_precision {
    CAVS_CHROMA_MOTION_EIGHTH = 3,
    CAVS_CHROMA_MOTION_SIXTEENTH = 4
} cavs_chroma_motion_precision;

/* Maps the luma motion vector to YUV420 or YUV422 chroma sample units. */
cavs_result cavs_derive_chroma_motion(
    cavs_pixel_format format, int32_t luma_x, int32_t luma_y,
    int32_t *chroma_x, int32_t *chroma_y);

/*
 * Interpolates one chroma block using 9.10.2.3 bilinear filtering. Motion
 * components use the selected chroma-sample precision. Reference positions
 * outside the plane are replaced by the nearest edge sample.
 */
cavs_result cavs_interpolate_chroma_block(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    cavs_chroma_motion_precision precision,
    uint8_t *prediction, size_t prediction_stride);

/* Interpolates one luma block using quarter-sample motion components. */
cavs_result cavs_interpolate_luma_block_quarter(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride);

#endif
