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
#include "image.h"
#include "broadcast_macroblock.h"
#include "broadcast_motion.h"
#include "broadcast_reconstruction.h"
#include "slice_decode.h"
#include "loop_filter.h"
#include "dpb.h"
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
    cavs_picture picture;
    cavs_macroblock *macroblocks;
    uint8_t filtered_fields;
} cavs_frame_storage;

typedef struct cavs_broadcast_reader_state {
    cavs_broadcast_slice_decoder entropy;
    const cavs_dpb *dpb;
    cavs_picture *picture;
    cavs_picture_type picture_type;
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t skip_mode_flag;
    uint8_t picture_reference_flag;
    uint8_t fixed_qp;
    uint8_t previous_qp;
    int8_t previous_qp_delta;
    uint8_t mb_weighting_flag;
    uint16_t slice_id;
    uint16_t start_row;
    uint32_t skip_remaining;
    uint8_t explicit_pending;
    size_t last_bit_offset;
    cavs_broadcast_motion_context motion;
    cavs_macroblock_prediction_420 *prediction;
    cavs_macroblock *entropy_row;
} cavs_broadcast_reader_state;

typedef struct cavs_motion_profile_capability {
    cavs_luma_motion_precision luma_precision;
    uint8_t motion_unit;
} cavs_motion_profile_capability;

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
    uint16_t next_slice_id;
    cavs_macroblock_prediction_420 broadcast_prediction;
    cavs_dpb dpb;
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

/* Returns private storage from the decoder-owned picture adapter. */
static cavs_frame_storage *picture_storage(cavs_picture *picture) {
    return picture == NULL ? NULL : (cavs_frame_storage *)picture->owner;
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
    if (storage->references == 0U) return;
    --storage->references;
    if (storage->references == 0U) {
        if (storage->macroblocks != NULL)
            free_fn(opaque, storage->macroblocks);
        if (storage->buffer != NULL) free_fn(opaque, storage->buffer);
        free_fn(opaque, storage);
    }
}

/* Retains a picture through its decoder-owned frame storage adapter. */
static void dpb_retain_picture(void *opaque, cavs_picture *picture) {
    cavs_frame_storage *storage = picture_storage(picture);
    (void)opaque;
    if (storage != NULL && storage->references != UINT_MAX)
        ++storage->references;
}

/* Releases a DPB-owned picture reference; no codec decision is introduced. */
static void dpb_release_picture(void *opaque, cavs_picture *picture) {
    cavs_frame_storage *storage = picture_storage(picture);
    cavs_frame *frame;
    (void)opaque;
    if (storage == NULL) return;
    frame = &storage->public_frame;
    release_frame(&frame);
}

/* Transfers one ready DPB output reference to the public event slot. */
static cavs_result queue_next_dpb_frame(cavs_decoder *decoder) {
    cavs_picture *picture;
    cavs_frame_storage *storage;
    cavs_result result;
    if (decoder->frame_pending) return CAVS_OK;
    result = cavs_dpb_pop_output(&decoder->dpb, &picture);
    if (result == CAVS_AGAIN || result == CAVS_EOF) return CAVS_OK;
    if (result != CAVS_OK) return result;
    storage = picture_storage(picture);
    if (storage == NULL) return CAVS_ERR_INVALID_STATE;
    decoder->pending_frame = &storage->public_frame;
    decoder->frame_pending = 1;
    return CAVS_OK;
}

/*
 * Allocates one decoder-owned YUV420 frame and metadata map. Buffer layout and
 * ownership introduce no codec decision.
 */
static cavs_result allocate_current_frame(
    cavs_decoder *decoder, cavs_frame **frame) {
    cavs_frame_storage *storage;
    uint32_t coded_width;
    uint32_t coded_height;
    size_t luma_size;
    size_t chroma_size;
    size_t total_size;
    size_t chroma_width;
    size_t chroma_height;
    size_t macroblock_count;
    uint32_t macroblock_height;
    uint8_t picture_structure;
    uint8_t top_field_first;
    uint8_t repeat_first_field;
    uint8_t picture_distance;
    if (frame == NULL || decoder->sequence.display_width > UINT32_MAX - 15U ||
        decoder->sequence.display_height > UINT32_MAX - 15U)
        return CAVS_ERR_INVALID_ARGUMENT;
    coded_width = (decoder->sequence.display_width + 15U) & ~UINT32_C(15);
    coded_height = decoder->sequence.progressive_sequence != 0U ?
        (decoder->sequence.display_height + 15U) & ~UINT32_C(15) :
        (decoder->sequence.display_height + 31U) & ~UINT32_C(31);
    chroma_width = coded_width / 2U;
    chroma_height = coded_height / 2U;
    if (!cavs_size_mul((size_t)coded_width, (size_t)coded_height,
                       &luma_size) ||
        !cavs_size_mul(chroma_width, chroma_height, &chroma_size) ||
        !cavs_size_add(luma_size, chroma_size, &total_size) ||
        !cavs_size_add(total_size, chroma_size, &total_size) ||
        luma_size > (size_t)PTRDIFF_MAX || chroma_width > (size_t)PTRDIFF_MAX)
        return CAVS_ERR_OUT_OF_MEMORY;
    macroblock_height = decoder->sequence.progressive_sequence != 0U ?
        coded_height / 16U : 2U * (coded_height / 32U);
    if (!cavs_size_mul((size_t)(coded_width / 16U),
                       (size_t)macroblock_height, &macroblock_count) ||
        macroblock_count > SIZE_MAX / sizeof(cavs_macroblock))
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
    storage->macroblocks = (cavs_macroblock *)decoder->config.alloc(
        decoder->config.allocator_opaque,
        macroblock_count * sizeof(*storage->macroblocks));
    if (storage->macroblocks == NULL) {
        decoder->config.free(decoder->config.allocator_opaque,
                             storage->buffer);
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
    memset(storage->macroblocks, 0,
           macroblock_count * sizeof(*storage->macroblocks));
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
    storage->public_frame.picture_type = decoder->picture_type;
    storage->public_frame.pts = decoder->picture_pts;
    storage->public_frame.dts = decoder->picture_dts;
    if (decoder->picture_type == CAVS_PICTURE_I) {
        picture_structure = decoder->i_picture.picture_structure;
        top_field_first = decoder->i_picture.top_field_first;
        repeat_first_field = decoder->i_picture.repeat_first_field;
        picture_distance = decoder->i_picture.picture_distance;
    } else {
        picture_structure = decoder->pb_picture.picture_structure;
        top_field_first = decoder->pb_picture.top_field_first;
        repeat_first_field = decoder->pb_picture.repeat_first_field;
        picture_distance = decoder->pb_picture.picture_distance;
    }
    storage->public_frame.top_field_first = (unsigned)(top_field_first != 0U);
    storage->public_frame.repeat_first_field =
        (unsigned)(repeat_first_field != 0U);
    storage->public_frame.field_picture =
        (unsigned)(picture_structure == 0U);
    storage->picture.plane[0] = storage->planes[0];
    storage->picture.plane[1] = storage->planes[1];
    storage->picture.plane[2] = storage->planes[2];
    storage->picture.stride[0] = (ptrdiff_t)coded_width;
    storage->picture.stride[1] = (ptrdiff_t)chroma_width;
    storage->picture.stride[2] = (ptrdiff_t)chroma_width;
    storage->picture.coded_width = coded_width;
    storage->picture.coded_height = coded_height;
    storage->picture.display_width = decoder->sequence.display_width;
    storage->picture.display_height = decoder->sequence.display_height;
    storage->picture.format = CAVS_YUV420P8;
    storage->picture.picture_type = decoder->picture_type;
    storage->picture.picture_distance = picture_distance;
    storage->picture.pts = decoder->picture_pts;
    storage->picture.dts = decoder->picture_dts;
    storage->picture.macroblock_width = (uint16_t)(coded_width / 16U);
    storage->picture.macroblock_height = (uint16_t)macroblock_height;
    storage->picture.macroblocks = storage->macroblocks;
    storage->picture.macroblock_count = macroblock_count;
    storage->picture.top_field_first = top_field_first;
    storage->picture.repeat_first_field = repeat_first_field;
    storage->picture.field_picture = (uint8_t)(picture_structure == 0U);
    storage->picture.is_reference =
        (uint8_t)(decoder->picture_type != CAVS_PICTURE_B);
    storage->picture.owner = storage;
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

/* Returns whether the current header selects the scoped broadcast subset. */
static int broadcast_picture_supported(const cavs_decoder *decoder) {
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t advanced_entropy;
    uint8_t weighting_quant;
    if (decoder->sequence.profile_id != UINT8_C(0x48) ||
        decoder->sequence.format != CAVS_YUV420P8 ||
        decoder->sequence.progressive_sequence != 0U)
        return 0;
    if (decoder->picture_type == CAVS_PICTURE_I) {
        progressive_frame = decoder->i_picture.progressive_frame;
        picture_structure = decoder->i_picture.picture_structure;
        advanced_entropy = decoder->i_picture.advanced_entropy_enabled;
        weighting_quant = decoder->i_picture.weighting_quant_flag;
    } else {
        progressive_frame = decoder->pb_picture.progressive_frame;
        picture_structure = decoder->pb_picture.picture_structure;
        advanced_entropy = decoder->pb_picture.advanced_entropy_enabled;
        weighting_quant = decoder->pb_picture.weighting_quant_flag;
    }
    return progressive_frame == 0U && picture_structure == 0U &&
        advanced_entropy != 0U && weighting_quant == 0U;
}

/*
 * GB/T 20090.2-2013 9.10.2 and GB/T 20090.16-2016 9.9.2 use quarter-sample
 * luma motion for the currently supported profiles. Eighth-sample motion is
 * selected only by profiles whose explicit advanced-subpixel syntax has been
 * parsed; those profiles are outside the current sequence parser.
 */
static cavs_result decoder_motion_profile_capability(
    const cavs_decoder *decoder, cavs_motion_profile_capability *capability) {
    if (decoder == NULL || capability == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    switch (decoder->sequence.profile_id) {
    case UINT8_C(0x20):
    case UINT8_C(0x48):
        capability->luma_precision = CAVS_LUMA_MOTION_QUARTER;
        capability->motion_unit = 4U;
        return CAVS_OK;
    default:
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    }
}

/* Returns the physical field represented by a successive-field syntax row. */
static cavs_result broadcast_field_for_row(const cavs_picture *picture,
                                           uint16_t row, uint8_t *field) {
    uint16_t field_rows;
    uint8_t first;
    if (picture == NULL || field == NULL || picture->field_picture == 0U ||
        (picture->macroblock_height & 1U) != 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    field_rows = (uint16_t)(picture->macroblock_height / 2U);
    if (row != 0U && row != field_rows)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    first = picture->top_field_first != 0U ? CAVS_FIELD_TOP :
                                             CAVS_FIELD_BOTTOM;
    *field = row == 0U ? first :
        (first == CAVS_FIELD_TOP ? CAVS_FIELD_BOTTOM : CAVS_FIELD_TOP);
    return CAVS_OK;
}

/* Expands one run-coded skipped macroblock without inventing source bits. */
static void broadcast_skipped_macroblock(
    cavs_broadcast_reader_state *reader, uint32_t address,
    cavs_macroblock *macroblock) {
    memset(macroblock, 0, sizeof(*macroblock));
    macroblock->address = address;
    macroblock->row = (uint16_t)(address /
        reader->picture->macroblock_width);
    macroblock->column = (uint16_t)(address %
        reader->picture->macroblock_width);
    macroblock->slice_id = reader->slice_id;
    macroblock->type = reader->picture_type == CAVS_PICTURE_B ?
        CAVS_MB_B_SKIP : CAVS_MB_P_SKIP;
    macroblock->raw_type = 0U;
    macroblock->is_skipped = 1U;
    macroblock->transform_8x8 = 1U;
    macroblock->partition_count = 1U;
    macroblock->partition[0].width = 16U;
    macroblock->partition[0].height = 16U;
    macroblock->partition[0].direction =
        reader->picture_type == CAVS_PICTURE_B ?
        CAVS_PRED_BIDIRECTIONAL : CAVS_PRED_FORWARD;
    macroblock->qp = reader->previous_qp;
    macroblock->end_bit_offset = reader->last_bit_offset;
}

/* Copies the current field's ordered DPB references into motion state. */
static cavs_result broadcast_motion_references(
    const cavs_dpb *dpb, uint8_t field,
    cavs_broadcast_motion_context *motion) {
    unsigned direction;
    unsigned index;
    if (dpb == NULL || motion == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
        for (index = 0U; index < CAVS_BROADCAST_REFERENCE_COUNT; ++index) {
            cavs_dpb_reference reference;
            cavs_result result = cavs_dpb_select_reference_entry(
                dpb, (uint8_t)direction, (uint8_t)index, field, &reference);
            if (result == CAVS_ERR_MISSING_REFERENCE) break;
            if (result != CAVS_OK) return result;
            motion->reference[direction][index].picture = reference.picture;
            motion->reference[direction][index].distance_index =
                reference.distance;
            motion->reference[direction][index].block_distance =
                reference.block_distance;
            motion->reference[direction][index].field = reference.field;
            motion->reference[direction][index].valid = 1U;
        }
    }
    return CAVS_OK;
}

/* Returns the committed partition covering one co-located 8x8 center. */
static const cavs_mb_partition *broadcast_partition_at(
    const cavs_macroblock *macroblock, uint8_t block) {
    unsigned index;
    uint8_t x = (uint8_t)((block & 1U) * 8U + 4U);
    uint8_t y = (uint8_t)((block >> 1U) * 8U + 4U);
    if (macroblock == NULL || block >= CAVS_MB_MOTION_BLOCKS)
        return NULL;
    for (index = 0U; index < macroblock->partition_count; ++index) {
        const cavs_mb_partition *partition = &macroblock->partition[index];
        if (x >= partition->x &&
            (unsigned)x < (unsigned)partition->x + partition->width &&
            y >= partition->y &&
            (unsigned)y < (unsigned)partition->y + partition->height)
            return partition;
    }
    return NULL;
}

/* Returns whether one entropy macroblock contains direct-mode partitions. */
static int broadcast_macroblock_uses_direct(
    const cavs_macroblock *macroblock) {
    unsigned index;
    if (macroblock->type == CAVS_MB_B_SKIP ||
        macroblock->type == CAVS_MB_B_DIRECT)
        return 1;
    if (macroblock->type != CAVS_MB_B_INTER) return 0;
    for (index = 0U; index < macroblock->partition_count; ++index) {
        const cavs_mb_partition *partition = &macroblock->partition[index];
        if (partition->direction == CAVS_PRED_BIDIRECTIONAL &&
            partition->motion[CAVS_PRED_FORWARD].valid == 0U &&
            partition->motion[CAVS_PRED_BACKWARD].valid == 0U)
            return 1;
    }
    return 0;
}

/*
 * GB/T 20090.16-2016 9.6.1 assigns DistanceIndex and BlockDistance to the
 * reference block selected while decoding each motion vector. Clause 9.9.1
 * b) later requires the co-located P vector's DistanceIndexRef (and, for
 * field correction, its physical field). A numeric reference_index is only
 * meaningful in that P field's transient list, so freeze the resolved
 * identity before a newer reference picture can replace an older DPB entry.
 */
static cavs_result broadcast_freeze_motion_references(
    const cavs_broadcast_motion_context *context,
    cavs_macroblock *macroblock) {
    unsigned partition_index;
    if (context == NULL || macroblock == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (partition_index = 0U;
         partition_index < macroblock->partition_count; ++partition_index) {
        unsigned direction;
        for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
            cavs_motion_vector *motion =
                &macroblock->partition[partition_index].motion[direction];
            const cavs_broadcast_reference *reference;
            if (motion->valid == 0U) continue;
            if (motion->valid > 1U || motion->reference_index < 0 ||
                motion->reference_index >=
                    (int8_t)CAVS_BROADCAST_REFERENCE_COUNT)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            reference = &context->reference[direction]
                [(unsigned)motion->reference_index];
            if (reference->valid == 0U)
                return CAVS_ERR_MISSING_REFERENCE;
            if (reference->valid > 1U || reference->distance_index >= 512U ||
                (reference->field != CAVS_FIELD_TOP &&
                 reference->field != CAVS_FIELD_BOTTOM &&
                 reference->field != CAVS_FIELD_BOTH))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            motion->reference_distance_index = reference->distance_index;
            motion->reference_field = reference->field;
            motion->reference_identity_valid = 1U;
        }
    }
    return CAVS_OK;
}

/*
 * Implements GB/T 20090.16-2016 9.9.1, Figures 26-29: locate each
 * co-located block in the default backward P field and retain its forward
 * vector plus the exact field reference selected by that vector.
 */
static cavs_result broadcast_colocated_motion(
    cavs_broadcast_reader_state *reader, const cavs_macroblock *macroblock) {
    size_t field_rows;
    size_t local_row;
    uint32_t base_x;
    uint32_t base_y;
    uint8_t parity;
    uint8_t block;
    if (reader == NULL || macroblock == NULL || reader->dpb == NULL ||
        reader->picture == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    memset(reader->motion.colocated, 0,
           sizeof(reader->motion.colocated));
    if (reader->picture_type != CAVS_PICTURE_B ||
        !broadcast_macroblock_uses_direct(macroblock))
        return CAVS_OK;
    if (reader->picture_structure != 0U)
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    field_rows = reader->picture->macroblock_height / 2U;
    if (field_rows == 0U) return CAVS_ERR_INVALID_STATE;
    local_row = macroblock->row % field_rows;
    parity = reader->motion.current_field == CAVS_FIELD_BOTTOM ? 1U : 0U;
    base_x = (uint32_t)macroblock->column * 16U;
    base_y = (uint32_t)local_row * 32U + parity;
    for (block = 0U; block < CAVS_MB_MOTION_BLOCKS; ++block) {
        cavs_dpb_reference colocated_reference;
        const cavs_macroblock *colocated_macroblock;
        const cavs_mb_partition *partition;
        const cavs_motion_vector *motion;
        uint8_t colocated_block;
        uint32_t sample_x = base_x + (uint32_t)(block & 1U) * 8U + 4U;
        uint32_t sample_y = base_y +
            ((uint32_t)(block >> 1U) * 8U + 4U) * 2U;
        cavs_broadcast_colocated_block *destination =
            &reader->motion.colocated[block];
        cavs_result result = cavs_dpb_colocated_macroblock(
            reader->dpb, CAVS_DPB_BACKWARD,
            reader->motion.default_reference_index[CAVS_PRED_BACKWARD],
            reader->motion.current_field, sample_x, sample_y,
            &colocated_reference, &colocated_macroblock, &colocated_block);
        if (result != CAVS_OK) return result;
        destination->available = 1U;
        destination->intra = colocated_macroblock->is_intra;
        destination->distance_index = colocated_reference.distance;
        destination->picture_structure =
            (uint8_t)(colocated_reference.picture->field_picture == 0U);
        destination->field = colocated_reference.field;
        if (destination->intra != 0U) continue;
        partition = broadcast_partition_at(colocated_macroblock,
                                           colocated_block);
        if (partition == NULL) return CAVS_ERR_INVALID_STATE;
        motion = &partition->motion[CAVS_PRED_FORWARD];
        if (motion->valid == 0U || motion->reference_index < 0 ||
            motion->reference_identity_valid == 0U)
            return CAVS_ERR_MISSING_REFERENCE;
        if (motion->reference_identity_valid > 1U ||
            motion->reference_distance_index >= 512U ||
            (motion->reference_field != CAVS_FIELD_TOP &&
             motion->reference_field != CAVS_FIELD_BOTTOM &&
             motion->reference_field != CAVS_FIELD_BOTH))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        destination->motion.x = motion->x;
        destination->motion.y = motion->y;
        destination->reference_distance_index =
            motion->reference_distance_index;
        /*
         * GB/T 20090.16-2016 9.9.1 b) step 2:
         * BlockDistanceRef = (DistanceIndexCol - DistanceIndexRef + 512)
         *                    % 512.
         */
        destination->block_distance = (uint16_t)(
            ((unsigned)colocated_reference.distance +
             CAVS_DPB_DISTANCE_MODULUS -
             motion->reference_distance_index) %
            CAVS_DPB_DISTANCE_MODULUS);
        if (destination->block_distance == 0U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        destination->reference_field = motion->reference_field;
    }
    return CAVS_OK;
}

/* Normalizes inter motion and produces this macroblock's prediction samples. */
static cavs_result prepare_broadcast_inter_prediction(
    cavs_broadcast_reader_state *reader, cavs_macroblock *macroblock) {
    cavs_macroblock assembled;
    cavs_result result;
    if (reader == NULL || macroblock == NULL || reader->prediction == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (macroblock->is_intra != 0U) return CAVS_OK;
    result = broadcast_colocated_motion(reader, macroblock);
    if (result != CAVS_OK) return result;
    result = cavs_assemble_broadcast_picture_macroblock_motion(
        &reader->motion, reader->picture, macroblock, &assembled);
    if (result != CAVS_OK) return result;
    result = broadcast_freeze_motion_references(&reader->motion, &assembled);
    if (result != CAVS_OK) return result;
    result = cavs_predict_broadcast_macroblock_420(
        &reader->motion, &assembled, reader->prediction);
    if (result != CAVS_OK) return result;
    *macroblock = assembled;
    return CAVS_OK;
}

/* Reads one explicit or run-expanded broadcast macroblock atomically. */
static cavs_result read_broadcast_macroblock(
    void *opaque, uint32_t macroblock_address, cavs_macroblock *macroblock) {
    cavs_broadcast_reader_state *reader =
        (cavs_broadcast_reader_state *)opaque;
    cavs_broadcast_reader_state working;
    cavs_macroblock parsed;
    cavs_macroblock entropy_macroblock;
    cavs_broadcast_mb_context context;
    uint32_t field_end;
    uint32_t field_start;
    int predicted_field;
    cavs_result result;
    if (reader == NULL || macroblock == NULL || reader->picture == NULL ||
        reader->entropy_row == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    working = *reader;
    field_start = (uint32_t)working.start_row *
        working.picture->macroblock_width;
    field_end = field_start +
        (uint32_t)(working.picture->macroblock_height / 2U) *
        working.picture->macroblock_width;
    if (macroblock_address == field_end)
        return cavs_broadcast_slice_finish(&working.entropy);
    if (macroblock_address < field_start || macroblock_address > field_end)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    predicted_field = working.picture_type != CAVS_PICTURE_I ||
        (working.picture_structure == 0U &&
         macroblock_address >= working.picture->macroblock_count / 2U);
    if (working.skip_remaining == 0U && working.explicit_pending == 0U &&
        working.skip_mode_flag != 0U && predicted_field) {
        result = cavs_broadcast_decode_skip_run(
            &working.entropy, field_end - macroblock_address,
            &working.skip_remaining);
        if (result != CAVS_OK) return result;
        working.explicit_pending = 1U;
        working.last_bit_offset = cavs_ae_bit_offset(
            &working.entropy.arithmetic);
        if (working.skip_remaining != 0U)
            working.previous_qp_delta = 0;
    }
    if (working.skip_remaining != 0U) {
        broadcast_skipped_macroblock(&working, macroblock_address, &parsed);
        --working.skip_remaining;
        entropy_macroblock = parsed;
        result = prepare_broadcast_inter_prediction(&working, &parsed);
        if (result != CAVS_OK) return result;
        working.entropy_row[parsed.column] = entropy_macroblock;
        *reader = working;
        *macroblock = parsed;
        return CAVS_OK;
    }
    working.explicit_pending = 0U;
    memset(&context, 0, sizeof(context));
    context.profile_id = UINT8_C(0x48);
    context.format = CAVS_YUV420P8;
    context.picture_type = working.picture_type;
    context.progressive_frame = working.progressive_frame;
    context.picture_structure = working.picture_structure;
    context.skip_mode_flag = working.skip_mode_flag;
    context.picture_reference_flag = working.picture_reference_flag;
    context.fixed_qp = working.fixed_qp;
    context.previous_qp = working.previous_qp;
    context.previous_qp_delta = working.previous_qp_delta;
    context.mb_weighting_flag = working.mb_weighting_flag;
    context.macroblock_index = macroblock_address;
    context.macroblock_width = working.picture->macroblock_width;
    context.macroblock_height = working.picture->macroblock_height;
    context.slice_id = working.slice_id;
    if (macroblock_address % working.picture->macroblock_width != 0U)
        context.left = &working.entropy_row[
            macroblock_address % working.picture->macroblock_width - 1U];
    if (macroblock_address >= field_start +
            working.picture->macroblock_width)
        context.top = &working.entropy_row[
            macroblock_address % working.picture->macroblock_width];
    result = cavs_decode_broadcast_macroblock(&working.entropy, &context,
                                              &parsed);
    if (result != CAVS_OK) return result;
    working.previous_qp = parsed.qp;
    working.previous_qp_delta = parsed.qp_delta;
    working.last_bit_offset = parsed.end_bit_offset;
    entropy_macroblock = parsed;
    result = prepare_broadcast_inter_prediction(&working, &parsed);
    if (result != CAVS_OK) return result;
    working.entropy_row[parsed.column] = entropy_macroblock;
    *reader = working;
    *macroblock = parsed;
    return CAVS_OK;
}

/* Frees the current picture, including its intra-mode side maps. */
static void discard_current_picture(cavs_decoder *decoder) {
    if (decoder->current_frame != NULL) {
        cavs_picture *picture =
            &frame_storage(decoder->current_frame)->picture;
        if (decoder->dpb.current_picture == picture)
            (void)cavs_dpb_end_picture(&decoder->dpb, picture);
    }
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
static cavs_result finish_current_picture(cavs_decoder *decoder) {
    if (decoder->current_frame == NULL) return CAVS_OK;
    if (broadcast_picture_supported(decoder)) {
        cavs_picture *picture =
            &frame_storage(decoder->current_frame)->picture;
        if (picture->completed_fields != CAVS_FIELD_BOTH ||
            picture->filtered == 0U) {
            discard_current_picture(decoder);
            return CAVS_OK;
        }
        return CAVS_OK;
    }
    if (decoder->current_frame_has_data) {
        decoder->pending_frame = decoder->current_frame;
        decoder->current_frame = NULL;
        decoder->frame_pending = 1;
    } else {
        discard_current_picture(decoder);
        return CAVS_OK;
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
    return CAVS_OK;
}

/* Starts a supported picture and any profile-specific neighbor maps. */
static cavs_result begin_current_picture(cavs_decoder *decoder) {
    size_t block_width;
    size_t block_height;
    size_t count;
    cavs_result result;
    discard_current_picture(decoder);
    if (!baseline_i_supported(decoder) &&
        !broadcast_picture_supported(decoder))
        return CAVS_OK;
    result = allocate_current_frame(decoder, &decoder->current_frame);
    if (result != CAVS_OK) return result;
    if (broadcast_picture_supported(decoder)) {
        decoder->next_slice_id = 1U;
        return CAVS_OK;
    }
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

/* Builds the in-loop filter parameters carried by the current picture. */
static cavs_result broadcast_loop_filter_config(
    const cavs_decoder *decoder, const cavs_picture *picture,
    cavs_loop_filter_config *filter) {
    const cavs_i_picture_header *i_picture = &decoder->i_picture;
    const cavs_pb_picture_header *pb_picture = &decoder->pb_picture;
    cavs_motion_profile_capability motion;
    cavs_result result;
    result = decoder_motion_profile_capability(decoder, &motion);
    if (result != CAVS_OK) return result;
    memset(filter, 0, sizeof(*filter));
    filter->enabled = (uint8_t)(decoder->picture_type == CAVS_PICTURE_I ?
        i_picture->loop_filter_disable == 0U :
        pb_picture->loop_filter_disable == 0U);
    filter->motion_unit = motion.motion_unit;
    filter->first_field = picture->top_field_first != 0U ?
        CAVS_FIELD_TOP : CAVS_FIELD_BOTTOM;
    filter->alpha_c_offset = decoder->picture_type == CAVS_PICTURE_I ?
        i_picture->alpha_c_offset : pb_picture->alpha_c_offset;
    filter->beta_offset = decoder->picture_type == CAVS_PICTURE_I ?
        i_picture->beta_offset : pb_picture->beta_offset;
    filter->chroma_qp_delta_cb = decoder->picture_type == CAVS_PICTURE_I ?
        i_picture->chroma_quant_parameter_delta_cb :
        pb_picture->chroma_quant_parameter_delta_cb;
    filter->chroma_qp_delta_cr = decoder->picture_type == CAVS_PICTURE_I ?
        i_picture->chroma_quant_parameter_delta_cr :
        pb_picture->chroma_quant_parameter_delta_cr;
    return CAVS_OK;
}

/* Filters a completed field and stores the picture after both fields. */
static cavs_result finish_broadcast_reconstruction(cavs_decoder *decoder,
                                                   uint8_t field) {
    cavs_frame_storage *storage;
    cavs_loop_filter_config filter;
    cavs_result result;
    if (decoder->current_frame == NULL) return CAVS_ERR_INVALID_STATE;
    storage = frame_storage(decoder->current_frame);
    if ((storage->picture.completed_fields & field) == 0U ||
        (field != CAVS_FIELD_TOP && field != CAVS_FIELD_BOTTOM))
        return CAVS_ERR_INVALID_STATE;
    if ((storage->filtered_fields & field) != 0U)
        return CAVS_ERR_INVALID_STATE;
    result = broadcast_loop_filter_config(decoder, &storage->picture,
                                          &filter);
    if (result != CAVS_OK) return result;
    result = cavs_loop_filter_field(&storage->picture, &filter, field);
    if (result != CAVS_OK) return result;
    storage->filtered_fields |= field;
    if (storage->picture.completed_fields != CAVS_FIELD_BOTH ||
        storage->filtered_fields != CAVS_FIELD_BOTH)
        return CAVS_OK;
    storage->picture.filtered = 1U;
    result = cavs_dpb_store(&decoder->dpb, &storage->picture);
    if (result != CAVS_OK) return result;
    decoder->current_frame_has_data = 1;
    release_frame(&decoder->current_frame);
    return queue_next_dpb_frame(decoder);
}

/* Decodes one complete broadcast field slice across all of its rows. */
static cavs_result decode_broadcast_slice(cavs_decoder *decoder,
                                          const uint8_t *data,
                                          size_t bit_size) {
    cavs_frame_storage *storage;
    cavs_broadcast_reader_state reader;
    cavs_macroblock *entropy_row;
    cavs_broadcast_reconstruction_context reconstruction;
    cavs_slice_cursor cursor;
    uint8_t field;
    uint8_t first;
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t skip_mode_flag;
    uint8_t picture_reference_flag;
    int8_t chroma_delta_cb;
    int8_t chroma_delta_cr;
    cavs_motion_profile_capability motion;
    cavs_result result;
    if (decoder->current_frame == NULL || data == NULL ||
        decoder->slice.header_bits > bit_size)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    storage = frame_storage(decoder->current_frame);
    result = broadcast_field_for_row(&storage->picture,
                                     decoder->slice.macroblock_row, &field);
    if (result != CAVS_OK) return result;
    first = storage->picture.top_field_first != 0U ? CAVS_FIELD_TOP :
                                                    CAVS_FIELD_BOTTOM;
    if ((storage->picture.completed_fields == 0U && field != first) ||
        (storage->picture.completed_fields == first && field == first) ||
        (storage->picture.completed_fields != 0U &&
         storage->picture.completed_fields != first))
        return CAVS_ERR_INVALID_STATE;
    result = cavs_dpb_begin_picture(&decoder->dpb, &storage->picture, field);
    if (result != CAVS_OK) return result;
    if (decoder->picture_type == CAVS_PICTURE_I) {
        progressive_frame = decoder->i_picture.progressive_frame;
        picture_structure = decoder->i_picture.picture_structure;
        skip_mode_flag = decoder->i_picture.skip_mode_flag;
        picture_reference_flag = 1U;
        chroma_delta_cb = decoder->i_picture.chroma_quant_parameter_delta_cb;
        chroma_delta_cr = decoder->i_picture.chroma_quant_parameter_delta_cr;
    } else {
        progressive_frame = decoder->pb_picture.progressive_frame;
        picture_structure = decoder->pb_picture.picture_structure;
        skip_mode_flag = decoder->pb_picture.skip_mode_flag;
        picture_reference_flag = decoder->pb_picture.picture_reference_flag;
        chroma_delta_cb = decoder->pb_picture.chroma_quant_parameter_delta_cb;
        chroma_delta_cr = decoder->pb_picture.chroma_quant_parameter_delta_cr;
    }
    memset(&reader, 0, sizeof(reader));
    reader.dpb = &decoder->dpb;
    reader.picture = &storage->picture;
    reader.picture_type = decoder->picture_type;
    reader.progressive_frame = progressive_frame;
    reader.picture_structure = picture_structure;
    reader.skip_mode_flag = skip_mode_flag;
    reader.picture_reference_flag = picture_reference_flag;
    reader.fixed_qp = decoder->slice.fixed_slice_qp;
    reader.previous_qp = decoder->slice.slice_qp;
    reader.mb_weighting_flag = decoder->slice.mb_weighting_flag;
    reader.slice_id = decoder->next_slice_id++;
    reader.start_row = decoder->slice.macroblock_row;
    reader.prediction = &decoder->broadcast_prediction;
    reader.motion.picture_type = decoder->picture_type;
    reader.motion.picture_structure = picture_structure;
    reader.motion.current_field = field;
    reader.motion.second_field = (uint8_t)(field != first);
    reader.motion.pb_field_enhanced = decoder->picture_type == CAVS_PICTURE_I ?
        0U : decoder->pb_picture.pb_field_enhanced_flag;
    result = decoder_motion_profile_capability(decoder, &motion);
    if (result != CAVS_OK) return result;
    reader.motion.precision = motion.luma_precision;
    reader.motion.current_distance_index = decoder->dpb.current_distance;
    reader.motion.default_reference_index[CAVS_PRED_FORWARD] = 0U;
    reader.motion.default_reference_index[CAVS_PRED_BACKWARD] = 0U;
    result = broadcast_motion_references(&decoder->dpb, field,
                                         &reader.motion);
    if (result != CAVS_OK) return result;
    entropy_row = (cavs_macroblock *)decoder->config.alloc(
        decoder->config.allocator_opaque,
        (size_t)storage->picture.macroblock_width * sizeof(*entropy_row));
    if (entropy_row == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(entropy_row, 0,
        (size_t)storage->picture.macroblock_width * sizeof(*entropy_row));
    reader.entropy_row = entropy_row;
    result = cavs_broadcast_slice_init(&reader.entropy, data, bit_size,
                                       decoder->slice.header_bits);
    if (result != CAVS_OK) {
        decoder->config.free(decoder->config.allocator_opaque, entropy_row);
        return result;
    }
    reader.last_bit_offset = cavs_ae_bit_offset(&reader.entropy.arithmetic);
    memset(&reconstruction, 0, sizeof(reconstruction));
    reconstruction.picture = &storage->picture;
    reconstruction.inter_prediction = &decoder->broadcast_prediction;
    reconstruction.slice_id = reader.slice_id;
    reconstruction.field = field;
    reconstruction.chroma_qp_delta_cb = chroma_delta_cb;
    reconstruction.chroma_qp_delta_cr = chroma_delta_cr;
    result = cavs_slice_cursor_init(&storage->picture, field,
        decoder->slice.macroblock_row, decoder->slice.header_bits,
        bit_size, &cursor);
    if (result == CAVS_OK)
        result = cavs_slice_decode(&cursor, read_broadcast_macroblock, &reader,
                                   &reconstruction);
    decoder->config.free(decoder->config.allocator_opaque, entropy_row);
    if (result != CAVS_OK) return result;
    decoder->current_frame_has_data = 1;
    return finish_broadcast_reconstruction(decoder, field);
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
    cavs_dpb_lifetime lifetime;
    cavs_decoder *decoder;
    cavs_result result;
    if (out == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    memset(&cfg, 0, sizeof(cfg));
    if (config != NULL) cfg = *config;
    if ((cfg.alloc == NULL) != (cfg.free == NULL)) return CAVS_ERR_INVALID_ARGUMENT;
    if (cfg.alloc == NULL) { cfg.alloc = default_alloc; cfg.free = default_free; }
    decoder = (cavs_decoder *)cfg.alloc(cfg.allocator_opaque, sizeof(*decoder));
    if (decoder == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(decoder, 0, sizeof(*decoder));
    decoder->config = cfg;
    result = cavs_dpb_init(&decoder->dpb);
    if (result != CAVS_OK) {
        cfg.free(cfg.allocator_opaque, decoder);
        return result;
    }
    memset(&lifetime, 0, sizeof(lifetime));
    lifetime.retain = dpb_retain_picture;
    lifetime.release = dpb_release_picture;
    result = cavs_dpb_set_lifetime(&decoder->dpb, &lifetime);
    if (result != CAVS_OK) {
        cfg.free(cfg.allocator_opaque, decoder);
        return result;
    }
    *out = decoder;
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
        if (decoder->has_picture) {
            result = finish_current_picture(decoder);
            if (result != CAVS_OK) return result;
            decoder->has_picture = 0;
            decoder->has_slice = 0;
        }
        result = cavs_dpb_flush(&decoder->dpb);
        if (result != CAVS_OK) return result;
        result = queue_next_dpb_frame(decoder);
        if (result != CAVS_OK) return result;
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
        if (decoder->has_picture) {
            result = finish_current_picture(decoder);
            if (result != CAVS_OK) return result;
        }
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
        if (decoder->has_picture) {
            result = finish_current_picture(decoder);
            if (result != CAVS_OK) return result;
        }
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
        result = begin_current_picture(decoder);
        if (result != CAVS_OK) {
            decoder->has_picture = 0;
            return result;
        }
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
            result = broadcast_picture_supported(decoder) ?
                decode_broadcast_slice(decoder, decoded, decoded_bits) :
                decode_baseline_i_slice(decoder, decoded, decoded_bits);
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
    cavs_result result;
    if (decoder == NULL || event == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    free_payload(decoder, &decoder->delivered_payload);
    memset(event, 0, sizeof(*event));
    if (decoder->sequence_pending) {
        decoder->sequence_pending = 0;
        event->type = CAVS_EVENT_SEQUENCE;
        event->sequence = decoder->sequence;
        return CAVS_OK;
    }
    result = queue_next_dpb_frame(decoder);
    if (result != CAVS_OK) return result;
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
    cavs_result result;
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (decoder->has_picture) {
        result = finish_current_picture(decoder);
        if (result != CAVS_OK) return result;
        decoder->has_picture = 0;
        decoder->has_slice = 0;
    }
    result = cavs_dpb_flush(&decoder->dpb);
    if (result != CAVS_OK) return result;
    result = queue_next_dpb_frame(decoder);
    if (result != CAVS_OK) return result;
    if (!decoder->flushing) {
        decoder->flushing = 1;
        decoder->end_pending = 1;
    }
    return CAVS_OK;
}

/* Restores a decoder to its initial input-accepting state. */
cavs_result cavs_decoder_reset(cavs_decoder *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    discard_current_picture(decoder);
    release_frame(&decoder->pending_frame);
    decoder->frame_pending = 0;
    (void)cavs_dpb_reset(&decoder->dpb);
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
        cavs_dpb_destroy(&decoder->dpb);
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
        if (storage->macroblocks != NULL)
            storage->free(storage->allocator_opaque, storage->macroblocks);
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
