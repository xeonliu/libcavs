/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Motion-vector prediction and fractional-sample interpolation primitives.
 */
#ifndef CAVS_MOTION_H
#define CAVS_MOTION_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_MAX_MOTION_BLOCK_DIMENSION 16U
#define CAVS_MOTION_NEIGHBOR_COUNT 4U

typedef struct cavs_motion_vector {
    int32_t x;
    int32_t y;
} cavs_motion_vector;

typedef struct cavs_motion_candidate {
    cavs_motion_vector vector;
    uint16_t block_distance;
    int8_t reference_index;
    uint8_t available;
    uint8_t intra;
    uint8_t same_direction;
} cavs_motion_candidate;

typedef enum cavs_motion_neighbor {
    CAVS_MOTION_NEIGHBOR_A = 0,
    CAVS_MOTION_NEIGHBOR_B = 1,
    CAVS_MOTION_NEIGHBOR_C = 2,
    CAVS_MOTION_NEIGHBOR_D = 3
} cavs_motion_neighbor;

typedef enum cavs_motion_partition_position {
    CAVS_MOTION_PARTITION_OTHER = 0,
    CAVS_MOTION_PARTITION_8X16_LEFT = 1,
    CAVS_MOTION_PARTITION_8X16_RIGHT = 2,
    CAVS_MOTION_PARTITION_16X8_TOP = 3,
    CAVS_MOTION_PARTITION_16X8_BOTTOM = 4
} cavs_motion_partition_position;

typedef enum cavs_luma_motion_precision {
    CAVS_LUMA_MOTION_QUARTER = 2,
    CAVS_LUMA_MOTION_EIGHTH = 3
} cavs_luma_motion_precision;

typedef enum cavs_chroma_motion_precision {
    CAVS_CHROMA_MOTION_EIGHTH = 3,
    CAVS_CHROMA_MOTION_SIXTEENTH = 4
} cavs_chroma_motion_precision;

/* Implements the profile-0x20/0x48 common path of 9.4.6.2. */
cavs_result cavs_predict_luma_motion(
    const cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT],
    int8_t current_reference_index, uint16_t current_block_distance,
    cavs_motion_partition_position partition,
    cavs_luma_motion_precision precision, cavs_motion_vector *prediction);

/* Adds the decoded 9.4.6.3 difference and enforces the precision range. */
cavs_result cavs_decode_luma_motion(
    const cavs_motion_vector *prediction, const cavs_motion_vector *difference,
    cavs_luma_motion_precision precision, cavs_motion_vector *motion);

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

/* Interpolates one luma block using eighth-sample motion components. */
cavs_result cavs_interpolate_luma_block_eighth(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride);

#endif
