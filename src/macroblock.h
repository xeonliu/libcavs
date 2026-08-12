/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Baseline-profile macroblock syntax parsed before transform coefficients.
 */
#ifndef CAVS_MACROBLOCK_H
#define CAVS_MACROBLOCK_H

#include <cavs/cavs.h>
#include "coefficients.h"
#include "prediction.h"
#include "reconstruction.h"
#include <stddef.h>
#include <stdint.h>

#define CAVS_MAX_MB_PARTITIONS 4U
#define CAVS_BASELINE420_MB_BLOCKS 6U
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

typedef struct cavs_baseline420_mb_context {
    uint8_t profile_id;
    cavs_pixel_format format;
    cavs_picture_type picture_type;
    uint8_t picture_structure;
    uint8_t skip_mode_flag;
    uint8_t picture_reference_flag;
    uint8_t fixed_qp;
    uint8_t previous_qp;
    uint8_t reference_index_bits;
    uint8_t mb_weighting_flag;
    uint32_t macroblock_index;
    uint32_t macroblock_width;
    uint32_t macroblock_height;
} cavs_baseline420_mb_context;

typedef struct cavs_baseline420_mb_header {
    uint32_t raw_type;
    uint8_t type_index;
    uint8_t is_intra;
    uint8_t is_skipped;
    uint8_t partition_type[CAVS_MAX_MB_PARTITIONS];
    uint8_t motion_vector_count;
    uint8_t prediction_mode_flag[4];
    uint8_t intra_luma_prediction_mode[4];
    uint8_t intra_chroma_prediction_mode;
    uint8_t reference_index[CAVS_MAX_MB_PARTITIONS];
    int32_t motion_vector_difference_x[CAVS_MAX_MB_PARTITIONS];
    int32_t motion_vector_difference_y[CAVS_MAX_MB_PARTITIONS];
    uint8_t weighting_prediction;
    uint8_t coded_block_pattern;
    int8_t qp_delta;
    uint8_t qp;
    size_t end_bit_offset;
} cavs_baseline420_mb_header;

typedef struct cavs_baseline420_macroblock {
    cavs_baseline420_mb_header header;
    cavs_basic_coefficients block[CAVS_BASELINE420_MB_BLOCKS];
    uint8_t block_coded[CAVS_BASELINE420_MB_BLOCKS];
    size_t end_bit_offset;
} cavs_baseline420_macroblock;

/*
 * Parses the basic-entropy, 8x8-transform subset of GB/T 20090.2-2013
 * Table 27 for baseline-profile YUV420. Transform coefficient syntax starts
 * at header.end_bit_offset.
 */
cavs_result cavs_parse_baseline420_mb_header(
    const uint8_t *data, size_t bit_size, size_t bit_offset,
    const cavs_baseline420_mb_context *context,
    cavs_baseline420_mb_header *header);

/* Decodes the header and every CBP-selected 8x8 coefficient block. */
cavs_result cavs_decode_baseline420_macroblock(
    const uint8_t *data, size_t bit_size, size_t bit_offset,
    const cavs_baseline420_mb_context *context,
    cavs_baseline420_macroblock *macroblock);

/*
 * Reconstructs the six basic-entropy blocks from a decoded macroblock. The
 * forward and reconstructed buffers contain six consecutive 64-byte blocks;
 * backward may be NULL for single-prediction reconstruction. Block order is
 * four luma blocks followed by Cb and Cr.
 */
cavs_result cavs_reconstruct_baseline420_macroblock(
    const cavs_baseline420_macroblock *macroblock,
    cavs_scan_mode_8x8 scan_mode,
    const uint8_t *forward,
    const uint8_t *backward,
    uint8_t *reconstructed);

/* Reconstructs one 8x8 block using the same baseline quantization path. */
cavs_result cavs_reconstruct_baseline420_block(
    const cavs_basic_coefficients *block, uint8_t coded, uint8_t qp,
    cavs_scan_mode_8x8 scan_mode, const uint8_t *forward,
    const uint8_t *backward, uint8_t reconstructed[64]);

/*
 * Resolves 9.4.4 macroblock intra modes and produces six prediction blocks.
 * luma_references contains four consecutive 8x8 reference sets and
 * chroma_references contains Cb followed by Cr. predicted_luma_modes holds
 * the mode predicted from already-decoded neighboring blocks for each luma
 * block. The output contains six consecutive 64-byte blocks.
 */
cavs_result cavs_predict_baseline420_intra_macroblock(
    const cavs_baseline420_macroblock *macroblock,
    const cavs_intra_references_8x8 *luma_references,
    const cavs_intra_references_8x8 *chroma_references,
    const uint8_t *predicted_luma_modes,
    uint8_t *forward);

#endif
