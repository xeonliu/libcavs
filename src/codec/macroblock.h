/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Reconstruction-facing macroblock representation shared by codec profiles.
 */
#ifndef CAVS_CODEC_MACROBLOCK_H
#define CAVS_CODEC_MACROBLOCK_H

#include <stddef.h>
#include <stdint.h>

#define CAVS_MAX_MB_PARTITIONS 4U
#define CAVS_MB_LUMA_4X4_BLOCKS 16U
#define CAVS_MB_CHROMA_4X4_BLOCKS 8U
#define CAVS_MB_4X4_BLOCKS \
    (CAVS_MB_LUMA_4X4_BLOCKS + CAVS_MB_CHROMA_4X4_BLOCKS)
#define CAVS_MB_8X8_BLOCKS 6U
#define CAVS_MB_MOTION_BLOCKS 4U
#define CAVS_MB_DIRECTIONS 2U

typedef enum cavs_macroblock_type {
    CAVS_MB_I_8X8 = 0,
    CAVS_MB_P_SKIP,
    CAVS_MB_P_16X16,
    CAVS_MB_P_16X8,
    CAVS_MB_P_8X16,
    CAVS_MB_P_8X8,
    CAVS_MB_B_SKIP,
    CAVS_MB_B_DIRECT,
    CAVS_MB_B_INTER,
    CAVS_MB_INVALID
} cavs_macroblock_type;

typedef enum cavs_prediction_direction {
    CAVS_PRED_FORWARD = 0,
    CAVS_PRED_BACKWARD = 1,
    CAVS_PRED_SYMMETRIC = 2,
    CAVS_PRED_BIDIRECTIONAL = 3,
    CAVS_PRED_INTRA = 4
} cavs_prediction_direction;

typedef struct cavs_motion_vector {
    int32_t x;
    int32_t y;
    int8_t reference_index;
    uint8_t valid;
    /*
     * GB/T 20090.16-2016 9.6.1 and 9.9.1 b) require Direct prediction to
     * recover DistanceIndexRef and the physical field selected by a stored
     * co-located P motion vector. reference_index is local to the P field's
     * decode-time list, so the resolved identity is frozen with the vector.
     */
    uint16_t reference_distance_index;
    uint8_t reference_field;
    uint8_t reference_identity_valid;
} cavs_motion_vector;

typedef struct cavs_mb_partition {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
    cavs_prediction_direction direction;
    cavs_motion_vector motion[CAVS_MB_DIRECTIONS];
} cavs_mb_partition;

/*
 * Unified decoded macroblock contract. GB/T 20090.16-2016 7.5-7.6 and
 * 9.2-9.6 define syntax through residual decoding; GB/T 20090.2-2013 uses
 * the same reconstruction-facing representation for shared tools. Storage
 * and indexing fields do not introduce codec decisions.
 */
typedef struct cavs_macroblock {
    uint32_t address;
    uint16_t row;
    uint16_t column;
    uint16_t slice_id;
    cavs_macroblock_type type;
    uint8_t raw_type;
    uint8_t is_intra;
    uint8_t is_skipped;
    /* GB/T 20090.16-2016 7.4.11/9.3 WeightingPrediction state. */
    uint8_t weighting_prediction;
    uint8_t transform_8x8;
    uint8_t partition_count;
    cavs_mb_partition partition[CAVS_MAX_MB_PARTITIONS];
    uint8_t intra_luma_mode[4];
    uint8_t intra_chroma_mode;
    uint32_t coded_block_pattern;
    int8_t qp_delta;
    uint8_t qp;
    int16_t coefficients_4x4[CAVS_MB_4X4_BLOCKS][16];
    int16_t coefficients_8x8[CAVS_MB_8X8_BLOCKS][64];
    uint8_t coefficient_count_4x4[CAVS_MB_4X4_BLOCKS];
    uint8_t coefficient_count_8x8[CAVS_MB_8X8_BLOCKS];
    size_t end_bit_offset;
} cavs_macroblock;

#endif
