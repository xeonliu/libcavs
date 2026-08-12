/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.5-7.6, 8.2 and 9.1 basic entropy for profile 0x48.
 * This is a profile-specific syntax adapter; it does not reuse baseline
 * picture control flow or the advanced-entropy arithmetic decoder.
 */
#include "codec/broadcast_basic_macroblock.h"
#include "bitreader.h"
#include "codec/coefficients.h"
#include <limits.h>
#include <string.h>

/* GB/T 20090.16-2016 8.2, Table 42. */
static const uint8_t cbp_intra[64] = {
    63, 15, 31, 47, 0, 14, 13, 11, 7, 5, 10, 8, 12, 61, 4, 55,
    1, 2, 59, 3, 62, 9, 6, 29, 45, 51, 23, 39, 27, 46, 53, 30,
    43, 37, 60, 16, 21, 28, 19, 35, 42, 26, 44, 32, 58, 24, 20, 17,
    18, 48, 22, 33, 25, 49, 40, 36, 34, 50, 52, 54, 41, 56, 38, 57
};

static const uint8_t cbp_inter[64] = {
    0, 15, 63, 31, 16, 32, 47, 13, 14, 11, 12, 5, 10, 7, 48, 3,
    2, 8, 4, 1, 61, 55, 59, 62, 29, 27, 23, 19, 30, 28, 9, 6,
    60, 21, 44, 26, 51, 35, 18, 20, 24, 53, 17, 37, 39, 45, 58, 43,
    42, 46, 36, 33, 34, 40, 52, 49, 50, 56, 25, 22, 54, 57, 41, 38
};

static uint8_t predicted_intra_mode(
    const cavs_broadcast_basic_mb_context *context,
    const cavs_macroblock *current, unsigned block);

static int read_motion_difference(cavs_bitreader *reader, int32_t *value) {
    return cavs_br_read_se(reader, value) && *value >= -4096 && *value <= 4095;
}

static void set_p_partitions(uint8_t type_index, cavs_mb_partition part[4],
                             uint8_t *count) {
    uint8_t index;
    uint8_t partition_count = type_index == 1U ? 1U :
        (type_index == 2U || type_index == 3U ? 2U :
         (type_index == 4U ? 4U : 0U));
    memset(part, 0, sizeof(cavs_mb_partition) * 4U);
    for (index = 0U; index < partition_count; ++index) {
        part[index].x = type_index == 3U ? (uint8_t)(index * 8U) :
            (type_index == 4U ? (uint8_t)((index & 1U) * 8U) : 0U);
        part[index].y = type_index == 2U ? (uint8_t)(index * 8U) :
            (type_index == 4U ? (uint8_t)((index >> 1U) * 8U) : 0U);
        part[index].width = type_index == 3U || type_index == 4U ? 8U : 16U;
        part[index].height = type_index == 2U || type_index == 4U ? 8U : 16U;
        part[index].direction = CAVS_PRED_FORWARD;
    }
    *count = partition_count;
}

static cavs_result set_b_partitions(uint8_t type_index,
                                    const uint8_t subtypes[4],
                                    cavs_mb_partition part[4], uint8_t *count) {
    static const cavs_prediction_direction pair_direction[9][2] = {
        { CAVS_PRED_FORWARD, CAVS_PRED_FORWARD },
        { CAVS_PRED_BACKWARD, CAVS_PRED_BACKWARD },
        { CAVS_PRED_FORWARD, CAVS_PRED_BACKWARD },
        { CAVS_PRED_BACKWARD, CAVS_PRED_FORWARD },
        { CAVS_PRED_FORWARD, CAVS_PRED_SYMMETRIC },
        { CAVS_PRED_BACKWARD, CAVS_PRED_SYMMETRIC },
        { CAVS_PRED_SYMMETRIC, CAVS_PRED_FORWARD },
        { CAVS_PRED_SYMMETRIC, CAVS_PRED_BACKWARD },
        { CAVS_PRED_SYMMETRIC, CAVS_PRED_SYMMETRIC }
    };
    uint8_t index;
    memset(part, 0, sizeof(cavs_mb_partition) * 4U);
    if (type_index <= 1U) {
        *count = 1U;
        part[0].width = 16U;
        part[0].height = 16U;
        part[0].direction = CAVS_PRED_BIDIRECTIONAL;
        return CAVS_OK;
    }
    if (type_index <= 4U) {
        *count = 1U;
        part[0].width = 16U;
        part[0].height = 16U;
        part[0].direction = type_index == 2U ? CAVS_PRED_FORWARD :
            (type_index == 3U ? CAVS_PRED_BACKWARD : CAVS_PRED_SYMMETRIC);
        return CAVS_OK;
    }
    if (type_index <= 22U) {
        unsigned pair = (unsigned)(type_index - 5U) / 2U;
        int vertical = (type_index & 1U) == 0U;
        *count = 2U;
        for (index = 0U; index < 2U; ++index) {
            part[index].x = vertical ? (uint8_t)(index * 8U) : 0U;
            part[index].y = vertical ? 0U : (uint8_t)(index * 8U);
            part[index].width = vertical ? 8U : 16U;
            part[index].height = vertical ? 16U : 8U;
            part[index].direction = pair_direction[pair][index];
        }
        return CAVS_OK;
    }
    if (type_index == 23U) {
        *count = 4U;
        for (index = 0U; index < 4U; ++index) {
            if (subtypes[index] > 3U) return CAVS_ERR_CORRUPT_BITSTREAM;
            part[index].x = (uint8_t)((index & 1U) * 8U);
            part[index].y = (uint8_t)((index >> 1U) * 8U);
            part[index].width = 8U;
            part[index].height = 8U;
            part[index].direction = subtypes[index] == 0U ?
                CAVS_PRED_BIDIRECTIONAL :
                (subtypes[index] == 1U ? CAVS_PRED_FORWARD :
                 (subtypes[index] == 2U ? CAVS_PRED_BACKWARD :
                                          CAVS_PRED_SYMMETRIC));
        }
        return CAVS_OK;
    }
    *count = 0U;
    return type_index == 24U ? CAVS_OK : CAVS_ERR_CORRUPT_BITSTREAM;
}

/*
 * GB/T 20090.16-2016 9.4.6 and Tables 56-57: Basic entropy carries MVDs
 * only for the coded forward/backward component of a B partition. Direct
 * partitions and the backward component derived by symmetric prediction do
 * not have an MVD in the bitstream.
 */
static int b_motion_present(uint8_t type_index, uint8_t subtype,
                            cavs_prediction_direction partition_direction,
                            cavs_prediction_direction direction) {
    if (type_index <= 1U) return 0;
    if (type_index == 23U) {
        return (subtype == 1U && direction == CAVS_PRED_FORWARD) ||
               (subtype == 2U && direction == CAVS_PRED_BACKWARD) ||
               (subtype == 3U && direction == CAVS_PRED_FORWARD);
    }
    if (direction == CAVS_PRED_FORWARD)
        return partition_direction == CAVS_PRED_FORWARD ||
               partition_direction == CAVS_PRED_SYMMETRIC ||
               partition_direction == CAVS_PRED_BIDIRECTIONAL;
    return partition_direction == CAVS_PRED_BACKWARD ||
           partition_direction == CAVS_PRED_BIDIRECTIONAL;
}

static int parse_type(cavs_bitreader *reader,
                      const cavs_broadcast_basic_mb_context *context,
                      uint8_t *raw_type, uint8_t *type_index,
                      uint32_t *embedded_cbp) {
    uint32_t raw;
    uint32_t adjusted;
    uint32_t boundary = context->picture_type == CAVS_PICTURE_B ? 24U : 5U;
    uint64_t total = (uint64_t)context->macroblock_width *
        context->macroblock_height;
    int implicit_i = context->picture_type == CAVS_PICTURE_I &&
        (context->picture_structure != 0U ||
         (uint64_t)context->macroblock_index < total / 2U);
    if (implicit_i) {
        *raw_type = 0U;
        *type_index = 5U;
        *embedded_cbp = 0U;
        return 1;
    }
    if (!cavs_br_read_ue(reader, &raw) || raw > UINT32_MAX -
        context->skip_mode_flag)
        return 0;
    adjusted = raw + context->skip_mode_flag;
    *embedded_cbp = UINT32_MAX;
    if (adjusted >= boundary) {
        *embedded_cbp = adjusted - boundary;
        if (*embedded_cbp >= 64U) return 0;
        adjusted = boundary;
    }
    if (adjusted > boundary) return 0;
    *raw_type = raw > UINT8_MAX ? UINT8_MAX : (uint8_t)raw;
    *type_index = (uint8_t)adjusted;
    return 1;
}

static int parse_intra(cavs_bitreader *reader,
                       const cavs_broadcast_basic_mb_context *context,
                       cavs_macroblock *macroblock) {
    unsigned index;
    uint32_t value;
    uint8_t predicted;
    uint8_t coded;
    for (index = 0U; index < 4U; ++index) {
        if (!cavs_br_read(reader, 1U, &value)) return 0;
        predicted = predicted_intra_mode(context, macroblock, index);
        if (value == 0U) {
            if (!cavs_br_read(reader, 2U, &value)) return 0;
            coded = (uint8_t)value;
            macroblock->intra_luma_mode[index] = coded < predicted ? coded :
                (uint8_t)(coded + 1U);
        } else {
            macroblock->intra_luma_mode[index] = predicted;
        }
    }
    if (!cavs_br_read_ue(reader, &value) || value > 3U) return 0;
    macroblock->intra_chroma_mode = (uint8_t)value;
    return 1;
}

static uint8_t predicted_intra_mode(const cavs_broadcast_basic_mb_context *context,
                                    const cavs_macroblock *current,
                                    unsigned block) {
    int left = -1;
    int top = -1;
    if ((block & 1U) != 0U) left = current->intra_luma_mode[block - 1U];
    else if (context->left != NULL && context->left->is_intra != 0U)
        left = context->left->intra_luma_mode[block + 1U];
    if (block >= 2U) top = current->intra_luma_mode[block - 2U];
    else if (context->top != NULL && context->top->is_intra != 0U)
        top = context->top->intra_luma_mode[block + 2U];
    return left < 0 || top < 0 ? 2U : (uint8_t)(left < top ? left : top);
}

static cavs_result read_reference(cavs_bitreader *reader, uint8_t maximum,
                                  uint8_t *reference) {
    uint32_t value;
    if (!cavs_br_read(reader, maximum > 1U ? 2U : 1U, &value) ||
        value > maximum) return CAVS_ERR_CORRUPT_BITSTREAM;
    *reference = (uint8_t)value;
    return CAVS_OK;
}

static cavs_result decode_one_coefficients(
    const uint8_t *data, size_t bit_size, size_t *offset, int intra, int chroma,
    int field, int16_t matrix[64], uint8_t *count) {
    cavs_basic_coefficients parsed;
    int32_t inverse[64];
    cavs_result result = cavs_decode_basic_coefficients_8x8(
        data, bit_size, *offset, chroma ? CAVS_BASIC_CHROMA :
                                          (intra ? CAVS_BASIC_INTRA_LUMA :
                                                   CAVS_BASIC_INTER_LUMA),
        &parsed);
    if (result != CAVS_OK) return result;
    result = cavs_inverse_scan_8x8(parsed.scan_coefficients,
                                    field ? CAVS_SCAN_8X8_FIELD :
                                             CAVS_SCAN_8X8_FRAME, inverse);
    if (result != CAVS_OK) return result;
    {
        unsigned index;
        for (index = 0U; index < 64U; ++index) {
            if (inverse[index] < INT16_MIN || inverse[index] > INT16_MAX)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            matrix[index] = (int16_t)inverse[index];
        }
    }
    *count = parsed.count;
    *offset = parsed.end_bit_offset;
    return CAVS_OK;
}

cavs_result cavs_broadcast_basic_init(
    cavs_broadcast_basic_decoder *decoder, const uint8_t *data,
    size_t bit_size, size_t bit_offset) {
    if (decoder == NULL || (data == NULL && bit_size != 0U) ||
        bit_offset > bit_size) return CAVS_ERR_INVALID_ARGUMENT;
    decoder->data = data;
    decoder->bit_size = bit_size;
    decoder->bit_offset = bit_offset;
    return CAVS_OK;
}

cavs_result cavs_broadcast_basic_skip_run(
    cavs_broadcast_basic_decoder *decoder, uint32_t maximum,
    uint32_t *skip_run) {
    cavs_bitreader reader;
    uint32_t parsed;
    if (decoder == NULL || skip_run == NULL ||
        !cavs_br_init_bits(&reader, decoder->data, decoder->bit_size))
        return CAVS_ERR_INVALID_ARGUMENT;
    reader.bit_pos = decoder->bit_offset;
    if (!cavs_br_read_ue(&reader, &parsed) || parsed > maximum)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    decoder->bit_offset = reader.bit_pos;
    *skip_run = parsed;
    return CAVS_OK;
}

cavs_result cavs_broadcast_basic_finish(
    const cavs_broadcast_basic_decoder *decoder) {
    if (decoder == NULL || decoder->bit_offset > decoder->bit_size)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    return CAVS_EOF;
}

cavs_result cavs_decode_broadcast_basic_macroblock(
    cavs_broadcast_basic_decoder *decoder,
    const cavs_broadcast_basic_mb_context *context, cavs_macroblock *macroblock) {
    cavs_broadcast_basic_decoder working;
    cavs_bitreader reader;
    cavs_macroblock parsed;
    uint32_t embedded_cbp;
    uint32_t cbp_code;
    uint32_t qp_value;
    int32_t qp_delta;
    uint8_t type_index;
    uint8_t raw_type;
    uint8_t subtypes[4] = { 0U, 0U, 0U, 0U };
    uint8_t partition_count;
    unsigned index;
    int intra;
    int field_scan;
    if (decoder == NULL || context == NULL || macroblock == NULL ||
        context->profile_id != UINT8_C(0x48) || context->format != CAVS_YUV420P8 ||
        context->picture_type > CAVS_PICTURE_B || context->progressive_frame > 1U ||
        context->picture_structure > 1U || context->skip_mode_flag > 1U ||
        context->picture_reference_flag > 1U || context->fixed_qp > 1U ||
        context->previous_qp > 63U || context->previous_qp_delta < -32 ||
        context->previous_qp_delta > 31 || context->slice_weighting_flag > 1U ||
        context->mb_weighting_flag > 1U || context->macroblock_width == 0U ||
        context->macroblock_height == 0U ||
        context->macroblock_index >= context->macroblock_width *
                                     context->macroblock_height)
        return CAVS_ERR_INVALID_ARGUMENT;
    working = *decoder;
    if (!cavs_br_init_bits(&reader, working.data, working.bit_size))
        return CAVS_ERR_INVALID_ARGUMENT;
    reader.bit_pos = working.bit_offset;
    memset(&parsed, 0, sizeof(parsed));
    parsed.address = context->macroblock_index;
    parsed.row = (uint16_t)(parsed.address / context->macroblock_width);
    parsed.column = (uint16_t)(parsed.address % context->macroblock_width);
    parsed.slice_id = context->slice_id;
    parsed.transform_8x8 = 1U;
    parsed.qp = context->previous_qp;

    if (!parse_type(&reader, context, &raw_type, &type_index, &embedded_cbp))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.raw_type = raw_type;
    intra = (context->picture_type != CAVS_PICTURE_B && type_index == 5U) ||
            (context->picture_type == CAVS_PICTURE_B && type_index == 24U);
    parsed.is_intra = (uint8_t)intra;
    /* GB/T 20090.16-2016 9.1: Basic has no macroblock weighting bit; the
     * normalized value follows the slice default when mb_weighting_flag is 0.
     */
    parsed.weighting_prediction = (uint8_t)(!intra &&
        context->slice_weighting_flag != 0U &&
        context->mb_weighting_flag == 0U);
    if (context->picture_type != CAVS_PICTURE_B) {
        parsed.type = intra ? CAVS_MB_I_8X8 :
            (type_index == 0U ? CAVS_MB_P_SKIP :
             (type_index == 1U ? CAVS_MB_P_16X16 :
              (type_index == 2U ? CAVS_MB_P_16X8 :
               (type_index == 3U ? CAVS_MB_P_8X16 : CAVS_MB_P_8X8))));
        parsed.is_skipped = (uint8_t)(type_index == 0U);
        set_p_partitions(type_index, parsed.partition, &partition_count);
    } else {
        parsed.type = intra ? CAVS_MB_I_8X8 :
            (type_index == 0U ? CAVS_MB_B_SKIP :
             (type_index == 1U ? CAVS_MB_B_DIRECT : CAVS_MB_B_INTER));
        parsed.is_skipped = (uint8_t)(type_index == 0U);
        if (type_index == 23U) {
            for (index = 0U; index < 4U; ++index) {
                if (!cavs_br_read(&reader, 2U, &qp_value) || qp_value > 3U)
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                subtypes[index] = (uint8_t)qp_value;
            }
        }
        if (set_b_partitions(type_index, subtypes, parsed.partition,
                             &partition_count) != CAVS_OK)
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    parsed.partition_count = partition_count;
    if (parsed.is_skipped != 0U) {
        parsed.end_bit_offset = reader.bit_pos;
        working.bit_offset = reader.bit_pos;
        *decoder = working;
        *macroblock = parsed;
        return CAVS_OK;
    }
    if (intra && !parse_intra(&reader, context, &parsed))
        return CAVS_ERR_CORRUPT_BITSTREAM;

    /* GB/T 20090.16-2016 7.5.7 and 9.5: when picture_reference_flag is zero,
     * P and B macroblocks carry reference indices. A field-structured picture
     * uses the 2-bit 0..3 form; the frame-structured form uses 1 bit. */
    if (!intra && context->picture_reference_flag == 0U &&
        (context->picture_type == CAVS_PICTURE_P ||
         context->picture_type == CAVS_PICTURE_B)) {
        uint8_t maximum = context->picture_type == CAVS_PICTURE_P &&
            context->picture_structure == 0U ? 3U : 1U;
        unsigned direction;
        for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
            for (index = 0U; index < partition_count; ++index) {
                int has_motion = context->picture_type == CAVS_PICTURE_B ?
                    b_motion_present(type_index, subtypes[index],
                                     parsed.partition[index].direction,
                                     (cavs_prediction_direction)direction) :
                    direction == CAVS_PRED_FORWARD;
                if (has_motion) {
                    uint8_t reference;
                    cavs_result result = read_reference(&reader, maximum, &reference);
                    if (result != CAVS_OK) return result;
                    parsed.partition[index].motion[direction].reference_index =
                        (int8_t)reference;
                    parsed.partition[index].motion[direction].valid = 1U;
                }
            }
        }
    }
    {
        unsigned direction;
        for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
            for (index = 0U; index < partition_count; ++index) {
                cavs_motion_vector *motion =
                    &parsed.partition[index].motion[direction];
                int has_motion = context->picture_type == CAVS_PICTURE_B ?
                    b_motion_present(type_index, subtypes[index],
                                     parsed.partition[index].direction,
                                     (cavs_prediction_direction)direction) :
                    direction == CAVS_PRED_FORWARD;
                if (!has_motion) continue;
                if (context->picture_reference_flag != 0U)
                    motion->reference_index = 0;
                if (!read_motion_difference(&reader, &motion->x) ||
                    !read_motion_difference(&reader, &motion->y))
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                motion->valid = 1U;
            }
        }
    }
    /*
     * GB/T 20090.16-2016 7.5.9 and 8.4: weighting_prediction is an AEC
     * syntax element. Basic entropy has no corresponding macroblock bit;
     * leave the normalized field at its default zero and do not consume the
     * next bit before Table 42 CBP.
     */
    if (embedded_cbp < 64U) {
        parsed.coded_block_pattern = intra ? cbp_intra[embedded_cbp] :
                                             cbp_inter[embedded_cbp];
    } else {
        if (!cavs_br_read_ue(&reader, &cbp_code) || cbp_code >= 64U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.coded_block_pattern = intra ? cbp_intra[cbp_code] :
                                             cbp_inter[cbp_code];
    }
    if (parsed.coded_block_pattern != 0U && context->fixed_qp == 0U) {
        if (!cavs_br_read_se(&reader, &qp_delta) || qp_delta < -32 ||
            qp_delta > 31 || (int32_t)context->previous_qp + qp_delta < 0 ||
            (int32_t)context->previous_qp + qp_delta > 63)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.qp_delta = (int8_t)qp_delta;
        parsed.qp = (uint8_t)((int32_t)context->previous_qp + qp_delta);
    }
    field_scan = context->progressive_frame == 0U && context->picture_structure == 0U;
    for (index = 0U; index < CAVS_MB_8X8_BLOCKS; ++index) {
        if ((parsed.coded_block_pattern & (UINT32_C(1) << index)) == 0U)
            continue;
        if (decode_one_coefficients(working.data, working.bit_size,
                                    &reader.bit_pos, intra, index >= 4U,
                                    field_scan, parsed.coefficients_8x8[index],
                                    &parsed.coefficient_count_8x8[index]) != CAVS_OK)
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    parsed.end_bit_offset = reader.bit_pos;
    working.bit_offset = reader.bit_pos;
    *decoder = working;
    *macroblock = parsed;
    return CAVS_OK;
}
