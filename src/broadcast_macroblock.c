/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.4-7.6, 8.3-8.4 and 9.2-9.8. The module converts
 * profile-0x48 advanced-entropy syntax directly to the frozen macroblock
 * reconstruction contract and owns no picture or decoder-global state.
 */
#include "broadcast_macroblock.h"
#include <limits.h>
#include <string.h>

#define AE_CTX_SKIP_RUN 0U
#define AE_CTX_MB_TYPE 4U
#define AE_CTX_PART_TYPE 19U
#define AE_CTX_INTRA_LUMA 22U
#define AE_CTX_INTRA_CHROMA 26U
#define AE_CTX_REFERENCE 30U
#define AE_CTX_MVD_X 36U
#define AE_CTX_MVD_Y 42U
#define AE_CTX_CBP 48U
#define AE_CTX_QP_DELTA 54U
#define AE_CTX_COEFF_FRAME_LUMA 58U
#define AE_CTX_COEFF_FRAME_CHROMA 124U
#define AE_CTX_COEFF_FIELD_LUMA 190U
#define AE_CTX_COEFF_FIELD_CHROMA 256U
#define AE_CTX_WEIGHTING 322U

typedef enum encoded_motion_kind {
    MOTION_NONE = 0,
    MOTION_FORWARD,
    MOTION_BACKWARD,
    MOTION_SYMMETRIC
} encoded_motion_kind;

typedef struct decoded_partition {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
    cavs_prediction_direction direction;
    encoded_motion_kind encoded;
} decoded_partition;

static const uint8_t frame_scan[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

static const uint8_t field_scan[64] = {
     0,  3, 11, 16, 22, 32, 38, 55,
     1,  6, 12, 20, 25, 33, 42, 57,
     2,  7, 15, 21, 28, 37, 43, 58,
     4, 10, 19, 27, 31, 39, 47, 59,
     5, 14, 24, 30, 36, 44, 50, 60,
     8, 17, 26, 35, 41, 48, 52, 61,
     9, 18, 29, 40, 46, 51, 54, 62,
    13, 23, 34, 45, 49, 53, 56, 63
};

/* Bounds checking and context indexing only; no codec decision is added. */
static cavs_result decode_context_bin(cavs_broadcast_slice_decoder *decoder,
                                      unsigned index, uint8_t *bin) {
    if (decoder == NULL || bin == NULL || index >= CAVS_AE_CONTEXT_COUNT)
        return CAVS_ERR_INVALID_ARGUMENT;
    return cavs_ae_decode(&decoder->arithmetic, &decoder->contexts[index], bin);
}

/* Decodes the Table 44 zero-prefix unary form with an explicit syntax limit. */
static cavs_result decode_zero_unary(cavs_broadcast_slice_decoder *decoder,
                                     unsigned base, unsigned saturated_index,
                                     uint32_t maximum, uint32_t *value) {
    uint32_t parsed = 0U;
    uint8_t bin;
    cavs_result result;
    if (decoder == NULL || value == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    for (;;) {
        unsigned index = parsed < saturated_index ? (unsigned)parsed :
                                                       saturated_index;
        result = decode_context_bin(decoder, base + index, &bin);
        if (result != CAVS_OK) return result;
        if (bin != 0U) break;
        if (parsed == maximum) return CAVS_ERR_CORRUPT_BITSTREAM;
        ++parsed;
    }
    *value = parsed;
    return CAVS_OK;
}

cavs_result cavs_broadcast_slice_init(cavs_broadcast_slice_decoder *decoder,
                                      const uint8_t *data, size_t bit_size,
                                      size_t bit_offset) {
    cavs_broadcast_slice_decoder parsed;
    cavs_result result;
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    memset(&parsed, 0, sizeof(parsed));
    cavs_ae_contexts_init(parsed.contexts, CAVS_AE_CONTEXT_COUNT);
    result = cavs_ae_decoder_init(&parsed.arithmetic, data, bit_size, bit_offset);
    if (result != CAVS_OK) return result;
    *decoder = parsed;
    return CAVS_OK;
}

cavs_result cavs_broadcast_decode_skip_run(
    cavs_broadcast_slice_decoder *decoder, uint32_t maximum,
    uint32_t *skip_run) {
    cavs_broadcast_slice_decoder working;
    cavs_result result;
    uint32_t parsed;
    if (decoder == NULL || skip_run == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    working = *decoder;
    result = decode_zero_unary(&working, AE_CTX_SKIP_RUN, 3U, maximum, &parsed);
    if (result != CAVS_OK) return result;
    if (parsed != 0U) {
        result = cavs_ae_decode_stuffing(&working.arithmetic,
                                         &working.last_stuffing_bit);
        if (result != CAVS_OK) return result;
        working.has_stuffing_bit = 1U;
    }
    *decoder = working;
    *skip_run = parsed;
    return CAVS_OK;
}

cavs_result cavs_broadcast_slice_finish(
    const cavs_broadcast_slice_decoder *decoder) {
    size_t offset;
    size_t remaining;
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (decoder->has_stuffing_bit == 0U ||
        decoder->last_stuffing_bit != 1U ||
        decoder->arithmetic.bit_offset > decoder->arithmetic.bit_size)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    remaining = decoder->arithmetic.bit_size - decoder->arithmetic.bit_offset;
    if (remaining > 8U) return CAVS_ERR_CORRUPT_BITSTREAM;
    for (offset = decoder->arithmetic.bit_offset;
         offset < decoder->arithmetic.bit_size; ++offset) {
        if (((decoder->arithmetic.data[offset / 8U] >>
              (7U - offset % 8U)) & UINT8_C(1)) != 0U &&
            offset != decoder->arithmetic.bit_offset)
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    return CAVS_EOF;
}

/* Table 55 geometry only; no motion prediction is performed here. */
static void set_p_partitions(uint8_t type_index, decoded_partition part[4],
                             uint8_t *count) {
    uint8_t index;
    memset(part, 0, sizeof(decoded_partition) * 4U);
    *count = type_index == 1U ? 1U :
             (type_index == 2U || type_index == 3U ? 2U :
              (type_index == 4U ? 4U : 0U));
    for (index = 0U; index < *count; ++index) {
        part[index].x = type_index == 3U ? (uint8_t)(index * 8U) :
                        (type_index == 4U ? (uint8_t)((index & 1U) * 8U) : 0U);
        part[index].y = type_index == 2U ? (uint8_t)(index * 8U) :
                        (type_index == 4U ? (uint8_t)((index >> 1U) * 8U) : 0U);
        part[index].width = (type_index == 3U || type_index == 4U) ? 8U : 16U;
        part[index].height = (type_index == 2U || type_index == 4U) ? 8U : 16U;
        part[index].direction = CAVS_PRED_FORWARD;
        part[index].encoded = MOTION_FORWARD;
    }
}

/* Tables 56-57 geometry and coded prediction directions. */
static cavs_result set_b_partitions(uint8_t type_index,
                                    const uint8_t subtypes[4],
                                    decoded_partition part[4],
                                    uint8_t *count) {
    static const encoded_motion_kind pair_direction[9][2] = {
        { MOTION_FORWARD, MOTION_FORWARD },
        { MOTION_BACKWARD, MOTION_BACKWARD },
        { MOTION_FORWARD, MOTION_BACKWARD },
        { MOTION_BACKWARD, MOTION_FORWARD },
        { MOTION_FORWARD, MOTION_SYMMETRIC },
        { MOTION_BACKWARD, MOTION_SYMMETRIC },
        { MOTION_SYMMETRIC, MOTION_FORWARD },
        { MOTION_SYMMETRIC, MOTION_BACKWARD },
        { MOTION_SYMMETRIC, MOTION_SYMMETRIC }
    };
    uint8_t index;
    memset(part, 0, sizeof(decoded_partition) * 4U);
    if (type_index <= 1U) {
        *count = 1U;
        part[0].width = 16U;
        part[0].height = 16U;
        part[0].direction = CAVS_PRED_BIDIRECTIONAL;
        return CAVS_OK;
    }
    if (type_index <= 4U) {
        encoded_motion_kind kind = type_index == 2U ? MOTION_FORWARD :
                                   (type_index == 3U ? MOTION_BACKWARD :
                                                      MOTION_SYMMETRIC);
        *count = 1U;
        part[0].width = 16U;
        part[0].height = 16U;
        part[0].encoded = kind;
    } else if (type_index <= 22U) {
        unsigned pair = (unsigned)(type_index - 5U) / 2U;
        int vertical_split = (type_index & 1U) == 0U;
        *count = 2U;
        for (index = 0U; index < 2U; ++index) {
            part[index].x = vertical_split ? (uint8_t)(index * 8U) : 0U;
            part[index].y = vertical_split ? 0U : (uint8_t)(index * 8U);
            part[index].width = vertical_split ? 8U : 16U;
            part[index].height = vertical_split ? 16U : 8U;
            part[index].encoded = pair_direction[pair][index];
        }
    } else if (type_index == 23U) {
        *count = 4U;
        for (index = 0U; index < 4U; ++index) {
            if (subtypes[index] > 3U) return CAVS_ERR_CORRUPT_BITSTREAM;
            part[index].x = (uint8_t)((index & 1U) * 8U);
            part[index].y = (uint8_t)((index >> 1U) * 8U);
            part[index].width = 8U;
            part[index].height = 8U;
            part[index].encoded = subtypes[index] == 0U ? MOTION_NONE :
                (encoded_motion_kind)subtypes[index];
        }
    } else {
        *count = 0U;
        return type_index == 24U ? CAVS_OK : CAVS_ERR_CORRUPT_BITSTREAM;
    }
    for (index = 0U; index < *count; ++index) {
        part[index].direction = part[index].encoded == MOTION_FORWARD ?
            CAVS_PRED_FORWARD :
            (part[index].encoded == MOTION_BACKWARD ? CAVS_PRED_BACKWARD :
             (part[index].encoded == MOTION_SYMMETRIC ? CAVS_PRED_SYMMETRIC :
                                                        CAVS_PRED_BIDIRECTIONAL));
    }
    return CAVS_OK;
}

static int is_skip_or_direct(const cavs_macroblock *macroblock) {
    return macroblock != NULL &&
        (macroblock->type == CAVS_MB_P_SKIP ||
         macroblock->type == CAVS_MB_B_SKIP ||
         macroblock->type == CAVS_MB_B_DIRECT);
}

static int is_available_non_skip(const cavs_macroblock *macroblock) {
    return macroblock != NULL && !is_skip_or_direct(macroblock);
}

/* GB/T 20090.16-2016 8.3 Tables 44-45 and 8.4.2 Table 51. */
static cavs_result decode_mb_type(cavs_broadcast_slice_decoder *decoder,
                                  const cavs_broadcast_mb_context *context,
                                  uint8_t *raw_type, uint8_t *type_index) {
    uint32_t symbol;
    cavs_result result;
    uint8_t bin;
    int p_family = context->picture_type != CAVS_PICTURE_B;
    int implicit_i = context->picture_type == CAVS_PICTURE_I &&
        (context->picture_structure != 0U ||
         context->macroblock_index <
             context->macroblock_width * context->macroblock_height / 2U);
    if (implicit_i) {
        *raw_type = 0U;
        *type_index = 5U;
        return CAVS_OK;
    }
    if (p_family) {
        result = decode_zero_unary(decoder, AE_CTX_MB_TYPE, 4U,
                                   context->skip_mode_flag != 0U ? 4U : 5U,
                                   &symbol);
        if (result != CAVS_OK) return result;
        *raw_type = (uint8_t)symbol;
        if (context->skip_mode_flag != 0U) {
            static const uint8_t map[5] = { 5U, 1U, 2U, 3U, 4U };
            *type_index = map[symbol];
        } else {
            static const uint8_t map[6] = { 5U, 0U, 1U, 2U, 3U, 4U };
            *type_index = map[symbol];
        }
        return CAVS_OK;
    }

    result = decode_context_bin(decoder, AE_CTX_MB_TYPE + 5U +
        (is_available_non_skip(context->left) ? 1U : 0U) +
        (is_available_non_skip(context->top) ? 1U : 0U), &bin);
    if (result != CAVS_OK) return result;
    if (bin == 0U) symbol = 0U;
    else {
        symbol = 1U;
        for (;;) {
            unsigned increment = symbol <= 7U ? (unsigned)symbol + 7U : 14U;
            result = decode_context_bin(decoder, AE_CTX_MB_TYPE + increment,
                                        &bin);
            if (result != CAVS_OK) return result;
            if (bin != 0U) break;
            if (symbol == (uint32_t)(24U - context->skip_mode_flag))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            ++symbol;
        }
    }
    *raw_type = (uint8_t)symbol;
    *type_index = (uint8_t)(symbol + context->skip_mode_flag);
    return *type_index <= 24U ? CAVS_OK : CAVS_ERR_CORRUPT_BITSTREAM;
}

/* GB/T 20090.16-2016 8.3 Table 46. */
static cavs_result decode_part_type(cavs_broadcast_slice_decoder *decoder,
                                    uint8_t *part_type) {
    uint8_t first;
    uint8_t second;
    cavs_result result = decode_context_bin(decoder, AE_CTX_PART_TYPE, &first);
    if (result != CAVS_OK) return result;
    result = decode_context_bin(decoder, AE_CTX_PART_TYPE +
        (first == 0U ? 1U : 2U), &second);
    if (result != CAVS_OK) return result;
    *part_type = (uint8_t)((first << 1U) | second);
    return CAVS_OK;
}

/* GB/T 20090.16-2016 8.3 Table 47 and 9.4.2. */
static cavs_result decode_intra_luma(cavs_broadcast_slice_decoder *decoder,
                                     uint8_t predicted, uint8_t *mode) {
    uint8_t bin;
    uint8_t syntax = 0U;
    uint8_t coded;
    unsigned bin_index;
    cavs_result result;
    if (predicted > 4U || mode == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    for (bin_index = 0U; bin_index < 4U; ++bin_index) {
        result = decode_context_bin(decoder, AE_CTX_INTRA_LUMA + bin_index,
                                    &bin);
        if (result != CAVS_OK) return result;
        if (bin != 0U) break;
        ++syntax;
    }
    if (syntax == 0U) *mode = predicted;
    else {
        coded = syntax == 4U ? 0U : syntax;
        *mode = coded < predicted ? coded : (uint8_t)(coded + 1U);
    }
    return *mode <= 4U ? CAVS_OK : CAVS_ERR_CORRUPT_BITSTREAM;
}

/* GB/T 20090.16-2016 8.3 Table 48. */
static cavs_result decode_intra_chroma(cavs_broadcast_slice_decoder *decoder,
                                       unsigned neighbor_index,
                                       uint8_t *mode) {
    uint8_t bin;
    cavs_result result = decode_context_bin(decoder, AE_CTX_INTRA_CHROMA +
                                             neighbor_index, &bin);
    if (result != CAVS_OK) return result;
    if (bin == 0U) {
        *mode = 0U;
        return CAVS_OK;
    }
    result = decode_context_bin(decoder, AE_CTX_INTRA_CHROMA + 3U, &bin);
    if (result != CAVS_OK) return result;
    if (bin == 0U) {
        *mode = 1U;
        return CAVS_OK;
    }
    result = decode_context_bin(decoder, AE_CTX_INTRA_CHROMA + 3U, &bin);
    if (result != CAVS_OK) return result;
    *mode = (uint8_t)(2U + bin);
    return CAVS_OK;
}

/* Finds normalized syntax for an 8x8 position; indexing only. */
static const cavs_mb_partition *partition_at(const cavs_macroblock *macroblock,
                                              uint8_t x, uint8_t y) {
    unsigned index;
    if (macroblock == NULL) return NULL;
    for (index = 0U; index < macroblock->partition_count; ++index) {
        const cavs_mb_partition *part = &macroblock->partition[index];
        if (x >= part->x && x < (uint8_t)(part->x + part->width) &&
            y >= part->y && y < (uint8_t)(part->y + part->height))
            return part;
    }
    return NULL;
}

static int8_t reference_at(const cavs_macroblock *macroblock, uint8_t x,
                           uint8_t y, unsigned direction) {
    const cavs_mb_partition *part = partition_at(macroblock, x, y);
    if (part == NULL || direction >= CAVS_MB_DIRECTIONS ||
        part->motion[direction].valid == 0U)
        return -1;
    return part->motion[direction].reference_index;
}

/* GB/T 20090.16-2016 8.3 Table 44 and 8.4.2(f). */
static cavs_result decode_reference_p(cavs_broadcast_slice_decoder *decoder,
                                      int left, int top, uint8_t maximum,
                                      uint8_t *reference) {
    uint8_t bin;
    uint32_t parsed;
    unsigned index = (left > 0 ? 1U : 0U) + (top > 0 ? 2U : 0U);
    cavs_result result = decode_context_bin(decoder, AE_CTX_REFERENCE + index,
                                            &bin);
    if (result != CAVS_OK) return result;
    if (bin != 0U) parsed = 0U;
    else {
        parsed = 1U;
        for (;;) {
            result = decode_context_bin(decoder, AE_CTX_REFERENCE +
                (parsed == 1U ? 4U : 5U), &bin);
            if (result != CAVS_OK) return result;
            if (bin != 0U) break;
            if (parsed == maximum) return CAVS_ERR_CORRUPT_BITSTREAM;
            ++parsed;
        }
    }
    if (parsed > maximum) return CAVS_ERR_CORRUPT_BITSTREAM;
    *reference = (uint8_t)parsed;
    return CAVS_OK;
}

/* GB/T 20090.16-2016 8.3(j): B reference index is an inverted single bin. */
static cavs_result decode_reference_b(cavs_broadcast_slice_decoder *decoder,
                                      int left, int top, uint8_t *reference) {
    uint8_t bin;
    unsigned index = (left > 0 ? 1U : 0U) + (top > 0 ? 2U : 0U);
    cavs_result result = decode_context_bin(decoder, AE_CTX_REFERENCE + index,
                                            &bin);
    if (result == CAVS_OK) *reference = (uint8_t)!bin;
    return result;
}

/* Reads one bypass-coded zero-order exponential-Golomb suffix. */
static cavs_result decode_bypass_ue(cavs_broadcast_slice_decoder *decoder,
                                    uint32_t maximum, uint32_t *value) {
    uint8_t bin;
    unsigned zeros = 0U;
    uint32_t suffix = 0U;
    unsigned index;
    cavs_result result;
    for (;;) {
        result = cavs_ae_decode_bypass(&decoder->arithmetic, &bin);
        if (result != CAVS_OK) return result;
        if (bin != 0U) break;
        if (zeros >= 12U) return CAVS_ERR_CORRUPT_BITSTREAM;
        ++zeros;
    }
    for (index = 0U; index < zeros; ++index) {
        result = cavs_ae_decode_bypass(&decoder->arithmetic, &bin);
        if (result != CAVS_OK) return result;
        suffix = (suffix << 1U) | bin;
    }
    suffix += (UINT32_C(1) << zeros) - 1U;
    if (suffix > maximum) return CAVS_ERR_CORRUPT_BITSTREAM;
    *value = suffix;
    return CAVS_OK;
}

/* GB/T 20090.16-2016 8.3 Table 49 and 8.4.2(g). */
static cavs_result decode_mvd(cavs_broadcast_slice_decoder *decoder,
                              unsigned component, int32_t left_mvd,
                              int32_t *difference) {
    uint8_t bin;
    uint8_t sign;
    uint32_t magnitude;
    uint32_t suffix;
    unsigned base = component == 0U ? AE_CTX_MVD_X : AE_CTX_MVD_Y;
    unsigned neighbor = left_mvd < 0 ? (unsigned)(-(int64_t)left_mvd) :
                                      (unsigned)left_mvd;
    unsigned first = neighbor < 2U ? 0U : (neighbor < 16U ? 1U : 2U);
    cavs_result result = decode_context_bin(decoder, base + first, &bin);
    if (result != CAVS_OK) return result;
    if (bin == 0U) magnitude = 0U;
    else {
        result = decode_context_bin(decoder, base + 3U, &bin);
        if (result != CAVS_OK) return result;
        if (bin == 0U) magnitude = 1U;
        else {
            result = decode_context_bin(decoder, base + 4U, &bin);
            if (result != CAVS_OK) return result;
            if (bin == 0U) magnitude = 2U;
            else {
                result = decode_context_bin(decoder, base + 5U, &bin);
                if (result != CAVS_OK) return result;
                result = decode_bypass_ue(decoder, 2046U, &suffix);
                if (result != CAVS_OK) return result;
                magnitude = (bin == 0U ? 3U : 4U) + suffix * 2U;
            }
        }
    }
    if (magnitude == 0U) {
        *difference = 0;
        return CAVS_OK;
    }
    result = cavs_ae_decode_bypass(&decoder->arithmetic, &sign);
    if (result != CAVS_OK) return result;
    if ((!sign && magnitude > 4095U) || (sign && magnitude > 4096U))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *difference = sign ? -(int32_t)magnitude : (int32_t)magnitude;
    return CAVS_OK;
}

/* GB/T 20090.16-2016 8.3 Table 50 and 8.4.2(h). */
static cavs_result decode_cbp(cavs_broadcast_slice_decoder *decoder,
                              const cavs_broadcast_mb_context *context,
                              uint8_t *coded_block_pattern) {
    uint8_t bin;
    uint8_t cbp = 0U;
    unsigned block;
    unsigned a;
    unsigned b;
    cavs_result result;
    for (block = 0U; block < 4U; ++block) {
        if (block == 0U) {
            a = context->left == NULL ||
                (context->left->coded_block_pattern & (1U << 1U)) != 0U ? 0U : 1U;
            b = context->top == NULL ||
                (context->top->coded_block_pattern & (1U << 2U)) != 0U ? 0U : 1U;
        } else if (block == 1U) {
            a = (cbp & 1U) != 0U ? 0U : 1U;
            b = context->top == NULL ||
                (context->top->coded_block_pattern & (1U << 3U)) != 0U ? 0U : 1U;
        } else if (block == 2U) {
            a = context->left == NULL ||
                (context->left->coded_block_pattern & (1U << 3U)) != 0U ? 0U : 1U;
            b = (cbp & 1U) != 0U ? 0U : 1U;
        } else {
            a = (cbp & (1U << 2U)) != 0U ? 0U : 1U;
            b = (cbp & (1U << 1U)) != 0U ? 0U : 1U;
        }
        result = decode_context_bin(decoder, AE_CTX_CBP + a + 2U * b, &bin);
        if (result != CAVS_OK) return result;
        cbp |= (uint8_t)(bin << block);
    }
    result = decode_context_bin(decoder, AE_CTX_CBP + 4U, &bin);
    if (result != CAVS_OK) return result;
    if (bin != 0U) {
        result = decode_context_bin(decoder, AE_CTX_CBP + 5U, &bin);
        if (result != CAVS_OK) return result;
        if (bin != 0U) cbp |= UINT8_C(48);
        else {
            result = decode_context_bin(decoder, AE_CTX_CBP + 5U, &bin);
            if (result != CAVS_OK) return result;
            cbp |= (uint8_t)((bin != 0U ? 32U : 16U));
        }
    }
    *coded_block_pattern = cbp;
    return CAVS_OK;
}

/* GB/T 20090.16-2016 8.3 Table 44, 8.4.2(i) and 9.8. */
static cavs_result decode_qp_delta(cavs_broadcast_slice_decoder *decoder,
                                   int8_t previous_delta, int8_t *delta) {
    uint8_t bin;
    uint32_t syntax = 0U;
    int32_t parsed;
    cavs_result result = decode_context_bin(decoder, AE_CTX_QP_DELTA +
        (previous_delta != 0 ? 1U : 0U), &bin);
    if (result != CAVS_OK) return result;
    while (bin == 0U) {
        if (syntax >= 63U) return CAVS_ERR_CORRUPT_BITSTREAM;
        ++syntax;
        result = decode_context_bin(decoder, AE_CTX_QP_DELTA +
            (syntax == 1U ? 2U : 3U), &bin);
        if (result != CAVS_OK) return result;
    }
    parsed = (int32_t)((syntax + 1U) >> 1U);
    if ((syntax & 1U) == 0U) parsed = -parsed;
    if (parsed < -32 || parsed > 31) return CAVS_ERR_CORRUPT_BITSTREAM;
    *delta = (int8_t)parsed;
    return CAVS_OK;
}

static unsigned coefficient_rank(uint32_t maximum) {
    if (maximum <= 2U) return (unsigned)maximum;
    if (maximum <= 4U) return 3U;
    return 4U;
}

/* Decodes a Table 44 unary syntax using consecutive/saturated contexts. */
static cavs_result decode_coeff_unary(cavs_broadcast_slice_decoder *decoder,
                                      unsigned base, unsigned first_context,
                                      unsigned saturated_delta,
                                      uint32_t maximum, uint32_t *value) {
    uint32_t parsed = 0U;
    uint8_t bin;
    cavs_result result;
    for (;;) {
        unsigned context = first_context +
            (parsed < saturated_delta ? (unsigned)parsed : saturated_delta);
        result = decode_context_bin(decoder, base + context, &bin);
        if (result != CAVS_OK) return result;
        if (bin != 0U) break;
        if (parsed == maximum) return CAVS_ERR_CORRUPT_BITSTREAM;
        ++parsed;
    }
    *value = parsed;
    return CAVS_OK;
}

/*
 * GB/T 20090.16-2016 8.3(i), 8.4.2(j-k), Tables 52-53 and 9.5.2.
 * The resulting matrix is already inverse-scanned per 9.5.3 Figures 22-23.
 */
static cavs_result decode_coefficients(cavs_broadcast_slice_decoder *decoder,
                                       int field_scan_mode, int chroma,
                                       int16_t matrix[64], uint8_t *count) {
    int16_t level[64];
    uint8_t run[64];
    int16_t scan[64];
    const uint8_t *mapping = field_scan_mode ? field_scan : frame_scan;
    unsigned base = field_scan_mode ?
        (chroma ? AE_CTX_COEFF_FIELD_CHROMA : AE_CTX_COEFF_FIELD_LUMA) :
        (chroma ? AE_CTX_COEFF_FRAME_CHROMA : AE_CTX_COEFF_FRAME_LUMA);
    uint32_t maximum_level = 0U;
    uint32_t position = 0U;
    unsigned pairs = 0U;
    cavs_result result;
    memset(level, 0, sizeof(level));
    memset(run, 0, sizeof(run));
    memset(scan, 0, sizeof(scan));
    for (;;) {
        unsigned rank = coefficient_rank(maximum_level);
        uint32_t raw_level = 0U;
        uint32_t abs_level;
        uint32_t raw_run;
        uint8_t sign;
        if (pairs != 0U) {
            unsigned primary = rank * 3U - 1U;
            uint32_t context_position = position < 64U ? position : 63U;
            unsigned weighted = 14U + (context_position >> 5U) * 16U +
                                ((context_position >> 1U) & 15U);
            uint8_t eob;
            result = cavs_ae_decode_weighted(&decoder->arithmetic,
                &decoder->contexts[base + primary],
                &decoder->contexts[base + weighted], &eob);
            if (result != CAVS_OK) return result;
            if (eob != 0U) break;
            raw_level = 1U;
            if (position >= 64U) return CAVS_ERR_CORRUPT_BITSTREAM;
        }
        if (pairs >= 64U) return CAVS_ERR_CORRUPT_BITSTREAM;
        if (pairs == 0U) {
            result = decode_coeff_unary(decoder, base, 0U, 1U,
                                        UINT32_C(2046), &raw_level);
            if (result != CAVS_OK) return result;
            abs_level = raw_level + 1U;
        } else {
            uint8_t level_bin;
            unsigned first = rank * 3U;
            while (raw_level < UINT32_C(2048)) {
                unsigned delta = raw_level == 1U ? 0U : 1U;
                result = decode_context_bin(decoder, base + first + delta,
                                            &level_bin);
                if (result != CAVS_OK) return result;
                if (level_bin != 0U) break;
                ++raw_level;
            }
            if (raw_level >= UINT32_C(2048))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            abs_level = raw_level;
        }
        if (abs_level > 2048U) return CAVS_ERR_CORRUPT_BITSTREAM;
        result = cavs_ae_decode_bypass(&decoder->arithmetic, &sign);
        if (result != CAVS_OK) return result;
        if (!sign && abs_level > 2047U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        level[pairs] = sign ? -(int16_t)abs_level : (int16_t)abs_level;
        result = decode_coeff_unary(decoder, base,
            46U + rank * 4U + (abs_level == 1U ? 0U : 2U), 1U,
            63U - position, &raw_run);
        if (result != CAVS_OK) return result;
        run[pairs] = (uint8_t)raw_run;
        position += raw_run + 1U;
        if (position > 64U) return CAVS_ERR_CORRUPT_BITSTREAM;
        if (abs_level > maximum_level) maximum_level = abs_level;
        ++pairs;
    }
    if (pairs == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    {
        int coefficient = -1;
        int pair;
        for (pair = (int)pairs - 1; pair >= 0; --pair) {
            coefficient += (int)run[pair] + 1;
            if (coefficient >= 64) return CAVS_ERR_CORRUPT_BITSTREAM;
            scan[coefficient] = level[pair];
        }
    }
    {
        unsigned index;
        for (index = 0U; index < 64U; ++index)
            matrix[index] = scan[mapping[index]];
    }
    *count = (uint8_t)pairs;
    return CAVS_OK;
}

static uint8_t predicted_intra_mode(const cavs_broadcast_mb_context *context,
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

/* Copies normalized geometry into the frozen contract; no codec decision. */
static void store_partitions(cavs_macroblock *macroblock,
                             const decoded_partition part[4], uint8_t count) {
    unsigned index;
    macroblock->partition_count = count;
    for (index = 0U; index < count; ++index) {
        macroblock->partition[index].x = part[index].x;
        macroblock->partition[index].y = part[index].y;
        macroblock->partition[index].width = part[index].width;
        macroblock->partition[index].height = part[index].height;
        macroblock->partition[index].direction = part[index].direction;
    }
}

cavs_result cavs_decode_broadcast_macroblock(
    cavs_broadcast_slice_decoder *decoder,
    const cavs_broadcast_mb_context *context, cavs_macroblock *macroblock) {
    cavs_broadcast_slice_decoder working;
    cavs_macroblock parsed;
    decoded_partition decoded[4];
    uint8_t subtypes[4] = { 0U, 0U, 0U, 0U };
    uint8_t type_index;
    uint8_t partition_count;
    uint8_t raw_type;
    cavs_result result;
    unsigned index;
    int p_family;
    if (decoder == NULL || context == NULL || macroblock == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (context->profile_id != UINT8_C(0x48))
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    if (context->format != CAVS_YUV420P8 ||
        context->picture_type > CAVS_PICTURE_B ||
        context->progressive_frame > 1U || context->picture_structure > 1U ||
        context->skip_mode_flag > 1U || context->picture_reference_flag > 1U ||
        context->fixed_qp > 1U || context->previous_qp > 63U ||
        context->previous_qp_delta < -32 || context->previous_qp_delta > 31 ||
        context->mb_weighting_flag > 1U || context->macroblock_width == 0U ||
        context->macroblock_height == 0U ||
        context->macroblock_index >=
            context->macroblock_width * context->macroblock_height)
        return CAVS_ERR_INVALID_ARGUMENT;
    working = *decoder;
    memset(&parsed, 0, sizeof(parsed));
    parsed.address = context->macroblock_index;
    parsed.row = (uint16_t)(context->macroblock_index /
                            context->macroblock_width);
    parsed.column = (uint16_t)(context->macroblock_index %
                               context->macroblock_width);
    parsed.slice_id = context->slice_id;
    parsed.transform_8x8 = 1U;
    parsed.qp = context->previous_qp;

    result = decode_mb_type(&working, context, &raw_type, &type_index);
    if (result != CAVS_OK) return result;
    parsed.raw_type = raw_type;
    p_family = context->picture_type != CAVS_PICTURE_B;
    if (p_family) {
        if (type_index == 5U) {
            parsed.type = CAVS_MB_I_8X8;
            parsed.is_intra = 1U;
            partition_count = 0U;
        } else {
            parsed.type = type_index == 0U ? CAVS_MB_P_SKIP :
                (type_index == 1U ? CAVS_MB_P_16X16 :
                 (type_index == 2U ? CAVS_MB_P_16X8 :
                  (type_index == 3U ? CAVS_MB_P_8X16 : CAVS_MB_P_8X8)));
            parsed.is_skipped = type_index == 0U;
            set_p_partitions(type_index, decoded, &partition_count);
        }
    } else {
        if (type_index == 24U) {
            parsed.type = CAVS_MB_I_8X8;
            parsed.is_intra = 1U;
            partition_count = 0U;
        } else {
            parsed.type = type_index == 0U ? CAVS_MB_B_SKIP :
                (type_index == 1U ? CAVS_MB_B_DIRECT : CAVS_MB_B_INTER);
            parsed.is_skipped = type_index == 0U;
            if (type_index == 23U) {
                for (index = 0U; index < 4U; ++index) {
                    result = decode_part_type(&working, &subtypes[index]);
                    if (result != CAVS_OK) return result;
                }
            }
            result = set_b_partitions(type_index, subtypes, decoded,
                                      &partition_count);
            if (result != CAVS_OK) return result;
        }
    }
    store_partitions(&parsed, decoded, partition_count);
    if (parsed.is_skipped != 0U) {
        result = cavs_ae_decode_stuffing(&working.arithmetic,
                                         &working.last_stuffing_bit);
        if (result != CAVS_OK) return result;
        working.has_stuffing_bit = 1U;
        parsed.end_bit_offset = cavs_ae_bit_offset(&working.arithmetic);
        *decoder = working;
        *macroblock = parsed;
        return CAVS_OK;
    }

    if (parsed.is_intra != 0U) {
        unsigned neighbor = 0U;
        for (index = 0U; index < 4U; ++index) {
            result = decode_intra_luma(&working,
                predicted_intra_mode(context, &parsed, index),
                &parsed.intra_luma_mode[index]);
            if (result != CAVS_OK) return result;
        }
        if (context->left != NULL && context->left->is_intra != 0U &&
            context->left->intra_chroma_mode != 0U) ++neighbor;
        if (context->top != NULL && context->top->is_intra != 0U &&
            context->top->intra_chroma_mode != 0U) ++neighbor;
        result = decode_intra_chroma(&working, neighbor,
                                     &parsed.intra_chroma_mode);
        if (result != CAVS_OK) return result;
    }

    {
        unsigned direction;
        for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
            for (index = 0U; index < partition_count; ++index) {
                int has_motion = direction == CAVS_PRED_FORWARD ?
                    (decoded[index].encoded == MOTION_FORWARD ||
                     decoded[index].encoded == MOTION_SYMMETRIC) :
                    decoded[index].encoded == MOTION_BACKWARD;
                cavs_motion_vector *motion;
                int left;
                int top;
                uint8_t reference = 0U;
                if (!has_motion) continue;
                motion = &parsed.partition[index].motion[direction];
                left = decoded[index].x != 0U ?
                    reference_at(&parsed,
                                 (uint8_t)(decoded[index].x - 1U),
                                 decoded[index].y, direction) :
                    reference_at(context->left, 15U,
                                 decoded[index].y, direction);
                top = decoded[index].y != 0U ?
                    reference_at(&parsed, decoded[index].x,
                                 (uint8_t)(decoded[index].y - 1U), direction) :
                    reference_at(context->top, decoded[index].x,
                                 15U, direction);
                if (context->picture_reference_flag == 0U &&
                    (context->picture_type == CAVS_PICTURE_P ||
                     (context->picture_type == CAVS_PICTURE_B &&
                      context->picture_structure == 0U))) {
                    if (context->picture_type == CAVS_PICTURE_B)
                        result = decode_reference_b(&working, left, top,
                                                    &reference);
                    else
                        result = decode_reference_p(&working, left, top,
                            context->picture_structure == 0U ? 3U : 1U,
                            &reference);
                    if (result != CAVS_OK) return result;
                }
                motion->reference_index = (int8_t)reference;
                motion->valid = 1U;
            }
        }
        for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
            for (index = 0U; index < partition_count; ++index) {
                int has_motion = direction == CAVS_PRED_FORWARD ?
                    (decoded[index].encoded == MOTION_FORWARD ||
                     decoded[index].encoded == MOTION_SYMMETRIC) :
                    decoded[index].encoded == MOTION_BACKWARD;
                cavs_motion_vector *motion;
                const cavs_mb_partition *left_part;
                int32_t left_x = 0;
                int32_t left_y = 0;
                if (!has_motion) continue;
                motion = &parsed.partition[index].motion[direction];
                left_part = decoded[index].x != 0U ?
                    partition_at(&parsed, (uint8_t)(decoded[index].x - 1U),
                                 decoded[index].y) :
                    partition_at(context->left, 15U, decoded[index].y);
                if (left_part != NULL && left_part->motion[direction].valid) {
                    left_x = left_part->motion[direction].x;
                    left_y = left_part->motion[direction].y;
                }
                result = decode_mvd(&working, 0U, left_x, &motion->x);
                if (result != CAVS_OK) return result;
                result = decode_mvd(&working, 1U, left_y, &motion->y);
                if (result != CAVS_OK) return result;
            }
        }
    }

    if (context->mb_weighting_flag != 0U && parsed.is_intra == 0U) {
        uint8_t ignored;
        result = decode_context_bin(&working, AE_CTX_WEIGHTING, &ignored);
        if (result != CAVS_OK) return result;
    }
    {
        uint8_t cbp;
        result = decode_cbp(&working, context, &cbp);
        if (result != CAVS_OK) return result;
        parsed.coded_block_pattern = cbp;
    }
    if (parsed.coded_block_pattern != 0U && context->fixed_qp == 0U) {
        int8_t delta;
        int qp;
        result = decode_qp_delta(&working, context->previous_qp_delta, &delta);
        if (result != CAVS_OK) return result;
        qp = ((int)context->previous_qp + delta + 64) % 64;
        parsed.qp_delta = delta;
        parsed.qp = (uint8_t)qp;
    }
    for (index = 0U; index < CAVS_MB_8X8_BLOCKS; ++index) {
        if ((parsed.coded_block_pattern & (UINT32_C(1) << index)) == 0U)
            continue;
        result = decode_coefficients(&working,
            context->progressive_frame == 0U && context->picture_structure == 0U,
            index >= 4U, parsed.coefficients_8x8[index],
            &parsed.coefficient_count_8x8[index]);
        if (result != CAVS_OK) return result;
    }
    result = cavs_ae_decode_stuffing(&working.arithmetic,
                                     &working.last_stuffing_bit);
    if (result == CAVS_OK) working.has_stuffing_bit = 1U;
    if (result != CAVS_OK) return result;
    parsed.end_bit_offset = cavs_ae_bit_offset(&working.arithmetic);
    *decoder = working;
    *macroblock = parsed;
    return CAVS_OK;
}
