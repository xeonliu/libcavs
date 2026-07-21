/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 7.1.3.7 Table 27 and 9.2 Tables 59, 61, and 62. This module is
 * intentionally limited to baseline-profile YUV420 basic entropy and 8x8
 * transforms; the advanced-entropy and VBS paths have different syntax.
 */
#include "macroblock.h"
#include "bitreader.h"
#include "safe.h"
#include <string.h>

/* Independently transcribed intra/inter columns of Table 45. */
static const uint8_t cbp420_intra[64] = {
    63, 15, 31, 47, 0, 14, 13, 11, 7, 5, 10, 8, 12, 61, 4, 55,
    1, 2, 59, 3, 62, 9, 6, 29, 45, 51, 23, 39, 27, 46, 53, 30,
    43, 37, 60, 16, 21, 28, 19, 35, 42, 26, 44, 32, 58, 24, 20, 17,
    18, 48, 22, 33, 25, 49, 40, 36, 34, 50, 52, 54, 41, 56, 38, 57
};

static const uint8_t cbp420_inter[64] = {
    0, 15, 63, 31, 16, 32, 47, 13, 14, 11, 12, 5, 10, 7, 48, 3,
    2, 8, 4, 1, 61, 55, 59, 62, 29, 27, 23, 19, 30, 28, 9, 6,
    60, 21, 44, 26, 51, 35, 18, 20, 24, 53, 17, 37, 39, 45, 58, 43,
    42, 46, 36, 33, 34, 40, 52, 49, 50, 56, 25, 22, 54, 57, 41, 38
};

/* Reads a signed motion component and applies the baseline quarter-pel range. */
static int read_motion_difference(cavs_bitreader *reader, int32_t *value) {
    return cavs_br_read_se(reader, value) && *value >= -4096 && *value <= 4095;
}

/* Derives Table 59 motion-vector count for a P-family macroblock. */
static uint8_t p_motion_vector_count(uint8_t type_index) {
    static const uint8_t counts[6] = { 0, 1, 2, 2, 4, 0 };
    return counts[type_index];
}

/* Derives Table 61 motion-vector count outside the B_8x8 special case. */
static uint8_t b_motion_vector_count(uint8_t type_index) {
    if (type_index >= 2U && type_index <= 4U) return 1U;
    if (type_index >= 5U && type_index <= 22U) return 2U;
    return 0U;
}

/* Parses and normalizes the basic-entropy mb_type syntax from 9.2. */
static int parse_type(cavs_bitreader *reader,
                      const cavs_baseline420_mb_context *context,
                      cavs_baseline420_mb_header *parsed,
                      uint32_t macroblock_count, uint32_t *embedded_cbp) {
    uint32_t adjusted;
    uint32_t boundary;
    int implicit_i = context->picture_type == CAVS_PICTURE_I &&
        (context->picture_structure != 0U ||
         context->macroblock_index < macroblock_count / 2U);

    if (implicit_i) {
        parsed->type_index = 5U;
        parsed->is_intra = 1U;
        *embedded_cbp = 0U;
        return 1;
    }
    if (!cavs_br_read_ue(reader, &parsed->raw_type)) return 0;
    if (parsed->raw_type > UINT32_MAX - context->skip_mode_flag) return 0;
    adjusted = parsed->raw_type + context->skip_mode_flag;
    boundary = context->picture_type == CAVS_PICTURE_B ? 24U : 5U;
    if (adjusted >= boundary) {
        *embedded_cbp = adjusted - boundary;
        if (*embedded_cbp >= 64U) return 0;
        adjusted = boundary;
    }
    parsed->type_index = (uint8_t)adjusted;
    parsed->is_intra = (uint8_t)(adjusted == boundary);
    parsed->is_skipped = (uint8_t)(adjusted == 0U);
    return 1;
}

/* Parses Table 27 fields specific to I_8x8 macroblocks. */
static int parse_intra_prediction(cavs_bitreader *reader,
                                  cavs_baseline420_mb_header *parsed) {
    uint32_t value;
    unsigned index;
    for (index = 0U; index < 4U; ++index) {
        if (!cavs_br_read(reader, 1U, &value)) return 0;
        parsed->prediction_mode_flag[index] = (uint8_t)value;
        if (value == 0U) {
            if (!cavs_br_read(reader, 2U, &value)) return 0;
            parsed->intra_luma_prediction_mode[index] = (uint8_t)value;
        }
    }
    if (!cavs_br_read_ue(reader, &value) || value > 3U) return 0;
    parsed->intra_chroma_prediction_mode = (uint8_t)value;
    return 1;
}

cavs_result cavs_parse_baseline420_mb_header(
    const uint8_t *data, size_t bit_size, size_t bit_offset,
    const cavs_baseline420_mb_context *context,
    cavs_baseline420_mb_header *header) {
    cavs_bitreader reader;
    cavs_baseline420_mb_header parsed;
    size_t macroblock_count_size;
    uint32_t macroblock_count;
    uint32_t embedded_cbp = UINT32_MAX;
    uint32_t cbp_code;
    uint32_t value;
    int32_t qp_delta;
    unsigned index;
    int has_reference_indices;

    if ((data == NULL && bit_size != 0U) || context == NULL || header == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (context->profile_id != UINT8_C(0x20))
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    if (context->format != CAVS_YUV420P8 || context->picture_structure > 1U ||
        context->picture_type > CAVS_PICTURE_B ||
        context->skip_mode_flag > 1U || context->picture_reference_flag > 1U ||
        context->fixed_qp > 1U || context->previous_qp > 63U ||
        context->mb_weighting_flag > 1U || context->macroblock_width == 0U ||
        context->macroblock_height == 0U || bit_offset > bit_size)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (!cavs_size_mul(context->macroblock_width, context->macroblock_height,
                       &macroblock_count_size) ||
        macroblock_count_size > UINT32_MAX ||
        context->macroblock_index >= macroblock_count_size ||
        !cavs_br_init_bits(&reader, data, bit_size))
        return CAVS_ERR_INVALID_ARGUMENT;
    macroblock_count = (uint32_t)macroblock_count_size;
    reader.bit_pos = bit_offset;
    memset(&parsed, 0, sizeof(parsed));
    parsed.qp = context->previous_qp;

    if (!parse_type(&reader, context, &parsed, macroblock_count, &embedded_cbp))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (parsed.is_skipped != 0U) {
        parsed.end_bit_offset = reader.bit_pos;
        *header = parsed;
        return CAVS_OK;
    }

    if (context->picture_type == CAVS_PICTURE_B && parsed.type_index == 23U) {
        for (index = 0U; index < 4U; ++index) {
            if (!cavs_br_read(&reader, 2U, &value))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.partition_type[index] = (uint8_t)value;
            if (value != 0U) ++parsed.motion_vector_count;
        }
    } else if (context->picture_type == CAVS_PICTURE_B) {
        parsed.motion_vector_count = b_motion_vector_count(parsed.type_index);
    } else {
        parsed.motion_vector_count = p_motion_vector_count(parsed.type_index);
    }

    if (parsed.is_intra != 0U && !parse_intra_prediction(&reader, &parsed))
        return CAVS_ERR_CORRUPT_BITSTREAM;

    has_reference_indices =
        (context->picture_type == CAVS_PICTURE_P ||
         (context->picture_type == CAVS_PICTURE_B && context->picture_structure == 0U)) &&
        context->picture_reference_flag == 0U;
    if (has_reference_indices && context->reference_index_bits != 1U &&
        context->reference_index_bits != 2U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (has_reference_indices) {
        for (index = 0U; index < parsed.motion_vector_count; ++index) {
            if (!cavs_br_read(&reader, context->reference_index_bits, &value))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.reference_index[index] = (uint8_t)value;
        }
    }
    for (index = 0U; index < parsed.motion_vector_count; ++index) {
        if (!read_motion_difference(&reader, &parsed.motion_vector_difference_x[index]) ||
            !read_motion_difference(&reader, &parsed.motion_vector_difference_y[index]))
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    if (context->mb_weighting_flag != 0U && parsed.is_intra == 0U) {
        if (!cavs_br_read(&reader, 1U, &value))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.weighting_prediction = (uint8_t)value;
    }

    if (parsed.is_intra != 0U) {
        if (embedded_cbp >= 64U) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.coded_block_pattern = cbp420_intra[embedded_cbp];
    } else {
        if (!cavs_br_read_ue(&reader, &cbp_code) || cbp_code >= 64U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.coded_block_pattern = cbp420_inter[cbp_code];
    }

    if (parsed.coded_block_pattern != 0U && context->fixed_qp == 0U) {
        if (!cavs_br_read_se(&reader, &qp_delta) || qp_delta < -32 || qp_delta > 31 ||
            (int32_t)context->previous_qp + qp_delta < 0 ||
            (int32_t)context->previous_qp + qp_delta > 63)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.qp_delta = (int8_t)qp_delta;
        parsed.qp = (uint8_t)((int32_t)context->previous_qp + qp_delta);
    }
    parsed.end_bit_offset = reader.bit_pos;
    *header = parsed;
    return CAVS_OK;
}
