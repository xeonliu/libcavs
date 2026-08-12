/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Scalar fractional-sample interpolation kernels from GB/T 20090.2-2013
 * 9.10.1-9.10.2 and Figures 36-43.
 */
#ifndef CAVS_DSP_MOTION_COMPENSATION_H
#define CAVS_DSP_MOTION_COMPENSATION_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_MAX_MOTION_BLOCK_DIMENSION 16U

typedef enum cavs_chroma_motion_precision {
    CAVS_CHROMA_MOTION_EIGHTH = 3,
    CAVS_CHROMA_MOTION_SIXTEENTH = 4
} cavs_chroma_motion_precision;

/* Implements the 9.10.2.3 chroma bilinear filter with edge replacement. */
cavs_result cavs_dsp_interpolate_chroma_block_c(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    cavs_chroma_motion_precision precision,
    uint8_t *prediction, size_t prediction_stride);

/* Implements the quarter-sample luma positions in Figures 36-42. */
cavs_result cavs_dsp_interpolate_luma_block_quarter_c(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride);

/* Implements the eighth-sample luma positions in Figure 43. */
cavs_result cavs_dsp_interpolate_luma_block_eighth_c(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride);

#endif
