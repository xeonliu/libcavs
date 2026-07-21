/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Public-API and baseline macroblock fuzz entry point.
 */
#include <cavs/cavs.h>
#include "coefficients.h"
#include "macroblock.h"
#include "motion.h"
#include "prediction.h"
#include "reconstruction.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef CAVS_AFL_STANDALONE
#include <stdio.h>
#endif

/* Routes arbitrary bytes through framing, syntax dispatch, drain, and cleanup. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    cavs_decoder *decoder = NULL;
    cavs_packet packet;
    cavs_event event;
    uint8_t *nal;
    size_t nal_size;
    if (size == 0U || size > SIZE_MAX - 3U) return 0;
    nal_size = size + 3U;
    nal = (uint8_t *)malloc(nal_size);
    if (nal == NULL) return 0;
    nal[0] = 0U;
    nal[1] = 0U;
    nal[2] = 1U;
    memcpy(nal + 3U, data, size);
    if (cavs_decoder_create(NULL, &decoder) == CAVS_OK) {
        packet.data = nal;
        packet.size = nal_size;
        packet.pts = 0;
        packet.dts = 0;
        packet.opaque = NULL;
        (void)cavs_decoder_send_nal(decoder, &packet);
        while (cavs_decoder_receive_event(decoder, &event) == CAVS_OK) {
        }
        (void)cavs_decoder_flush(decoder);
        while (cavs_decoder_receive_event(decoder, &event) == CAVS_OK) {
        }
        cavs_decoder_destroy(decoder);
    }
    free(nal);

    if (size <= SIZE_MAX / 8U) {
        cavs_basic_coefficients coefficients;
        cavs_baseline420_mb_context context;
        cavs_baseline420_mb_header header;
        memset(&context, 0, sizeof(context));
        context.profile_id = UINT8_C(0x20);
        context.format = CAVS_YUV420P8;
        context.picture_type = (cavs_picture_type)(data[0] % 3U);
        context.picture_structure = size > 1U ? data[1] & 1U : 1U;
        context.skip_mode_flag = size > 2U ? data[2] & 1U : 0U;
        context.picture_reference_flag = size > 3U ? data[3] & 1U : 1U;
        context.fixed_qp = size > 4U ? data[4] & 1U : 0U;
        context.previous_qp = size > 5U ? data[5] & 63U : 0U;
        context.reference_index_bits = size > 6U ? (data[6] & 1U) + 1U : 1U;
        context.mb_weighting_flag = size > 7U ? data[7] & 1U : 0U;
        context.macroblock_width = 2U;
        context.macroblock_height = 2U;
        context.macroblock_index = size > 8U ? data[8] & 3U : 0U;
        (void)cavs_parse_baseline420_mb_header(data, size * 8U, 0U,
                                               &context, &header);
        (void)cavs_decode_basic_coefficients_8x8(
            data, size * 8U, 0U,
            (cavs_basic_block_kind)(data[0] % 3U), &coefficients);
    }
    {
        int32_t quant[64];
        int32_t predicted[64];
        int32_t matrix[64];
        int16_t residual[64];
        cavs_intra_references_8x8 references;
        cavs_intra_availability_8x8 availability;
        uint8_t reference_plane[16U * 16U];
        uint8_t prediction[64];
        uint8_t reconstructed[64];
        uint8_t motion_prediction[64];
        uint8_t weights[64];
        unsigned index;
        for (index = 0U; index < 64U; ++index) {
            uint8_t byte = data[index % size];
            quant[index] = (int32_t)(int8_t)byte;
            predicted[index] = 0;
            weights[index] = data[(index * 13U) % size];
        }
        (void)cavs_inverse_scan_8x8(
            quant, (cavs_scan_mode_8x8)(data[0] & 1U), matrix);
        (void)cavs_inverse_quantize_8x8(
            matrix, predicted, weights, data[0] & 63U, quant);
        (void)cavs_inverse_transform_8x8(quant, residual);
        memset(&references, 0, sizeof(references));
        references.top_available = UINT32_C(0x1ffff);
        references.left_available = UINT32_C(0x1ffff);
        for (index = 0U; index < 17U; ++index) {
            references.top[index] = data[index % size];
            references.left[index] = data[(index * 7U) % size];
        }
        references.left[0] = references.top[0];
        for (index = 0U; index < sizeof(reference_plane); ++index)
            reference_plane[index] = data[index % size];
        availability.top = size > 1U ? (uint16_t)(data[0] | data[1] << 8U) : 0U;
        availability.left = size > 2U ? (uint16_t)(data[1] | data[2] << 8U) : 0U;
        availability.top_left = data[0] & 1U;
        (void)cavs_acquire_intra_references_8x8(
            reference_plane, 16U, 16U, 16U, 8U, 8U,
            &availability, &references);
        (void)cavs_predict_intra_luma_8x8(
            &references, (cavs_intra_luma_mode_8x8)(data[0] % 5U), prediction);
        (void)cavs_predict_intra_chroma_8x8(
            &references, (cavs_intra_chroma_mode_8x8)(data[0] % 4U), prediction);
        (void)cavs_reconstruct_samples_8x8(
            prediction, data[0] & 1U ? prediction : NULL,
            residual, reconstructed);
        (void)cavs_interpolate_chroma_block(
            reference_plane, 16U, 16U, 16U, 4U, 4U, 8U, 8U,
            (int8_t)data[0], (int8_t)data[size - 1U],
            data[0] & 1U ? CAVS_CHROMA_MOTION_EIGHTH
                         : CAVS_CHROMA_MOTION_SIXTEENTH,
            motion_prediction, 8U);
    }
    return 0;
}

#ifdef CAVS_AFL_STANDALONE
/* Reads one AFL test case from standard input and invokes the shared harness. */
int main(void) {
    const size_t capacity = 16U * 1024U * 1024U;
    uint8_t *data = (uint8_t *)malloc(capacity);
    size_t size;
    int result;
    if (data == NULL) return 1;
    size = fread(data, 1U, capacity, stdin);
    result = LLVMFuzzerTestOneInput(data, size);
    free(data);
    return result;
}
#endif
