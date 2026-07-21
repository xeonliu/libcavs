/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Baseline-profile macroblock syntax parsed before transform coefficients.
 */
#ifndef CAVS_MACROBLOCK_H
#define CAVS_MACROBLOCK_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_MAX_MB_PARTITIONS 4U

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

/*
 * Parses the basic-entropy, 8x8-transform subset of GB/T 20090.2-2013
 * Table 27 for baseline-profile YUV420. Transform coefficient syntax starts
 * at header.end_bit_offset.
 */
cavs_result cavs_parse_baseline420_mb_header(
    const uint8_t *data, size_t bit_size, size_t bit_offset,
    const cavs_baseline420_mb_context *context,
    cavs_baseline420_mb_header *header);

#endif
