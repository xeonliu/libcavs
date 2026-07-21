/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Decoder lifetime and public API state transitions. Syntax and reconstruction
 * are added in separate modules as their conformance coverage becomes ready.
 */
#include <cavs/cavs.h>
#include "syntax.h"
#include "pseudo_start_code.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct cavs_frame_storage {
    cavs_frame public_frame;
    unsigned references;
    cavs_free_fn free;
    void *allocator_opaque;
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
                                       cavs_slice_header *slice) {
    cavs_slice_context context;
    uint8_t *decoded;
    size_t output_bits;
    cavs_result result;

    if (size == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
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
    decoder->config.free(decoder->config.allocator_opaque, decoded);
    return result;
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
    if (decoder->sequence_pending || decoder->payload_pending) return CAVS_AGAIN;
    unit_type = cavs_classify_start_code(packet->data[3]);
    if (unit_type == CAVS_UNIT_SEQUENCE_END) {
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
        return CAVS_OK;
    }
    if (unit_type == CAVS_UNIT_PB_PICTURE) {
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
        return CAVS_OK;
    }
    if (unit_type == CAVS_UNIT_SLICE) {
        if (!decoder->has_picture) return CAVS_ERR_INVALID_STATE;
        result = parse_slice_payload(decoder, packet->data[3],
                                     packet->data + 4U, packet->size - 4U,
                                     &slice);
        if (result != CAVS_OK) return result;
        decoder->slice = slice;
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
    if (!decoder->flushing) { decoder->flushing = 1; decoder->end_pending = 1; }
    return CAVS_OK;
}

/* Restores a decoder to its initial input-accepting state. */
cavs_result cavs_decoder_reset(cavs_decoder *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
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
    if (--storage->references == 0U) storage->free(storage->allocator_opaque, storage);
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
