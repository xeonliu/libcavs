/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Public decoder API, input state machine, events, and backpressure.
 */
#include <cavs/cavs.h>
#include "codec/unit.h"
#include "picture_pipeline.h"
#include <stdlib.h>
#include <string.h>

struct cavs_decoder {
    cavs_decoder_config config;
    cavs_picture_pipeline *pipeline;
    int flushing;
    int end_pending;
    int sequence_pending;
    int has_sequence;
    cavs_sequence_info sequence;
    cavs_event_type payload_event_type;
    int payload_pending;
    uint8_t *pending_payload;
    size_t pending_payload_size;
    uint8_t *delivered_payload;
};

static void *default_alloc(void *opaque, size_t size) {
    (void)opaque;
    return malloc(size);
}

static void default_free(void *opaque, void *ptr) {
    (void)opaque;
    free(ptr);
}

static void free_payload(cavs_decoder *decoder, uint8_t **payload) {
    if (*payload != NULL) {
        decoder->config.free(decoder->config.allocator_opaque, *payload);
        *payload = NULL;
    }
}

static cavs_result queue_payload(
    cavs_decoder *decoder, cavs_event_type type,
    const uint8_t *data, size_t size) {
    uint8_t *copy = NULL;
    if (size != 0U) {
        copy = (uint8_t *)decoder->config.alloc(
            decoder->config.allocator_opaque, size);
        if (copy == NULL) return CAVS_ERR_OUT_OF_MEMORY;
        memcpy(copy, data, size);
    }
    decoder->payload_event_type = type;
    decoder->payload_pending = 1;
    decoder->pending_payload = copy;
    decoder->pending_payload_size = size;
    return CAVS_OK;
}

cavs_result cavs_decoder_create(
    const cavs_decoder_config *config, cavs_decoder **out) {
    cavs_decoder_config cfg;
    cavs_decoder *decoder;
    cavs_result result;
    if (out == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    memset(&cfg, 0, sizeof(cfg));
    if (config != NULL) cfg = *config;
    if ((cfg.alloc == NULL) != (cfg.free == NULL))
        return CAVS_ERR_INVALID_ARGUMENT;
    if (cfg.alloc == NULL) {
        cfg.alloc = default_alloc;
        cfg.free = default_free;
    }
    decoder = (cavs_decoder *)cfg.alloc(
        cfg.allocator_opaque, sizeof(*decoder));
    if (decoder == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(decoder, 0, sizeof(*decoder));
    decoder->config = cfg;
    result = cavs_picture_pipeline_create(&cfg, &decoder->pipeline);
    if (result != CAVS_OK) {
        cfg.free(cfg.allocator_opaque, decoder);
        return result;
    }
    *out = decoder;
    return CAVS_OK;
}

cavs_result cavs_decoder_send_nal(
    cavs_decoder *decoder, const cavs_packet *packet) {
    cavs_unit_type type;
    cavs_result result;
    if (decoder == NULL || packet == NULL || packet->data == NULL ||
        packet->size < 4U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (decoder->flushing) return CAVS_ERR_INVALID_STATE;
    if (packet->data[0] != 0U || packet->data[1] != 0U ||
        packet->data[2] != 1U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (decoder->sequence_pending || decoder->payload_pending ||
        cavs_picture_pipeline_has_frame(decoder->pipeline))
        return CAVS_AGAIN;

    type = cavs_unit_classify(packet->data[3]);
    if (type == CAVS_UNIT_SEQUENCE_END) {
        result = cavs_picture_pipeline_flush(decoder->pipeline);
        if (result != CAVS_OK) return result;
        decoder->flushing = 1;
        decoder->end_pending = 1;
        return CAVS_OK;
    }
    if (type == CAVS_UNIT_USER_DATA)
        return queue_payload(decoder, CAVS_EVENT_METADATA,
                             packet->data + 4U, packet->size - 4U);
    if (type == CAVS_UNIT_EXTENSION)
        return queue_payload(decoder, CAVS_EVENT_RAW_EXTENSION,
                             packet->data + 4U, packet->size - 4U);
    if (type == CAVS_UNIT_I_PICTURE) {
        cavs_i_picture_header picture;
        result = cavs_picture_pipeline_finish_picture(decoder->pipeline);
        if (result != CAVS_OK) return result;
        if (!decoder->has_sequence) return CAVS_ERR_INVALID_STATE;
        result = cavs_unit_parse_i_picture(
            &decoder->config, packet->data + 4U, packet->size - 4U,
            &decoder->sequence, &picture);
        if (result != CAVS_OK) return result;
        return cavs_picture_pipeline_begin_i(
            decoder->pipeline, &picture, packet);
    }
    if (type == CAVS_UNIT_PB_PICTURE) {
        cavs_pb_picture_header picture;
        result = cavs_picture_pipeline_finish_picture(decoder->pipeline);
        if (result != CAVS_OK) return result;
        if (!decoder->has_sequence) return CAVS_ERR_INVALID_STATE;
        result = cavs_unit_parse_pb_picture(
            &decoder->config, packet->data + 4U, packet->size - 4U,
            &decoder->sequence, &picture);
        if (result != CAVS_OK) return result;
        return cavs_picture_pipeline_begin_pb(
            decoder->pipeline, &picture, packet);
    }
    if (type == CAVS_UNIT_SLICE) {
        if (!cavs_picture_pipeline_has_picture(decoder->pipeline))
            return CAVS_ERR_INVALID_STATE;
        return cavs_picture_pipeline_decode_slice(
            decoder->pipeline, packet->data[3], packet->data + 4U,
            packet->size - 4U);
    }
    if (type == CAVS_UNIT_SEQUENCE_HEADER) {
        cavs_sequence_info sequence;
        result = cavs_unit_parse_sequence(
            &decoder->config, packet->data + 4U, packet->size - 4U,
            &sequence);
        if (result != CAVS_OK) return result;
        if (!decoder->has_sequence ||
            memcmp(&decoder->sequence, &sequence, sizeof(sequence)) != 0) {
            decoder->sequence = sequence;
            decoder->has_sequence = 1;
            decoder->sequence_pending = 1;
            cavs_picture_pipeline_set_sequence(decoder->pipeline, &sequence);
        }
        return CAVS_OK;
    }
    return CAVS_ERR_UNSUPPORTED_PROFILE;
}

cavs_result cavs_decoder_receive_event(
    cavs_decoder *decoder, cavs_event *event) {
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
    result = cavs_picture_pipeline_pop_frame(
        decoder->pipeline, &event->frame);
    if (result == CAVS_OK) {
        event->type = CAVS_EVENT_FRAME;
        return CAVS_OK;
    }
    if (result != CAVS_AGAIN) return result;
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
    if (decoder->end_pending) {
        decoder->end_pending = 0;
        event->type = CAVS_EVENT_END;
        return CAVS_OK;
    }
    return decoder->flushing ? CAVS_EOF : CAVS_AGAIN;
}

cavs_result cavs_decoder_flush(cavs_decoder *decoder) {
    cavs_result result;
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    result = cavs_picture_pipeline_flush(decoder->pipeline);
    if (result != CAVS_OK) return result;
    if (!decoder->flushing) {
        decoder->flushing = 1;
        decoder->end_pending = 1;
    }
    return CAVS_OK;
}

cavs_result cavs_decoder_reset(cavs_decoder *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    (void)cavs_picture_pipeline_reset(decoder->pipeline);
    decoder->flushing = 0;
    decoder->end_pending = 0;
    decoder->sequence_pending = 0;
    decoder->has_sequence = 0;
    free_payload(decoder, &decoder->pending_payload);
    free_payload(decoder, &decoder->delivered_payload);
    decoder->payload_pending = 0;
    decoder->pending_payload_size = 0U;
    memset(&decoder->sequence, 0, sizeof(decoder->sequence));
    return CAVS_OK;
}

void cavs_decoder_destroy(cavs_decoder *decoder) {
    if (decoder != NULL) {
        cavs_free_fn free_fn = decoder->config.free;
        void *opaque = decoder->config.allocator_opaque;
        cavs_picture_pipeline_destroy(decoder->pipeline);
        free_payload(decoder, &decoder->pending_payload);
        free_payload(decoder, &decoder->delivered_payload);
        free_fn(opaque, decoder);
    }
}

const char *cavs_version(void) {
    return "0.1.0-dev";
}

const char *cavs_strerror(cavs_result result) {
    switch (result) {
    case CAVS_OK: return "success";
    case CAVS_AGAIN: return "try again";
    case CAVS_EOF: return "end of stream";
    case CAVS_ERR_INVALID_ARGUMENT: return "invalid argument";
    case CAVS_ERR_OUT_OF_MEMORY: return "out of memory";
    case CAVS_ERR_CORRUPT_BITSTREAM: return "corrupt bitstream";
    case CAVS_ERR_UNSUPPORTED_PROFILE: return "unsupported profile";
    case CAVS_ERR_UNSUPPORTED_LEVEL: return "unsupported level";
    case CAVS_ERR_INVALID_STATE: return "invalid state";
    case CAVS_ERR_MISSING_REFERENCE: return "missing reference picture";
    default: return "unknown error";
    }
}
