/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Decoder lifetime and public API state transitions. Syntax and reconstruction
 * are added in separate modules as their conformance coverage becomes ready.
 */
#include <cavs/cavs.h>
#include "syntax.h"
#include "macroblock.h"
#include "prediction.h"
#include "pseudo_start_code.h"
#include "safe.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct cavs_frame_storage {
    cavs_frame public_frame;
    unsigned references;
    cavs_free_fn free;
    void *allocator_opaque;
    uint8_t *buffer;
    uint8_t *planes[3];
} cavs_frame_storage;

struct cavs_decoder {
    cavs_decoder_config config;
    int flushing;
    int end_pending;
    int sequence_pending;
    int has_sequence;
    cavs_sequence_info sequence;
    int has_picture;
    cavs_picture_type picture_type;
    cavs_i_picture_header i_picture;
    cavs_pb_picture_header pb_picture;
    int has_slice;
    cavs_slice_header slice;
    cavs_frame *current_frame;
    cavs_frame *pending_frame;
    int frame_pending;
    int current_frame_has_data;
    uint8_t *luma_modes;
    uint8_t *luma_intra;
    size_t luma_mode_count;
    int64_t picture_pts;
    int64_t picture_dts;
    void *picture_opaque;
    cavs_event_type payload_event_type;
    int payload_pending;
    uint8_t *pending_payload;
    size_t pending_payload_size;
    uint8_t *delivered_payload;
};

/* Adapts the C runtime allocator to the public allocator callback signature. */
static void *default_alloc(void *opaque, size_t size) { (void)opaque; return malloc(size); }

/* Adapts the C runtime deallocator to the public allocator callback signature. */
static void default_free(void *opaque, void *ptr) { (void)opaque; free(ptr); }

/* Releases event payloads whose decoder-owned lifetime has ended. */
static void free_payload(cavs_decoder *decoder, uint8_t **payload) {
    if (*payload != NULL) {
        decoder->config.free(decoder->config.allocator_opaque, *payload);
        *payload = NULL;
    }
}

/* Copies an opaque unit payload so input memory may expire after send_nal. */
static cavs_result queue_payload(cavs_decoder *decoder, cavs_event_type type,
                                 const uint8_t *data, size_t size) {
    uint8_t *copy = NULL;
    if (size != 0U) {
        copy = (uint8_t *)decoder->config.alloc(decoder->config.allocator_opaque, size);
        if (copy == NULL) return CAVS_ERR_OUT_OF_MEMORY;
        memcpy(copy, data, size);
    }
    decoder->payload_event_type = type;
    decoder->payload_pending = 1;
    decoder->pending_payload = copy;
    decoder->pending_payload_size = size;
    return CAVS_OK;
}

/* Removes Annex A prevention bits before parsing standard-defined syntax. */
static cavs_result parse_sequence_payload(cavs_decoder *decoder,
                                          const uint8_t *data, size_t size,
                                          cavs_sequence_info *sequence) {
    uint8_t *decoded;
    size_t output_bits;
    cavs_result result;
    if (size == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    decoded = (uint8_t *)decoder->config.alloc(decoder->config.allocator_opaque, size);
    if (decoded == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    if (!cavs_remove_pseudo_start_codes(data, size, decoded, size, &output_bits)) {
        decoder->config.free(decoder->config.allocator_opaque, decoded);
        return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    result = cavs_parse_sequence_header(decoded, output_bits / 8U, sequence);
    decoder->config.free(decoder->config.allocator_opaque, decoded);
    return result;
}

/* Unescapes and parses an I-picture header without mutating decoder state. */
static cavs_result parse_i_picture_payload(cavs_decoder *decoder,
                                           const uint8_t *data, size_t size,
                                           cavs_i_picture_header *picture) {
    uint8_t *decoded;
    size_t output_bits;
    cavs_result result;
    if (size == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    decoded = (uint8_t *)decoder->config.alloc(decoder->config.allocator_opaque, size);
    if (decoded == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    if (!cavs_remove_pseudo_start_codes(data, size, decoded, size, &output_bits)) {
        decoder->config.free(decoder->config.allocator_opaque, decoded);
        return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    result = cavs_parse_i_picture_header(decoded, output_bits,
                                         &decoder->sequence, picture);
    decoder->config.free(decoder->config.allocator_opaque, decoded);
    return result;
}

/* Unescapes and parses a P/B-picture header without mutating decoder state. */
static cavs_result parse_pb_picture_payload(cavs_decoder *decoder,
                                            const uint8_t *data, size_t size,
                                            cavs_pb_picture_header *picture) {
    uint8_t *decoded;
    size_t output_bits;
    cavs_result result;
    if (size == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    decoded = (uint8_t *)decoder->config.alloc(decoder->config.allocator_opaque, size);
    if (decoded == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    if (!cavs_remove_pseudo_start_codes(data, size, decoded, size, &output_bits)) {
        decoder->config.free(decoder->config.allocator_opaque, decoded);
        return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    result = cavs_parse_pb_picture_header(decoded, output_bits,
                                          &decoder->sequence, picture);
    decoder->config.free(decoder->config.allocator_opaque, decoded);
    return result;
}

/* Builds the syntax context and parses one unescaped slice header. */
static cavs_result parse_slice_payload(cavs_decoder *decoder, uint8_t start_code,
                                       const uint8_t *data, size_t size,
                                       cavs_slice_header *slice,
                                       uint8_t **decoded_payload,
                                       size_t *decoded_bits) {
    cavs_slice_context context;
    uint8_t *decoded;
    size_t output_bits;
    cavs_result result;

    if (size == 0U || decoded_payload == NULL || decoded_bits == NULL)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *decoded_payload = NULL;
    *decoded_bits = 0U;
    memset(&context, 0, sizeof(context));
    context.profile_id = decoder->sequence.profile_id;
    context.vertical_size = decoder->sequence.display_height;
    context.macroblock_height = decoder->sequence.progressive_sequence != 0U ?
        (decoder->sequence.display_height + 15U) / 16U :
        2U * ((decoder->sequence.display_height + 31U) / 32U);
    context.picture_type = decoder->picture_type;
    if (decoder->picture_type == CAVS_PICTURE_I) {
        context.picture_structure = decoder->i_picture.picture_structure;
        context.fixed_picture_qp = decoder->i_picture.fixed_picture_qp;
        context.picture_qp = decoder->i_picture.picture_qp;
        context.advanced_entropy_enabled = decoder->i_picture.advanced_entropy_enabled;
    } else {
        context.picture_structure = decoder->pb_picture.picture_structure;
        context.fixed_picture_qp = decoder->pb_picture.fixed_picture_qp;
        context.picture_qp = decoder->pb_picture.picture_qp;
        context.advanced_entropy_enabled = decoder->pb_picture.advanced_entropy_enabled;
    }

    decoded = (uint8_t *)decoder->config.alloc(decoder->config.allocator_opaque, size);
    if (decoded == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    if (!cavs_remove_pseudo_start_codes(data, size, decoded, size, &output_bits)) {
        decoder->config.free(decoder->config.allocator_opaque, decoded);
        return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    result = cavs_parse_slice_header(start_code, decoded, output_bits,
                                     &context, slice);
    if (result != CAVS_OK) {
        decoder->config.free(decoder->config.allocator_opaque, decoded);
        return result;
    }
    *decoded_payload = decoded;
    *decoded_bits = output_bits;
    return result;
}

/* Returns the private storage prefix for a library-owned output frame. */
static cavs_frame_storage *frame_storage(cavs_frame *frame) {
    return (cavs_frame_storage *)(void *)frame;
}

/* Releases a frame buffer and its storage through the selected allocator. */
static void release_frame(cavs_frame **frame) {
    cavs_frame_storage *storage;
    cavs_free_fn free_fn;
    void *opaque;
    if (frame == NULL || *frame == NULL) return;
    storage = frame_storage(*frame);
    free_fn = storage->free;
    opaque = storage->allocator_opaque;
    *frame = NULL;
    if (storage->buffer != NULL) free_fn(opaque, storage->buffer);
    free_fn(opaque, storage);
}

/* Allocates a padded progressive YUV420 frame for the supported I path. */
static cavs_result allocate_baseline_i_frame(
    cavs_decoder *decoder, cavs_frame **frame) {
    cavs_frame_storage *storage;
    uint32_t coded_width;
    uint32_t coded_height;
    size_t luma_size;
    size_t chroma_size;
    size_t total_size;
    size_t chroma_width;
    size_t chroma_height;
    if (frame == NULL || decoder->sequence.display_width > UINT32_MAX - 15U ||
        decoder->sequence.display_height > UINT32_MAX - 15U)
        return CAVS_ERR_INVALID_ARGUMENT;
    coded_width = (decoder->sequence.display_width + 15U) & ~UINT32_C(15);
    coded_height = (decoder->sequence.display_height + 15U) & ~UINT32_C(15);
    chroma_width = coded_width / 2U;
    chroma_height = coded_height / 2U;
    if (!cavs_size_mul((size_t)coded_width, (size_t)coded_height,
                       &luma_size) ||
        !cavs_size_mul(chroma_width, chroma_height, &chroma_size) ||
        !cavs_size_add(luma_size, chroma_size, &total_size) ||
        !cavs_size_add(total_size, chroma_size, &total_size) ||
        luma_size > (size_t)PTRDIFF_MAX || chroma_width > (size_t)PTRDIFF_MAX)
        return CAVS_ERR_OUT_OF_MEMORY;
    storage = (cavs_frame_storage *)decoder->config.alloc(
        decoder->config.allocator_opaque, sizeof(*storage));
    if (storage == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(storage, 0, sizeof(*storage));
    storage->buffer = (uint8_t *)decoder->config.alloc(
        decoder->config.allocator_opaque, total_size);
    if (storage->buffer == NULL) {
        decoder->config.free(decoder->config.allocator_opaque, storage);
        return CAVS_ERR_OUT_OF_MEMORY;
    }
    storage->free = decoder->config.free;
    storage->allocator_opaque = decoder->config.allocator_opaque;
    storage->references = 1U;
    storage->planes[0] = storage->buffer;
    storage->planes[1] = storage->planes[0] + luma_size;
    storage->planes[2] = storage->planes[1] + chroma_size;
    memset(storage->buffer, 128, total_size);
    storage->public_frame.plane[0] = storage->planes[0];
    storage->public_frame.plane[1] = storage->planes[1];
    storage->public_frame.plane[2] = storage->planes[2];
    storage->public_frame.stride[0] = (ptrdiff_t)coded_width;
    storage->public_frame.stride[1] = (ptrdiff_t)chroma_width;
    storage->public_frame.stride[2] = (ptrdiff_t)chroma_width;
    storage->public_frame.coded_width = coded_width;
    storage->public_frame.coded_height = coded_height;
    storage->public_frame.display_width = decoder->sequence.display_width;
    storage->public_frame.display_height = decoder->sequence.display_height;
    storage->public_frame.format = CAVS_YUV420P8;
    storage->public_frame.picture_type = CAVS_PICTURE_I;
    storage->public_frame.pts = decoder->picture_pts;
    storage->public_frame.dts = decoder->picture_dts;
    storage->public_frame.top_field_first =
        (unsigned)(decoder->i_picture.top_field_first != 0U);
    storage->public_frame.repeat_first_field =
        (unsigned)(decoder->i_picture.repeat_first_field != 0U);
    storage->public_frame.field_picture = 0U;
    *frame = &storage->public_frame;
    return CAVS_OK;
}

/* Returns whether the current picture is the implemented baseline I subset. */
static int baseline_i_supported(const cavs_decoder *decoder) {
    return decoder->sequence.profile_id == UINT8_C(0x20) &&
        decoder->sequence.format == CAVS_YUV420P8 &&
        decoder->sequence.progressive_sequence != 0U &&
        decoder->i_picture.progressive_frame != 0U &&
        decoder->i_picture.picture_structure == 1U &&
        decoder->i_picture.advanced_entropy_enabled == 0U;
}

/* Frees the current picture, including its intra-mode side maps. */
static void discard_current_picture(cavs_decoder *decoder) {
    release_frame(&decoder->current_frame);
    if (decoder->luma_modes != NULL)
        decoder->config.free(decoder->config.allocator_opaque,
                             decoder->luma_modes);
    if (decoder->luma_intra != NULL)
        decoder->config.free(decoder->config.allocator_opaque,
                             decoder->luma_intra);
    decoder->luma_modes = NULL;
    decoder->luma_intra = NULL;
    decoder->luma_mode_count = 0U;
    decoder->current_frame_has_data = 0;
}

/* Queues the current decoded picture for delivery before the next picture. */
static void finish_current_picture(cavs_decoder *decoder) {
    if (decoder->current_frame == NULL) return;
    if (decoder->current_frame_has_data) {
        decoder->pending_frame = decoder->current_frame;
        decoder->current_frame = NULL;
        decoder->frame_pending = 1;
    } else {
        discard_current_picture(decoder);
        return;
    }
    if (decoder->luma_modes != NULL)
        decoder->config.free(decoder->config.allocator_opaque,
                             decoder->luma_modes);
    if (decoder->luma_intra != NULL)
        decoder->config.free(decoder->config.allocator_opaque,
                             decoder->luma_intra);
    decoder->luma_modes = NULL;
    decoder->luma_intra = NULL;
    decoder->luma_mode_count = 0U;
    decoder->current_frame_has_data = 0;
}

/* Starts a baseline I frame and its decoded-neighbor mode maps. */
static cavs_result begin_current_picture(cavs_decoder *decoder) {
    size_t block_width;
    size_t block_height;
    size_t count;
    cavs_result result;
    discard_current_picture(decoder);
    if (!baseline_i_supported(decoder)) return CAVS_OK;
    result = allocate_baseline_i_frame(decoder, &decoder->current_frame);
    if (result != CAVS_OK) return result;
    block_width = decoder->current_frame->coded_width / 8U;
    block_height = decoder->current_frame->coded_height / 8U;
    if (!cavs_size_mul(block_width, block_height, &count)) {
        discard_current_picture(decoder);
        return CAVS_ERR_OUT_OF_MEMORY;
    }
    decoder->luma_modes = (uint8_t *)decoder->config.alloc(
        decoder->config.allocator_opaque, count);
    decoder->luma_intra = (uint8_t *)decoder->config.alloc(
        decoder->config.allocator_opaque, count);
    if (decoder->luma_modes == NULL || decoder->luma_intra == NULL) {
        discard_current_picture(decoder);
        return CAVS_ERR_OUT_OF_MEMORY;
    }
    memset(decoder->luma_modes, 0, count);
    memset(decoder->luma_intra, 0, count);
    decoder->luma_mode_count = count;
    return CAVS_OK;
}

/* Builds the availability mask for a block whose upper/left samples exist. */
static cavs_intra_availability_8x8 block_availability(int top, int left) {
    cavs_intra_availability_8x8 availability;
    memset(&availability, 0, sizeof(availability));
    if (top != 0) availability.top = UINT16_C(0xffff);
    if (left != 0) availability.left = UINT16_C(0xffff);
    availability.top_left = (uint8_t)(top != 0 && left != 0);
    return availability;
}

/* Gets the 8x8 luma mode prediction from already reconstructed neighbors. */
static uint8_t predicted_luma_mode(const cavs_decoder *decoder,
                                   size_t block_x, size_t block_y,
                                   size_t map_width) {
    int left_available = block_x != 0U &&
        decoder->luma_intra[block_y * map_width + block_x - 1U] != 0U;
    int top_available = block_y != 0U &&
        decoder->luma_intra[(block_y - 1U) * map_width + block_x] != 0U;
    uint8_t left = left_available ?
        decoder->luma_modes[block_y * map_width + block_x - 1U] : 2U;
    uint8_t top = top_available ?
        decoder->luma_modes[(block_y - 1U) * map_width + block_x] : 2U;
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
static cavs_result decode_baseline_i_slice(cavs_decoder *decoder,
                                           const uint8_t *data,
                                           size_t bit_size) {
    cavs_baseline420_mb_context context;
    cavs_frame_storage *storage;
    size_t macroblock_width;
    size_t macroblock_height;
    size_t row;
    size_t column;
    size_t cursor;
    uint8_t chroma_qp;
    if (decoder->current_frame == NULL) return CAVS_OK;
    if (data == NULL || decoder->slice.header_bits > bit_size)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    storage = frame_storage(decoder->current_frame);
    macroblock_width = decoder->current_frame->coded_width / 16U;
    macroblock_height = decoder->current_frame->coded_height / 16U;
    row = decoder->slice.macroblock_row;
    if (row >= macroblock_height) return CAVS_ERR_CORRUPT_BITSTREAM;
    /* A header-only unit is valid for API state-machine callers; the final
     * byte may also contain one zero alignment bit after the slice header. */
    if (!decoder->current_frame_has_data &&
        bit_size - decoder->slice.header_bits <= 8U)
        return CAVS_OK;
    memset(&context, 0, sizeof(context));
    context.profile_id = UINT8_C(0x20);
    context.format = CAVS_YUV420P8;
    context.picture_type = CAVS_PICTURE_I;
    context.picture_structure = 1U;
    context.skip_mode_flag = decoder->i_picture.skip_mode_flag;
    context.picture_reference_flag = 1U;
    context.fixed_qp = decoder->slice.fixed_slice_qp;
    context.previous_qp = decoder->slice.slice_qp;
    context.reference_index_bits = 1U;
    context.mb_weighting_flag = decoder->slice.mb_weighting_flag;
    context.macroblock_width = (uint32_t)macroblock_width;
    context.macroblock_height = (uint32_t)macroblock_height;
    cursor = decoder->slice.header_bits;
    for (column = 0U; column < macroblock_width; ++column) {
        cavs_baseline420_macroblock macroblock;
        cavs_intra_references_8x8 chroma_references[2];
        cavs_intra_availability_8x8 availability;
        size_t block_width = macroblock_width * 2U;
        size_t macroblock_x = column * 16U;
        size_t macroblock_y = row * 16U;
        unsigned index;
        cavs_result result;

        context.macroblock_index = (uint32_t)(row * macroblock_width + column);
        result = cavs_decode_baseline420_macroblock(
            data, bit_size, cursor, &context, &macroblock);
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
                storage->planes[0], decoder->current_frame->coded_width,
                decoder->current_frame->coded_height,
                (size_t)decoder->current_frame->stride[0], block_x, block_y,
                &availability, &references);
            if (result != CAVS_OK) return result;
            predicted_mode = predicted_luma_mode(
                decoder, map_x, map_y, block_width);
            result = resolved_luma_mode(&macroblock.header, index,
                                        predicted_mode, &mode);
            if (result != CAVS_OK) return result;
            result = cavs_predict_intra_luma_8x8(
                &references, (cavs_intra_luma_mode_8x8)mode, prediction);
            if (result != CAVS_OK) return result;
            result = cavs_reconstruct_baseline420_block(
                &macroblock.block[index], macroblock.block_coded[index],
                macroblock.header.qp, CAVS_SCAN_8X8_FRAME, prediction, NULL,
                reconstructed);
            if (result != CAVS_OK) return result;
            store_block(storage->planes[0],
                        (size_t)decoder->current_frame->stride[0], block_x,
                        block_y, reconstructed);
            decoder->luma_modes[map_y * block_width + map_x] = mode;
            decoder->luma_intra[map_y * block_width + map_x] = 1U;
        }
        for (index = 0U; index < 2U; ++index) {
            uint8_t prediction[64];
            uint8_t reconstructed[64];
            availability = block_availability(row != 0U, column != 0U);
            result = cavs_acquire_intra_references_8x8(
                storage->planes[index + 1U],
                decoder->current_frame->coded_width / 2U,
                decoder->current_frame->coded_height / 2U,
                (size_t)decoder->current_frame->stride[index + 1U],
                column * 8U, row * 8U, &availability,
                &chroma_references[index]);
            if (result != CAVS_OK) return result;
            result = cavs_predict_intra_chroma_8x8(
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
            store_block(storage->planes[index + 1U],
                        (size_t)decoder->current_frame->stride[index + 1U],
                        column * 8U, row * 8U,
                        reconstructed);
        }
        context.previous_qp = macroblock.header.qp;
        cursor = macroblock.end_bit_offset;
    }
    decoder->current_frame_has_data = 1;
    return CAVS_OK;
}

/* Validates configuration and creates an empty decoder state. */
cavs_result cavs_decoder_create(const cavs_decoder_config *config, cavs_decoder **out) {
    cavs_decoder_config cfg;
    cavs_decoder *decoder;
    if (out == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    memset(&cfg, 0, sizeof(cfg));
    if (config != NULL) cfg = *config;
    if ((cfg.alloc == NULL) != (cfg.free == NULL)) return CAVS_ERR_INVALID_ARGUMENT;
    if (cfg.alloc == NULL) { cfg.alloc = default_alloc; cfg.free = default_free; }
    decoder = (cavs_decoder *)cfg.alloc(cfg.allocator_opaque, sizeof(*decoder));
    if (decoder == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(decoder, 0, sizeof(*decoder)); decoder->config = cfg; *out = decoder;
    return CAVS_OK;
}

/* Validates unit framing before dispatching to profile-specific syntax code. */
cavs_result cavs_decoder_send_nal(cavs_decoder *decoder, const cavs_packet *packet) {
    cavs_unit_type unit_type;
    cavs_sequence_info sequence;
    cavs_i_picture_header picture;
    cavs_pb_picture_header pb_picture;
    cavs_slice_header slice;
    cavs_result result;
    if (decoder == NULL || packet == NULL || packet->data == NULL || packet->size < 4U) return CAVS_ERR_INVALID_ARGUMENT;
    if (decoder->flushing) return CAVS_ERR_INVALID_STATE;
    if (packet->data[0] != 0U || packet->data[1] != 0U || packet->data[2] != 1U) return CAVS_ERR_CORRUPT_BITSTREAM;
    if (decoder->sequence_pending || decoder->payload_pending ||
        decoder->frame_pending) return CAVS_AGAIN;
    unit_type = cavs_classify_start_code(packet->data[3]);
    if (unit_type == CAVS_UNIT_SEQUENCE_END) {
        if (decoder->has_picture) finish_current_picture(decoder);
        decoder->flushing = 1;
        decoder->end_pending = 1;
        return CAVS_OK;
    }
    if (unit_type == CAVS_UNIT_USER_DATA)
        return queue_payload(decoder, CAVS_EVENT_METADATA,
                             packet->data + 4U, packet->size - 4U);
    if (unit_type == CAVS_UNIT_EXTENSION)
        return queue_payload(decoder, CAVS_EVENT_RAW_EXTENSION,
                             packet->data + 4U, packet->size - 4U);
    if (unit_type == CAVS_UNIT_I_PICTURE) {
        if (decoder->has_picture) finish_current_picture(decoder);
        if (!decoder->has_sequence) return CAVS_ERR_INVALID_STATE;
        result = parse_i_picture_payload(decoder, packet->data + 4U,
                                         packet->size - 4U, &picture);
        if (result != CAVS_OK) return result;
        decoder->i_picture = picture;
        decoder->picture_type = CAVS_PICTURE_I;
        decoder->picture_pts = packet->pts;
        decoder->picture_dts = packet->dts;
        decoder->picture_opaque = packet->opaque;
        decoder->has_picture = 1;
        decoder->has_slice = 0;
        result = begin_current_picture(decoder);
        if (result != CAVS_OK) {
            decoder->has_picture = 0;
            return result;
        }
        return CAVS_OK;
    }
    if (unit_type == CAVS_UNIT_PB_PICTURE) {
        if (decoder->has_picture) finish_current_picture(decoder);
        if (!decoder->has_sequence) return CAVS_ERR_INVALID_STATE;
        result = parse_pb_picture_payload(decoder, packet->data + 4U,
                                          packet->size - 4U, &pb_picture);
        if (result != CAVS_OK) return result;
        decoder->pb_picture = pb_picture;
        decoder->picture_type = pb_picture.picture_coding_type == 1U ?
                                CAVS_PICTURE_P : CAVS_PICTURE_B;
        decoder->picture_pts = packet->pts;
        decoder->picture_dts = packet->dts;
        decoder->picture_opaque = packet->opaque;
        decoder->has_picture = 1;
        decoder->has_slice = 0;
        discard_current_picture(decoder);
        return CAVS_OK;
    }
    if (unit_type == CAVS_UNIT_SLICE) {
        uint8_t *decoded = NULL;
        size_t decoded_bits = 0U;
        if (!decoder->has_picture) return CAVS_ERR_INVALID_STATE;
        result = parse_slice_payload(decoder, packet->data[3],
                                     packet->data + 4U, packet->size - 4U,
                                     &slice, &decoded, &decoded_bits);
        if (result != CAVS_OK) return result;
        decoder->slice = slice;
        if (decoder->current_frame != NULL) {
            result = decode_baseline_i_slice(decoder, decoded, decoded_bits);
            decoder->config.free(decoder->config.allocator_opaque, decoded);
            if (result != CAVS_OK) return result;
        } else {
            decoder->config.free(decoder->config.allocator_opaque, decoded);
        }
        decoder->has_slice = 1;
        return CAVS_OK;
    }
    if (unit_type != CAVS_UNIT_SEQUENCE_HEADER) return CAVS_ERR_UNSUPPORTED_PROFILE;
    result = parse_sequence_payload(decoder, packet->data + 4U,
                                    packet->size - 4U, &sequence);
    if (result != CAVS_OK) return result;
    if (!decoder->has_sequence || memcmp(&decoder->sequence, &sequence, sizeof(sequence)) != 0) {
        decoder->sequence = sequence;
        decoder->has_sequence = 1;
        decoder->has_picture = 0;
        decoder->has_slice = 0;
        decoder->sequence_pending = 1;
    }
    return CAVS_OK;
}

/* Returns queued events and models the terminal drain state. */
cavs_result cavs_decoder_receive_event(cavs_decoder *decoder, cavs_event *event) {
    if (decoder == NULL || event == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    free_payload(decoder, &decoder->delivered_payload);
    memset(event, 0, sizeof(*event));
    if (decoder->sequence_pending) {
        decoder->sequence_pending = 0;
        event->type = CAVS_EVENT_SEQUENCE;
        event->sequence = decoder->sequence;
        return CAVS_OK;
    }
    if (decoder->frame_pending) {
        decoder->frame_pending = 0;
        event->type = CAVS_EVENT_FRAME;
        event->frame = decoder->pending_frame;
        decoder->pending_frame = NULL;
        return CAVS_OK;
    }
    if (decoder->payload_pending) {
        decoder->delivered_payload = decoder->pending_payload;
        decoder->pending_payload = NULL;
        decoder->payload_pending = 0;
        event->type = decoder->payload_event_type;
        event->data = decoder->delivered_payload;
        event->size = decoder->pending_payload_size;
        decoder->pending_payload_size = 0U;
        return CAVS_OK;
    }
    if (decoder->end_pending) { decoder->end_pending = 0; event->type = CAVS_EVENT_END; return CAVS_OK; }
    return decoder->flushing ? CAVS_EOF : CAVS_AGAIN;
}

/* Enters the drain state exactly once and schedules an end event. */
cavs_result cavs_decoder_flush(cavs_decoder *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (decoder->has_picture) {
        finish_current_picture(decoder);
        decoder->has_picture = 0;
        decoder->has_slice = 0;
    }
    if (!decoder->flushing) { decoder->flushing = 1; decoder->end_pending = 1; }
    return CAVS_OK;
}

/* Restores a decoder to its initial input-accepting state. */
cavs_result cavs_decoder_reset(cavs_decoder *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    discard_current_picture(decoder);
    release_frame(&decoder->pending_frame);
    decoder->frame_pending = 0;
    decoder->flushing = 0;
    decoder->end_pending = 0;
    decoder->sequence_pending = 0;
    decoder->has_sequence = 0;
    decoder->has_picture = 0;
    decoder->has_slice = 0;
    free_payload(decoder, &decoder->pending_payload);
    free_payload(decoder, &decoder->delivered_payload);
    decoder->payload_pending = 0;
    decoder->pending_payload_size = 0U;
    memset(&decoder->sequence, 0, sizeof(decoder->sequence));
    decoder->picture_type = CAVS_PICTURE_I;
    memset(&decoder->i_picture, 0, sizeof(decoder->i_picture));
    memset(&decoder->pb_picture, 0, sizeof(decoder->pb_picture));
    memset(&decoder->slice, 0, sizeof(decoder->slice));
    decoder->picture_pts = 0;
    decoder->picture_dts = 0;
    decoder->picture_opaque = NULL;
    return CAVS_OK;
}

/* Releases the decoder through the allocator selected at creation. */
void cavs_decoder_destroy(cavs_decoder *decoder) {
    if (decoder != NULL) {
        cavs_free_fn free_fn = decoder->config.free;
        void *opaque = decoder->config.allocator_opaque;
        discard_current_picture(decoder);
        release_frame(&decoder->pending_frame);
        free_payload(decoder, &decoder->pending_payload);
        free_payload(decoder, &decoder->delivered_payload);
        free_fn(opaque, decoder);
    }
}

/* Increments a frame reference unless its counter is saturated. */
cavs_frame *cavs_frame_ref(cavs_frame *frame) {
    cavs_frame_storage *storage;
    if (frame == NULL) return NULL;
    storage = (cavs_frame_storage *)(void *)frame;
    if (storage->references == UINT_MAX) return NULL;
    ++storage->references;
    return frame;
}

/* Drops one frame reference and releases storage at the last reference. */
void cavs_frame_unref(cavs_frame **frame) {
    cavs_frame_storage *storage;
    if (frame == NULL || *frame == NULL) return;
    storage = (cavs_frame_storage *)(void *)*frame;
    *frame = NULL;
    if (--storage->references == 0U) {
        if (storage->buffer != NULL)
            storage->free(storage->allocator_opaque, storage->buffer);
        storage->free(storage->allocator_opaque, storage);
    }
}

/* Returns the development version of this pre-conformance implementation. */
const char *cavs_version(void) { return "0.1.0-dev"; }

/* Maps every public result code to a stable diagnostic string. */
const char *cavs_strerror(cavs_result result) {
    switch (result) {
    case CAVS_OK: return "success"; case CAVS_AGAIN: return "try again"; case CAVS_EOF: return "end of stream";
    case CAVS_ERR_INVALID_ARGUMENT: return "invalid argument"; case CAVS_ERR_OUT_OF_MEMORY: return "out of memory";
    case CAVS_ERR_CORRUPT_BITSTREAM: return "corrupt bitstream"; case CAVS_ERR_UNSUPPORTED_PROFILE: return "unsupported profile";
    case CAVS_ERR_UNSUPPORTED_LEVEL: return "unsupported level"; case CAVS_ERR_INVALID_STATE: return "invalid state";
    case CAVS_ERR_MISSING_REFERENCE: return "missing reference picture"; default: return "unknown error";
    }
}
