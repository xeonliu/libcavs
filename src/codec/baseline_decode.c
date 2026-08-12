/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013 7.4 and 9.2-9.11 baseline I-slice decoding.
 */
#include "codec/baseline_decode.h"
#include "codec/baseline_macroblock.h"
#include "codec/coefficients.h"
#include "dsp/prediction.h"
#include "frame.h"
#include <string.h>

int cavs_baseline_picture_supported(
    const cavs_sequence_info *sequence, const cavs_i_picture_header *picture) {
    if (sequence == NULL || picture == NULL) return 0;
    return sequence->profile_id == UINT8_C(0x20) &&
        sequence->format == CAVS_YUV420P8 &&
        sequence->progressive_sequence != 0U &&
        picture->progressive_frame != 0U && picture->picture_structure == 1U &&
        picture->advanced_entropy_enabled == 0U;
}

static cavs_intra_availability_8x8 block_availability(int top, int left) {
    cavs_intra_availability_8x8 availability;
    memset(&availability, 0, sizeof(availability));
    if (top != 0) availability.top = UINT16_C(0xffff);
    if (left != 0) availability.left = UINT16_C(0xffff);
    availability.top_left = (uint8_t)(top != 0 && left != 0);
    return availability;
}

/* Gets the 8x8 luma mode prediction from already reconstructed neighbors. */
static uint8_t predicted_luma_mode(const cavs_baseline_decode_context *context,
                                   size_t block_x, size_t block_y,
                                   size_t map_width) {
    int left_available = block_x != 0U &&
        context->luma_intra[block_y * map_width + block_x - 1U] != 0U;
    int top_available = block_y != 0U &&
        context->luma_intra[(block_y - 1U) * map_width + block_x] != 0U;
    uint8_t left = left_available ?
        context->luma_modes[block_y * map_width + block_x - 1U] : 2U;
    uint8_t top = top_available ?
        context->luma_modes[(block_y - 1U) * map_width + block_x] : 2U;
    if (left_available && top_available) return left < top ? left : top;
    if (left_available) return left;
    if (top_available) return top;
    return 2U;
}

/* Resolves the coded 2-bit luma mode against 9.4.4's predicted mode. */
static cavs_result resolved_luma_mode(const cavs_baseline420_mb_header *header,
                                      unsigned index, uint8_t predicted,
                                      uint8_t *mode) {
    uint8_t coded;
    if (header == NULL || mode == NULL || index >= 4U || predicted > 4U ||
        header->prediction_mode_flag[index] > 1U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (header->prediction_mode_flag[index] != 0U) {
        *mode = predicted;
        return CAVS_OK;
    }
    coded = header->intra_luma_prediction_mode[index];
    if (coded > 3U) return CAVS_ERR_CORRUPT_BITSTREAM;
    *mode = coded < predicted ? coded : (uint8_t)(coded + 1U);
    return CAVS_OK;
}

/* Copies one reconstructed 8x8 block into a frame plane. */
static void store_block(uint8_t *plane, size_t stride, size_t x0, size_t y0,
                        const uint8_t block[64]) {
    unsigned row;
    for (row = 0U; row < 8U; ++row)
        memcpy(plane + (y0 + row) * stride + x0, block + row * 8U, 8U);
}

/* Decodes one baseline I slice row and writes its six blocks to the frame. */
cavs_result cavs_baseline_decode_slice(
    const cavs_baseline_decode_context *decode,
    const uint8_t *data, size_t bit_size) {
    cavs_baseline420_mb_context macroblock_context;
    uint8_t *planes[3];
    size_t macroblock_width;
    size_t macroblock_height;
    size_t row;
    size_t column;
    size_t cursor;
    uint8_t chroma_qp;
    if (decode == NULL || decode->picture == NULL ||
        decode->slice == NULL || decode->luma_modes == NULL ||
        decode->luma_intra == NULL || decode->frame_has_data == NULL ||
        data == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (decode->frame == NULL) return CAVS_OK;
    if (decode->slice->header_bits > bit_size)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    planes[0] = (uint8_t *)cavs_frame_plane(decode->frame, 0U);
    planes[1] = (uint8_t *)cavs_frame_plane(decode->frame, 1U);
    planes[2] = (uint8_t *)cavs_frame_plane(decode->frame, 2U);
    macroblock_width = decode->frame->coded_width / 16U;
    macroblock_height = decode->frame->coded_height / 16U;
    row = decode->slice->macroblock_row;
    if (row >= macroblock_height) return CAVS_ERR_CORRUPT_BITSTREAM;
    /* A header-only unit is valid for API state-machine callers; the final
     * byte may also contain one zero alignment bit after the slice header. */
    if (!(*decode->frame_has_data) &&
        bit_size - decode->slice->header_bits <= 8U)
        return CAVS_OK;
    memset(&macroblock_context, 0, sizeof(macroblock_context));
    macroblock_context.profile_id = UINT8_C(0x20);
    macroblock_context.format = CAVS_YUV420P8;
    macroblock_context.picture_type = CAVS_PICTURE_I;
    macroblock_context.picture_structure = 1U;
    macroblock_context.skip_mode_flag = decode->picture->skip_mode_flag;
    macroblock_context.picture_reference_flag = 1U;
    macroblock_context.fixed_qp = decode->slice->fixed_slice_qp;
    macroblock_context.previous_qp = decode->slice->slice_qp;
    macroblock_context.reference_index_bits = 1U;
    macroblock_context.mb_weighting_flag = decode->slice->mb_weighting_flag;
    macroblock_context.macroblock_width = (uint32_t)macroblock_width;
    macroblock_context.macroblock_height = (uint32_t)macroblock_height;
    cursor = decode->slice->header_bits;
    for (column = 0U; column < macroblock_width; ++column) {
        cavs_baseline420_macroblock macroblock;
        cavs_intra_references_8x8 chroma_references[2];
        cavs_intra_availability_8x8 availability;
        size_t block_width = macroblock_width * 2U;
        size_t macroblock_x = column * 16U;
        size_t macroblock_y = row * 16U;
        unsigned index;
        cavs_result result;

        macroblock_context.macroblock_index =
            (uint32_t)(row * macroblock_width + column);
        result = cavs_decode_baseline420_macroblock(
            data, bit_size, cursor, &macroblock_context, &macroblock);
        if (result != CAVS_OK) return result;
        for (index = 0U; index < 4U; ++index) {
            cavs_intra_references_8x8 references;
            uint8_t prediction[64];
            uint8_t reconstructed[64];
            uint8_t predicted_mode;
            uint8_t mode;
            size_t block_x = macroblock_x + (index & 1U) * 8U;
            size_t block_y = macroblock_y + (index >= 2U ? 8U : 0U);
            size_t map_x = column * 2U + (index & 1U);
            size_t map_y = row * 2U + (index >= 2U ? 1U : 0U);
            int top = block_y != 0U;
            int left = block_x != 0U;
            availability = block_availability(top, left);
            result = cavs_acquire_intra_references_8x8(
                planes[0], decode->frame->coded_width,
                decode->frame->coded_height,
                (size_t)decode->frame->stride[0], block_x, block_y,
                &availability, &references);
            if (result != CAVS_OK) return result;
            predicted_mode = predicted_luma_mode(
                decode, map_x, map_y, block_width);
            result = resolved_luma_mode(&macroblock.header, index,
                                        predicted_mode, &mode);
            if (result != CAVS_OK) return result;
            result = cavs_dsp_predict_intra_luma_8x8_c(
                &references, (cavs_intra_luma_mode_8x8)mode, prediction);
            if (result != CAVS_OK) return result;
            result = cavs_reconstruct_baseline420_block(
                &macroblock.block[index], macroblock.block_coded[index],
                macroblock.header.qp, CAVS_SCAN_8X8_FRAME, prediction, NULL,
                reconstructed);
            if (result != CAVS_OK) return result;
            store_block(planes[0],
                        (size_t)decode->frame->stride[0], block_x,
                        block_y, reconstructed);
            decode->luma_modes[map_y * block_width + map_x] = mode;
            decode->luma_intra[map_y * block_width + map_x] = 1U;
        }
        for (index = 0U; index < 2U; ++index) {
            uint8_t prediction[64];
            uint8_t reconstructed[64];
            availability = block_availability(row != 0U, column != 0U);
            result = cavs_acquire_intra_references_8x8(
                planes[index + 1U],
                decode->frame->coded_width / 2U,
                decode->frame->coded_height / 2U,
                (size_t)decode->frame->stride[index + 1U],
                column * 8U, row * 8U, &availability,
                &chroma_references[index]);
            if (result != CAVS_OK) return result;
            result = cavs_dsp_predict_intra_chroma_8x8_c(
                &chroma_references[index],
                (cavs_intra_chroma_mode_8x8)
                    macroblock.header.intra_chroma_prediction_mode,
                prediction);
            if (result != CAVS_OK) return result;
            result = cavs_map_chroma_qp(
                macroblock.header.qp, 0, &chroma_qp);
            if (result != CAVS_OK) return result;
            result = cavs_reconstruct_baseline420_block(
                &macroblock.block[index + 4U],
                macroblock.block_coded[index + 4U], chroma_qp,
                CAVS_SCAN_8X8_FRAME, prediction, NULL, reconstructed);
            if (result != CAVS_OK) return result;
            store_block(planes[index + 1U],
                        (size_t)decode->frame->stride[index + 1U],
                        column * 8U, row * 8U,
                        reconstructed);
        }
        macroblock_context.previous_qp = macroblock.header.qp;
        cursor = macroblock.end_bit_offset;
    }
    (*decode->frame_has_data) = 1;
    return CAVS_OK;
}
