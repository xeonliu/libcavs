/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 9.5-9.10 and GB/T 20090.2-2013 9.7-9.11.
 */
#include "codec/broadcast_reconstruction.h"
#include "codec/coefficients.h"
#include "dsp/prediction.h"
#include "dsp/reconstruction.h"
#include "dsp/transform.h"
#include <limits.h>
#include <string.h>

#define INTRA_REFERENCE_MASK UINT32_C(0x1ffff)

static const uint16_t dequant_table[64] = {
    32768,36061,38968,42495,46341,50535,55437,60424,
    32932,35734,38968,42495,46177,50535,55109,59933,
    65535,35734,38968,42577,46341,50617,55027,60097,
    32809,35734,38968,42454,46382,50576,55109,60056,
    65535,35734,38968,42495,46320,50515,55109,60076,
    65535,35744,38968,42495,46341,50535,55099,60087,
    65535,35734,38973,42500,46341,50535,55109,60097,
    32771,35734,38965,42497,46341,50535,55109,60099
};

static const uint8_t dequant_shift[64] = {
    14,14,14,14,14,14,14,14,13,13,13,13,13,13,13,13,
    13,12,12,12,12,12,12,12,11,11,11,11,11,11,11,11,
    11,10,10,10,10,10,10,10,10,9,9,9,9,9,9,9,
    9,8,8,8,8,8,8,8,7,7,7,7,7,7,7,7
};

/* Integer arithmetic helper only; it introduces no codec decision. */
static int64_t floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}


cavs_result cavs_picture_field_plane(cavs_picture *picture, unsigned plane,
                                     uint8_t field, cavs_field_plane *view) {
    cavs_field_plane parsed;
    uint32_t width;
    uint32_t height;
    ptrdiff_t stride;
    unsigned vertical_shift;

    if (picture == NULL || view == NULL || plane >= 3U ||
        picture->format != CAVS_YUV420P8 || picture->plane[plane] == NULL ||
        picture->stride[plane] <= 0 || picture->coded_width == 0U ||
        picture->coded_height == 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    width = plane == 0U ? picture->coded_width : picture->coded_width / 2U;
    height = plane == 0U ? picture->coded_height : picture->coded_height / 2U;
    stride = picture->stride[plane];
    if ((uint64_t)stride < width)
        return CAVS_ERR_INVALID_ARGUMENT;
    parsed.data = picture->plane[plane];
    parsed.width = width;
    parsed.height = height;
    parsed.stride = stride;
    if (field == CAVS_FIELD_BOTH) {
        *view = parsed;
        return CAVS_OK;
    }
    vertical_shift = plane == 0U ? 2U : 1U;
    if ((field != CAVS_FIELD_TOP && field != CAVS_FIELD_BOTTOM) ||
        (height & 1U) != 0U || (picture->coded_height & 3U) != 0U ||
        stride > PTRDIFF_MAX / 2)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (field == CAVS_FIELD_BOTTOM) parsed.data += stride;
    parsed.height >>= 1U;
    parsed.stride *= 2;
    if (parsed.height < (size_t)vertical_shift * 8U)
        return CAVS_ERR_INVALID_ARGUMENT;
    *view = parsed;
    return CAVS_OK;
}

cavs_result cavs_broadcast_inverse_quantize_8x8(
    const int16_t quant[64], uint8_t qp, int32_t coefficients[64]) {
    int32_t input[64];
    int32_t predicted[64];
    uint8_t weights[64];
    unsigned index;
    if (quant == NULL || coefficients == NULL || qp > 63U)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (index = 0U; index < 64U; ++index) {
        input[index] = quant[index];
        predicted[index] = 0;
        weights[index] = 128U;
    }
    return cavs_inverse_quantize_8x8(input, predicted, weights, qp,
                                     coefficients);
}

cavs_result cavs_broadcast_inverse_transform_8x8(
    const int32_t coefficients[64], int16_t residual[64]) {
    return cavs_dsp_inverse_transform_8x8_c(coefficients, residual);
}

cavs_result cavs_broadcast_inverse_quantize_4x4(
    const int16_t quant[16], uint8_t qp, int32_t coefficients[16]) {
    int32_t parsed[16];
    unsigned index;
    unsigned shift;
    if (quant == NULL || coefficients == NULL || qp > 63U)
        return CAVS_ERR_INVALID_ARGUMENT;
    shift = dequant_shift[qp];
    for (index = 0U; index < 16U; ++index) {
        int64_t value;
        if (quant[index] < -1024 || quant[index] > 1023)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        value = (int64_t)quant[index] * dequant_table[qp];
        value = floor_shift(value + (INT64_C(1) << (shift - 1U)), shift);
        if (value < -4096 || value > 4095)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed[index] = (int32_t)value;
    }
    memcpy(coefficients, parsed, sizeof(parsed));
    return CAVS_OK;
}

/* Validates the frozen picture storage contract without codec decisions. */
static int picture_valid(const cavs_picture *picture) {
    size_t required;
    if (picture == NULL || picture->format != CAVS_YUV420P8 ||
        picture->macroblock_width == 0U || picture->macroblock_height == 0U ||
        picture->coded_width != (uint32_t)picture->macroblock_width * 16U ||
        picture->coded_height != (uint32_t)picture->macroblock_height * 16U ||
        picture->macroblocks == NULL)
        return 0;
    required = (size_t)picture->macroblock_width * picture->macroblock_height;
    return picture->macroblock_count >= required;
}

/* Returns the syntax-row base for a physical field; no codec decision. */
static uint16_t field_row_base(const cavs_picture *picture, uint8_t field) {
    uint16_t field_rows = (uint16_t)(picture->macroblock_height / 2U);
    if (field == CAVS_FIELD_BOTH) return 0U;
    if ((field == CAVS_FIELD_TOP) == (picture->top_field_first != 0U))
        return 0U;
    return field_rows;
}

/* Checks whether an external sample belongs to a committed same-slice MB. */
static int external_sample_available(
    const cavs_broadcast_reconstruction_context *context,
    const cavs_field_plane *plane, unsigned plane_index, size_t x, size_t y) {
    const cavs_picture *picture = context->picture;
    size_t mb_width = plane_index == 0U ? 16U : 8U;
    size_t mb_height = plane_index == 0U ? 16U : 8U;
    size_t column;
    size_t local_row;
    size_t row;
    size_t address;
    const cavs_macroblock *neighbor;
    if (x >= plane->width || y >= plane->height) return 0;
    column = x / mb_width;
    local_row = y / mb_height;
    row = (size_t)field_row_base(picture, context->field) + local_row;
    if (column >= picture->macroblock_width || row >= picture->macroblock_height)
        return 0;
    address = row * picture->macroblock_width + column;
    if (address >= picture->macroblock_count) return 0;
    neighbor = &picture->macroblocks[address];
    return neighbor->end_bit_offset != 0U && neighbor->address == address &&
           neighbor->row == row && neighbor->column == column &&
           neighbor->slice_id == context->slice_id;
}

/* Reads either an already staged current-MB sample or a committed neighbor. */
static int working_sample(
    const cavs_broadcast_reconstruction_context *context,
    const cavs_field_plane *plane, unsigned plane_index, size_t mb_x,
    size_t mb_y, size_t block_size, const uint8_t *staged,
    const uint8_t *staged_valid, size_t x, size_t y, uint8_t *sample) {
    if (x >= mb_x && x < mb_x + block_size && y >= mb_y &&
        y < mb_y + block_size) {
        size_t offset = (y - mb_y) * block_size + x - mb_x;
        if (staged_valid[offset] == 0U) return 0;
        *sample = staged[offset];
        return 1;
    }
    if (!external_sample_available(context, plane, plane_index, x, y))
        return 0;
    *sample = plane->data[y * (size_t)plane->stride + x];
    return 1;
}

/* Implements 9.8.2 reference collection against staged unfiltered samples. */
static void acquire_working_references(
    const cavs_broadcast_reconstruction_context *context,
    const cavs_field_plane *plane, unsigned plane_index, size_t mb_x,
    size_t mb_y, size_t macroblock_size, const uint8_t *staged,
    const uint8_t *staged_valid, size_t x0, size_t y0,
    cavs_intra_references_8x8 *references) {
    unsigned index;
    memset(references, 0, sizeof(*references));
    for (index = 1U; index <= 16U; ++index) {
        size_t offset = (size_t)index - 1U;
        uint32_t bit = UINT32_C(1) << index;
        uint8_t sample;
        int top = y0 != 0U && working_sample(
            context, plane, plane_index, mb_x, mb_y, macroblock_size, staged,
            staged_valid, x0 + offset, y0 - 1U, &sample);
        if (top) {
            references->top[index] = sample;
            references->top_available |= bit;
        } else if (index > 8U &&
                   (references->top_available & (UINT32_C(1) << 8U)) != 0U) {
            references->top[index] = references->top[8];
            references->top_available |= bit;
        }
        if (x0 != 0U && working_sample(
                context, plane, plane_index, mb_x, mb_y, macroblock_size,
                staged, staged_valid, x0 - 1U, y0 + offset, &sample)) {
            references->left[index] = sample;
            references->left_available |= bit;
        } else if (index > 8U &&
                   (references->left_available & (UINT32_C(1) << 8U)) != 0U) {
            references->left[index] = references->left[8];
            references->left_available |= bit;
        }
    }
    if (x0 != 0U && y0 != 0U) {
        uint8_t sample;
        if (working_sample(context, plane, plane_index, mb_x, mb_y,
                           macroblock_size, staged, staged_valid, x0 - 1U,
                           y0 - 1U, &sample)) {
            references->top[0] = sample;
            references->top_available |= 1U;
        }
    }
    if ((references->top_available & 1U) == 0U) {
        if ((references->top_available & 2U) != 0U) {
            references->top[0] = references->top[1];
            references->top_available |= 1U;
        } else if ((references->left_available & 2U) != 0U) {
            references->top[0] = references->left[1];
            references->top_available |= 1U;
        }
    }
    references->left[0] = references->top[0];
    references->left_available |= references->top_available & 1U;
    references->top_available &= INTRA_REFERENCE_MASK;
    references->left_available &= INTRA_REFERENCE_MASK;
}

/* Applies either one 8x8 transform or four 4x4 transforms to a block. */
static cavs_result reconstruct_residual_block(
    const cavs_macroblock *macroblock, unsigned block_index,
    uint8_t qp, const uint8_t prediction[64], uint8_t output[64]) {
    int16_t residual[64];
    cavs_result result;
    unsigned index;
    if (macroblock->transform_8x8 != 0U) {
        int32_t coefficients[64];
        if (block_index >= CAVS_MB_8X8_BLOCKS ||
            macroblock->coefficient_count_8x8[block_index] > 64U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        result = cavs_broadcast_inverse_quantize_8x8(
            macroblock->coefficients_8x8[block_index], qp, coefficients);
        if (result != CAVS_OK) return result;
        result = cavs_broadcast_inverse_transform_8x8(coefficients, residual);
        if (result != CAVS_OK) return result;
    } else {
        memset(residual, 0, sizeof(residual));
        for (index = 0U; index < 4U; ++index) {
            int32_t coefficients[16];
            int16_t sub_residual[16];
            unsigned source = block_index < 4U ? block_index * 4U + index :
                CAVS_MB_LUMA_4X4_BLOCKS + (block_index - 4U) * 4U + index;
            unsigned x = (index & 1U) * 4U;
            unsigned y = (index >> 1U) * 4U;
            unsigned sx;
            unsigned sy;
            if (source >= CAVS_MB_4X4_BLOCKS ||
                macroblock->coefficient_count_4x4[source] > 16U)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            result = cavs_broadcast_inverse_quantize_4x4(
                macroblock->coefficients_4x4[source], qp, coefficients);
            if (result != CAVS_OK) return result;
            result = cavs_dsp_inverse_transform_4x4_c(
                coefficients, sub_residual);
            if (result != CAVS_OK) return result;
            for (sy = 0U; sy < 4U; ++sy)
                for (sx = 0U; sx < 4U; ++sx)
                    residual[(y + sy) * 8U + x + sx] =
                        sub_residual[sy * 4U + sx];
        }
    }
    cavs_dsp_add_residual_8x8_c(prediction, residual, output);
    return CAVS_OK;
}

/* Copies a compact block to macroblock staging; no codec decision. */
static void stage_block(uint8_t *staged, uint8_t *valid, size_t stride,
                        size_t x0, size_t y0, const uint8_t block[64]) {
    unsigned x;
    unsigned y;
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x) {
            size_t offset = (y0 + y) * stride + x0 + x;
            staged[offset] = block[y * 8U + x];
            valid[offset] = 1U;
        }
    }
}

/* Copies a staged macroblock to a strided field plane; no codec decision. */
static void commit_block(cavs_field_plane *plane, size_t x0, size_t y0,
                         size_t width, const uint8_t *samples) {
    size_t y;
    for (y = 0U; y < width; ++y)
        memcpy(plane->data + (y0 + y) * (size_t)plane->stride + x0,
               samples + y * width, width);
}

cavs_result cavs_broadcast_reconstruct_macroblock(
    const cavs_broadcast_reconstruction_context *context,
    const cavs_macroblock *macroblock) {
    cavs_picture *picture;
    cavs_field_plane planes[3];
    uint8_t staged_luma[256];
    uint8_t staged_luma_valid[256];
    uint8_t staged_chroma[2][64];
    uint8_t staged_chroma_valid[64];
    uint32_t sample_x;
    uint32_t sample_y;
    ptrdiff_t line_step;
    size_t luma_y;
    size_t chroma_y;
    size_t address;
    unsigned block;
    cavs_result result;

    if (context == NULL || macroblock == NULL ||
        !picture_valid(context->picture) ||
        context->weighting_quant_flag > 1U || macroblock->transform_8x8 > 1U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (context->weighting_quant_flag != 0U)
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    picture = context->picture;
    if (macroblock->row >= picture->macroblock_height ||
        macroblock->column >= picture->macroblock_width)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    address = (size_t)macroblock->row * picture->macroblock_width +
        macroblock->column;
    if (macroblock->address != address || address >= picture->macroblock_count)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    result = cavs_picture_field_position(
        picture, context->field, macroblock->row, macroblock->column,
        &sample_x, &sample_y, &line_step);
    if (result != CAVS_OK) return result;
    (void)line_step;
    for (block = 0U; block < 3U; ++block) {
        result = cavs_picture_field_plane(picture, block, context->field,
                                          &planes[block]);
        if (result != CAVS_OK) return result;
    }
    luma_y = context->field == CAVS_FIELD_BOTH ? sample_y : sample_y / 2U;
    chroma_y = luma_y / 2U;
    if ((size_t)sample_x + 16U > planes[0].width ||
        luma_y + 16U > planes[0].height ||
        (size_t)sample_x / 2U + 8U > planes[1].width ||
        chroma_y + 8U > planes[1].height)
        return CAVS_ERR_CORRUPT_BITSTREAM;

    memset(staged_luma, 0, sizeof(staged_luma));
    memset(staged_luma_valid, 0, sizeof(staged_luma_valid));
    memset(staged_chroma, 0, sizeof(staged_chroma));
    memset(staged_chroma_valid, 0, sizeof(staged_chroma_valid));

    if (macroblock->is_intra != 0U) {
        for (block = 0U; block < 4U; ++block) {
            cavs_intra_references_8x8 references;
            uint8_t prediction[64];
            uint8_t reconstructed[64];
            size_t x = (size_t)sample_x + (block & 1U) * 8U;
            size_t y = luma_y + (block >> 1U) * 8U;
            if (macroblock->intra_luma_mode[block] >
                CAVS_INTRA_LUMA_DOWN_RIGHT_8X8)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            acquire_working_references(
                context, &planes[0], 0U, sample_x, luma_y, 16U,
                staged_luma, staged_luma_valid, x, y, &references);
            result = cavs_dsp_predict_intra_luma_8x8_c(
                &references,
                (cavs_intra_luma_mode_8x8)macroblock->intra_luma_mode[block],
                prediction);
            if (result != CAVS_OK) return result;
            result = reconstruct_residual_block(
                macroblock, block, macroblock->qp, prediction, reconstructed);
            if (result != CAVS_OK) return result;
            stage_block(staged_luma, staged_luma_valid, 16U,
                        (block & 1U) * 8U, (block >> 1U) * 8U, reconstructed);
        }
        for (block = 0U; block < 2U; ++block) {
            cavs_intra_references_8x8 references;
            uint8_t prediction[64];
            uint8_t qp;
            int8_t delta = block == 0U ? context->chroma_qp_delta_cb :
                context->chroma_qp_delta_cr;
            if (macroblock->intra_chroma_mode > CAVS_INTRA_CHROMA_PLANE_8X8)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            acquire_working_references(
                context, &planes[block + 1U], block + 1U, sample_x / 2U,
                chroma_y, 8U, staged_chroma[block], staged_chroma_valid,
                sample_x / 2U, chroma_y, &references);
            result = cavs_dsp_predict_intra_chroma_8x8_c(
                &references,
                (cavs_intra_chroma_mode_8x8)macroblock->intra_chroma_mode,
                prediction);
            if (result != CAVS_OK) return result;
            result = cavs_map_chroma_qp(macroblock->qp, delta, &qp);
            if (result != CAVS_OK) return result;
            result = reconstruct_residual_block(
                macroblock, block + 4U, qp, prediction,
                staged_chroma[block]);
            if (result != CAVS_OK) return result;
        }
    } else {
        if (context->inter_prediction == NULL)
            return CAVS_ERR_MISSING_REFERENCE;
        memcpy(staged_luma, context->inter_prediction->luma,
               sizeof(staged_luma));
        for (block = 0U; block < 4U; ++block) {
            uint8_t prediction[64];
            uint8_t reconstructed[64];
            unsigned x = (block & 1U) * 8U;
            unsigned y = (block >> 1U) * 8U;
            unsigned row;
            for (row = 0U; row < 8U; ++row)
                memcpy(prediction + row * 8U,
                       context->inter_prediction->luma +
                           (y + row) * 16U + x, 8U);
            result = reconstruct_residual_block(
                macroblock, block, macroblock->qp, prediction, reconstructed);
            if (result != CAVS_OK) return result;
            stage_block(staged_luma, staged_luma_valid, 16U, x, y,
                        reconstructed);
        }
        for (block = 0U; block < 2U; ++block) {
            uint8_t qp;
            int8_t delta = block == 0U ? context->chroma_qp_delta_cb :
                context->chroma_qp_delta_cr;
            result = cavs_map_chroma_qp(macroblock->qp, delta, &qp);
            if (result != CAVS_OK) return result;
            result = reconstruct_residual_block(
                macroblock, block + 4U, qp,
                context->inter_prediction->chroma[block],
                staged_chroma[block]);
            if (result != CAVS_OK) return result;
        }
    }

    commit_block(&planes[0], sample_x, luma_y, 16U, staged_luma);
    commit_block(&planes[1], sample_x / 2U, chroma_y, 8U, staged_chroma[0]);
    commit_block(&planes[2], sample_x / 2U, chroma_y, 8U, staged_chroma[1]);
    picture->macroblocks[address] = *macroblock;
    picture->macroblocks[address].slice_id = context->slice_id;
    picture->filtered = 0U;
    return CAVS_OK;
}
