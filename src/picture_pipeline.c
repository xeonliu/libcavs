/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Current-picture lifecycle, profile selection, slice dispatch, and DPB output.
 */
#include <cavs/cavs.h>
#include "codec/syntax.h"
#include "codec/baseline_decode.h"
#include "codec/macroblock.h"
#include "picture.h"
#include "codec/broadcast_decode.h"
#include "codec/loop_filter.h"
#include "dpb.h"
#include "frame.h"
#include "picture_pipeline.h"
#include "codec/unit.h"
#include "safe.h"
#include <string.h>

struct cavs_picture_pipeline {
    cavs_decoder_config config;
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
    /* GB/T 20090.16-2016 7.4/9.3: defer a slice until its end row is known. */
    struct {
        uint8_t *data;
        size_t bit_size;
        cavs_slice_header header;
        uint8_t field;
        uint16_t slice_id;
        uint8_t active;
    } broadcast_pending;
    cavs_macroblock_prediction_420 broadcast_prediction;
    cavs_dpb dpb;
    uint8_t *luma_modes;
    uint8_t *luma_intra;
    size_t luma_mode_count;
    int64_t picture_pts;
    int64_t picture_dts;
};

static cavs_result finish_broadcast_reconstruction(
    cavs_picture_pipeline *decoder, uint8_t field);
static cavs_result decode_pending_broadcast_slice(
    cavs_picture_pipeline *decoder, uint16_t end_row);

/* GB/T 20090.16-2016 6.5/9.4.5: begin one physical field exactly once. */
static cavs_result begin_broadcast_field(cavs_picture_pipeline *decoder,
                                          uint8_t field) {
    cavs_picture *picture;
    if (decoder == NULL || decoder->current_frame == NULL)
        return CAVS_ERR_INVALID_STATE;
    picture = cavs_frame_picture(decoder->current_frame);
    if (decoder->dpb.current_picture == picture &&
        decoder->dpb.current_field == field)
        return CAVS_OK;
    return cavs_dpb_begin_picture(&decoder->dpb, picture, field);
}

/* Releases the deferred GB/T 20090.16-2016 7.4 slice payload. */
static void release_broadcast_pending(cavs_picture_pipeline *decoder) {
    if (decoder->broadcast_pending.data != NULL)
        decoder->config.free(decoder->config.allocator_opaque,
                             decoder->broadcast_pending.data);
    memset(&decoder->broadcast_pending, 0,
           sizeof(decoder->broadcast_pending));
}

/* Transfers one ready DPB output reference to the public event slot. */
static cavs_result queue_next_dpb_frame(cavs_picture_pipeline *decoder) {
    cavs_picture *picture;
    cavs_result result;
    if (decoder->frame_pending) return CAVS_OK;
    result = cavs_dpb_pop_output(&decoder->dpb, &picture);
    if (result == CAVS_AGAIN || result == CAVS_EOF) return CAVS_OK;
    if (result != CAVS_OK) return result;
    decoder->pending_frame = cavs_picture_frame(picture);
    if (decoder->pending_frame == NULL) return CAVS_ERR_INVALID_STATE;
    decoder->frame_pending = 1;
    return CAVS_OK;
}

/*
 * Allocates one decoder-owned YUV420 frame and metadata map. Buffer layout and
 * ownership introduce no codec decision.
 */
static cavs_result allocate_current_frame(
    cavs_picture_pipeline *decoder, cavs_frame **frame) {
    cavs_frame_parameters parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.sequence = &decoder->sequence;
    parameters.picture_type = decoder->picture_type;
    parameters.pts = decoder->picture_pts;
    parameters.dts = decoder->picture_dts;
    parameters.macroblock_size = sizeof(cavs_macroblock);
    if (decoder->picture_type == CAVS_PICTURE_I) {
        parameters.picture_structure = decoder->i_picture.picture_structure;
        parameters.top_field_first = decoder->i_picture.top_field_first;
        parameters.repeat_first_field = decoder->i_picture.repeat_first_field;
        parameters.picture_distance = decoder->i_picture.picture_distance;
    } else {
        parameters.picture_structure = decoder->pb_picture.picture_structure;
        parameters.top_field_first = decoder->pb_picture.top_field_first;
        parameters.repeat_first_field = decoder->pb_picture.repeat_first_field;
        parameters.picture_distance = decoder->pb_picture.picture_distance;
    }
    return cavs_frame_allocate(&decoder->config, &parameters, frame);
}

/* Profile modules own their support checks; the pipeline only selects one. */
static int baseline_i_supported(const cavs_picture_pipeline *pipeline) {
    return cavs_baseline_picture_supported(&pipeline->sequence,
                                           &pipeline->i_picture);
}

static int broadcast_picture_supported(const cavs_picture_pipeline *pipeline) {
    return cavs_broadcast_picture_supported(
        &pipeline->sequence, pipeline->picture_type, &pipeline->i_picture,
        &pipeline->pb_picture);
}

/* Frees the current picture, including its intra-mode side maps. */
static void discard_current_picture(cavs_picture_pipeline *decoder) {
    release_broadcast_pending(decoder);
    if (decoder->current_frame != NULL) {
        cavs_picture *picture = cavs_frame_picture(decoder->current_frame);
        if (decoder->dpb.current_picture == picture)
            (void)cavs_dpb_end_picture(&decoder->dpb, picture);
    }
    cavs_frame_release(&decoder->current_frame);
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
static cavs_result finish_current_picture(cavs_picture_pipeline *decoder) {
    cavs_result result;
    if (decoder->current_frame == NULL) return CAVS_OK;
    if (broadcast_picture_supported(decoder)) {
        cavs_picture *picture = cavs_frame_picture(decoder->current_frame);
        if (decoder->broadcast_pending.active != 0U) {
            uint16_t end_row;
            result = cavs_broadcast_field_end_row(
                picture, decoder->broadcast_pending.field, &end_row);
            if (result != CAVS_OK) return result;
            /* The final pending slice is the last slice of its field. */
            result = decode_pending_broadcast_slice(decoder, end_row);
            if (result != CAVS_OK) {
                discard_current_picture(decoder);
                return result;
            }
        }
        if (decoder->current_frame == NULL) return CAVS_OK;
        if (picture->completed_fields != CAVS_FIELD_BOTH ||
            picture->filtered == 0U) {
            discard_current_picture(decoder);
            return CAVS_ERR_CORRUPT_BITSTREAM;
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
static cavs_result begin_current_picture(cavs_picture_pipeline *decoder) {
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

/* Filters a completed field and stores the picture after both fields. */
static cavs_result finish_broadcast_reconstruction(cavs_picture_pipeline *decoder,
                                                   uint8_t field) {
    cavs_picture *picture;
    cavs_loop_filter_config filter;
    cavs_result result;
    if (decoder->current_frame == NULL) return CAVS_ERR_INVALID_STATE;
    picture = cavs_frame_picture(decoder->current_frame);
    if ((picture->completed_fields & field) == 0U ||
        (field != CAVS_FIELD_TOP && field != CAVS_FIELD_BOTTOM))
        return CAVS_ERR_INVALID_STATE;
    if ((cavs_frame_filtered_fields(decoder->current_frame) & field) != 0U)
        return CAVS_ERR_INVALID_STATE;
    result = cavs_broadcast_loop_filter_config(
        decoder->picture_type, &decoder->i_picture, &decoder->pb_picture,
        picture, &filter);
    if (result != CAVS_OK) return result;
    result = cavs_loop_filter_field(picture, &filter, field);
    if (result != CAVS_OK) return result;
    cavs_frame_mark_filtered(decoder->current_frame, field);
    if (picture->completed_fields != CAVS_FIELD_BOTH ||
        cavs_frame_filtered_fields(decoder->current_frame) != CAVS_FIELD_BOTH)
        return CAVS_OK;
    picture->filtered = 1U;
    result = cavs_dpb_store(&decoder->dpb, picture);
    if (result != CAVS_OK) return result;
    decoder->current_frame_has_data = 1;
    cavs_frame_release(&decoder->current_frame);
    return queue_next_dpb_frame(decoder);
}

/*
 * GB/T 20090.16-2016 7.4 and 9.3: decode a deferred slice only after its
 * following slice start row is known, or with the field end at picture finish.
 */
static cavs_result decode_pending_broadcast_slice(
    cavs_picture_pipeline *decoder, uint16_t end_row) {
    cavs_broadcast_decode_context decode;
    uint8_t decoded_field;
    cavs_result result;
    if (decoder == NULL || decoder->current_frame == NULL ||
        decoder->broadcast_pending.active == 0U || end_row == 0U)
        return CAVS_ERR_INVALID_STATE;
    memset(&decode, 0, sizeof(decode));
    decode.config = &decoder->config;
    decode.sequence = &decoder->sequence;
    decode.dpb = &decoder->dpb;
    decode.frame = decoder->current_frame;
    decode.picture_type = decoder->picture_type;
    decode.i_picture = &decoder->i_picture;
    decode.pb_picture = &decoder->pb_picture;
    decode.slice = &decoder->broadcast_pending.header;
    decode.prediction = &decoder->broadcast_prediction;
    decode.slice_id = decoder->broadcast_pending.slice_id;
    result = cavs_broadcast_decode_slice(
        &decode, decoder->broadcast_pending.data,
        decoder->broadcast_pending.bit_size, end_row, &decoded_field);
    if (result != CAVS_OK) return result;
    release_broadcast_pending(decoder);
    decoder->current_frame_has_data = 1;
    if (decoded_field != 0U) {
        cavs_picture *picture = cavs_frame_picture(decoder->current_frame);
        /* GB/T 20090.16-2016 9.3: field completion is a pipeline commit. */
        picture->completed_fields |= decoded_field;
        return finish_broadcast_reconstruction(decoder, decoded_field);
    }
    return CAVS_OK;
}

/* Creates the current-picture and DPB state owned by one public decoder. */
cavs_result cavs_picture_pipeline_create(
    const cavs_decoder_config *config, cavs_picture_pipeline **out) {
    cavs_dpb_lifetime lifetime;
    cavs_picture_pipeline *decoder;
    cavs_result result;
    if (config == NULL || config->alloc == NULL || config->free == NULL ||
        out == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    decoder = (cavs_picture_pipeline *)config->alloc(
        config->allocator_opaque, sizeof(*decoder));
    if (decoder == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(decoder, 0, sizeof(*decoder));
    decoder->config = *config;
    result = cavs_dpb_init(&decoder->dpb);
    if (result != CAVS_OK) {
        config->free(config->allocator_opaque, decoder);
        return result;
    }
    memset(&lifetime, 0, sizeof(lifetime));
    lifetime.retain = cavs_frame_retain_picture;
    lifetime.release = cavs_frame_release_picture;
    result = cavs_dpb_set_lifetime(&decoder->dpb, &lifetime);
    if (result != CAVS_OK) {
        cavs_dpb_destroy(&decoder->dpb);
        config->free(config->allocator_opaque, decoder);
        return result;
    }
    *out = decoder;
    return CAVS_OK;
}

void cavs_picture_pipeline_set_sequence(
    cavs_picture_pipeline *decoder, const cavs_sequence_info *sequence) {
    if (decoder == NULL || sequence == NULL) return;
    decoder->sequence = *sequence;
    decoder->has_picture = 0;
    decoder->has_slice = 0;
}

int cavs_picture_pipeline_has_picture(
    const cavs_picture_pipeline *decoder) {
    return decoder != NULL && decoder->has_picture != 0;
}

int cavs_picture_pipeline_has_frame(const cavs_picture_pipeline *decoder) {
    return decoder != NULL && decoder->frame_pending != 0;
}

cavs_result cavs_picture_pipeline_finish_picture(
    cavs_picture_pipeline *decoder) {
    cavs_result result;
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (!decoder->has_picture) return CAVS_OK;
    result = finish_current_picture(decoder);
    decoder->has_picture = 0;
    decoder->has_slice = 0;
    if (result != CAVS_OK) return result;
    return CAVS_OK;
}

cavs_result cavs_picture_pipeline_begin_i(
    cavs_picture_pipeline *decoder, const cavs_i_picture_header *header,
    const cavs_packet *packet) {
    cavs_result result;
    if (decoder == NULL || header == NULL || packet == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    decoder->i_picture = *header;
    decoder->picture_type = CAVS_PICTURE_I;
    decoder->picture_pts = packet->pts;
    decoder->picture_dts = packet->dts;
    decoder->has_picture = 1;
    decoder->has_slice = 0;
    result = begin_current_picture(decoder);
    if (result != CAVS_OK) decoder->has_picture = 0;
    return result;
}

cavs_result cavs_picture_pipeline_begin_pb(
    cavs_picture_pipeline *decoder, const cavs_pb_picture_header *header,
    const cavs_packet *packet) {
    cavs_result result;
    if (decoder == NULL || header == NULL || packet == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    decoder->pb_picture = *header;
    decoder->picture_type = header->picture_coding_type == 1U ?
        CAVS_PICTURE_P : CAVS_PICTURE_B;
    decoder->picture_pts = packet->pts;
    decoder->picture_dts = packet->dts;
    decoder->has_picture = 1;
    decoder->has_slice = 0;
    result = begin_current_picture(decoder);
    if (result != CAVS_OK) decoder->has_picture = 0;
    return result;
}

cavs_result cavs_picture_pipeline_decode_slice(
    cavs_picture_pipeline *decoder, uint8_t start_code,
    const uint8_t *data, size_t size) {
    cavs_slice_context context;
    cavs_unit_payload payload;
    cavs_result result;
    if (decoder == NULL || data == NULL || !decoder->has_picture)
        return decoder == NULL || data == NULL ? CAVS_ERR_INVALID_ARGUMENT :
                                                CAVS_ERR_INVALID_STATE;
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
        context.advanced_entropy_enabled =
            decoder->i_picture.advanced_entropy_enabled;
    } else {
        context.picture_structure = decoder->pb_picture.picture_structure;
        context.fixed_picture_qp = decoder->pb_picture.fixed_picture_qp;
        context.picture_qp = decoder->pb_picture.picture_qp;
        context.advanced_entropy_enabled =
            decoder->pb_picture.advanced_entropy_enabled;
    }
    /*
     * GB/T 20090.16-2016 7.4/9.3: the slice carries one parameter slot for
     * an I second field, two for a frame P/B picture, and four interleaved
     * forward/backward slots for a field P/B picture. The parser ignores this
     * count when the current I first field has no prediction syntax.
     */
    if (decoder->picture_type == CAVS_PICTURE_I)
        context.number_of_references =
            decoder->i_picture.picture_structure == 0U ? 1U : 0U;
    else
        context.number_of_references =
            decoder->pb_picture.picture_structure == 0U ? 4U : 2U;
    result = cavs_unit_parse_slice(
        &decoder->config, start_code, data, size, &context, &decoder->slice,
        &payload);
    if (result != CAVS_OK) return result;
    /* GB/T 20090.16-2016 7.4/9.3: a completed picture has no slice range. */
    if (decoder->current_frame == NULL &&
        broadcast_picture_supported(decoder)) {
        cavs_unit_payload_release(&decoder->config, &payload);
        decoder->has_picture = 0;
        decoder->has_slice = 0;
        return CAVS_ERR_INVALID_STATE;
    }
    if (decoder->current_frame != NULL &&
        broadcast_picture_supported(decoder)) {
        uint8_t field;
        uint16_t current_end;
        if (decoder->broadcast_pending.active != 0U) {
            result = cavs_broadcast_field_for_row(
                cavs_frame_picture(decoder->current_frame),
                decoder->slice.macroblock_row, &field);
            if (result == CAVS_OK &&
                field != decoder->broadcast_pending.field)
                result = cavs_broadcast_field_end_row(
                    cavs_frame_picture(decoder->current_frame),
                    decoder->broadcast_pending.field, &current_end);
            else if (result == CAVS_OK)
                current_end = decoder->slice.macroblock_row;
            if (result != CAVS_OK || current_end <=
                    decoder->broadcast_pending.header.macroblock_row)
                result = CAVS_ERR_CORRUPT_BITSTREAM;
            if (result == CAVS_OK)
                result = decode_pending_broadcast_slice(decoder, current_end);
            if (result == CAVS_OK && decoder->current_frame == NULL)
                result = CAVS_ERR_INVALID_STATE;
        }
        if (result == CAVS_OK) {
            result = cavs_broadcast_field_for_row(
                cavs_frame_picture(decoder->current_frame),
                decoder->slice.macroblock_row, &field);
        }
        if (result == CAVS_OK) {
            uint16_t field_start;
            uint16_t field_end;
            result = cavs_broadcast_field_range(
                cavs_frame_picture(decoder->current_frame), field,
                &field_start, &field_end);
            if (result == CAVS_OK &&
                decoder->broadcast_pending.active == 0U &&
                decoder->dpb.current_field != field &&
                decoder->slice.macroblock_row != field_start)
                result = CAVS_ERR_CORRUPT_BITSTREAM;
            (void)field_end;
        }
        if (result == CAVS_OK &&
            decoder->broadcast_pending.active == 0U) {
            result = begin_broadcast_field(decoder, field);
        }
        if (result == CAVS_OK) {
            decoder->broadcast_pending.data = payload.data;
            decoder->broadcast_pending.bit_size = payload.bit_size;
            decoder->broadcast_pending.header = decoder->slice;
            decoder->broadcast_pending.field = field;
            decoder->broadcast_pending.slice_id = decoder->next_slice_id++;
            decoder->broadcast_pending.active = 1U;
            payload.data = NULL;
            payload.bit_size = 0U;
        }
    } else if (decoder->current_frame != NULL) {
        cavs_baseline_decode_context decode;
        memset(&decode, 0, sizeof(decode));
        decode.frame = decoder->current_frame;
        decode.picture = &decoder->i_picture;
        decode.slice = &decoder->slice;
        decode.luma_modes = decoder->luma_modes;
        decode.luma_intra = decoder->luma_intra;
        decode.frame_has_data = &decoder->current_frame_has_data;
        result = cavs_baseline_decode_slice(
            &decode, payload.data, payload.bit_size);
    }
    cavs_unit_payload_release(&decoder->config, &payload);
    if (result != CAVS_OK) {
        if (broadcast_picture_supported(decoder)) {
            if (decoder->current_frame != NULL)
                discard_current_picture(decoder);
            decoder->has_picture = 0;
            decoder->has_slice = 0;
        }
        return result;
    }
    decoder->has_slice = 1;
    return CAVS_OK;
}

cavs_result cavs_picture_pipeline_pop_frame(
    cavs_picture_pipeline *decoder, cavs_frame **frame) {
    cavs_result result;
    if (decoder == NULL || frame == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    result = queue_next_dpb_frame(decoder);
    if (result != CAVS_OK) return result;
    if (!decoder->frame_pending) return CAVS_AGAIN;
    *frame = decoder->pending_frame;
    decoder->pending_frame = NULL;
    decoder->frame_pending = 0;
    return CAVS_OK;
}

cavs_result cavs_picture_pipeline_flush(cavs_picture_pipeline *decoder) {
    cavs_result result;
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    result = cavs_picture_pipeline_finish_picture(decoder);
    if (result != CAVS_OK) return result;
    result = cavs_dpb_flush(&decoder->dpb);
    if (result != CAVS_OK) return result;
    return queue_next_dpb_frame(decoder);
}

cavs_result cavs_picture_pipeline_reset(cavs_picture_pipeline *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    discard_current_picture(decoder);
    cavs_frame_release(&decoder->pending_frame);
    decoder->frame_pending = 0;
    (void)cavs_dpb_reset(&decoder->dpb);
    decoder->has_picture = 0;
    decoder->has_slice = 0;
    memset(&decoder->sequence, 0, sizeof(decoder->sequence));
    decoder->picture_type = CAVS_PICTURE_I;
    memset(&decoder->i_picture, 0, sizeof(decoder->i_picture));
    memset(&decoder->pb_picture, 0, sizeof(decoder->pb_picture));
    memset(&decoder->slice, 0, sizeof(decoder->slice));
    decoder->picture_pts = 0;
    decoder->picture_dts = 0;
    return CAVS_OK;
}

void cavs_picture_pipeline_destroy(cavs_picture_pipeline *decoder) {
    if (decoder != NULL) {
        cavs_free_fn free_fn = decoder->config.free;
        void *opaque = decoder->config.allocator_opaque;
        discard_current_picture(decoder);
        cavs_frame_release(&decoder->pending_frame);
        cavs_dpb_destroy(&decoder->dpb);
        free_fn(opaque, decoder);
    }
}
