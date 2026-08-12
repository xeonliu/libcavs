/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "codec/unit.h"
#include "codec/pseudo_start_code.h"

static cavs_result unescape(
    const cavs_decoder_config *config, const uint8_t *data, size_t size,
    cavs_unit_payload *payload) {
    uint8_t *decoded;
    size_t output_bits;
    if (config == NULL || payload == NULL || data == NULL || size == 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    payload->data = NULL;
    payload->bit_size = 0U;
    decoded = (uint8_t *)config->alloc(config->allocator_opaque, size);
    if (decoded == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    if (!cavs_remove_pseudo_start_codes(
            data, size, decoded, size, &output_bits)) {
        config->free(config->allocator_opaque, decoded);
        return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    payload->data = decoded;
    payload->bit_size = output_bits;
    return CAVS_OK;
}

cavs_unit_type cavs_unit_classify(uint8_t start_code) {
    return cavs_classify_start_code(start_code);
}

cavs_result cavs_unit_parse_sequence(
    const cavs_decoder_config *config, const uint8_t *data, size_t size,
    cavs_sequence_info *sequence) {
    cavs_unit_payload payload;
    cavs_result result = unescape(config, data, size, &payload);
    if (result != CAVS_OK) return result;
    result = cavs_parse_sequence_header(
        payload.data, payload.bit_size / 8U, sequence);
    cavs_unit_payload_release(config, &payload);
    return result;
}

cavs_result cavs_unit_parse_i_picture(
    const cavs_decoder_config *config, const uint8_t *data, size_t size,
    const cavs_sequence_info *sequence, cavs_i_picture_header *picture) {
    cavs_unit_payload payload;
    cavs_result result = unescape(config, data, size, &payload);
    if (result != CAVS_OK) return result;
    result = cavs_parse_i_picture_header(
        payload.data, payload.bit_size, sequence, picture);
    cavs_unit_payload_release(config, &payload);
    return result;
}

cavs_result cavs_unit_parse_pb_picture(
    const cavs_decoder_config *config, const uint8_t *data, size_t size,
    const cavs_sequence_info *sequence, cavs_pb_picture_header *picture) {
    cavs_unit_payload payload;
    cavs_result result = unescape(config, data, size, &payload);
    if (result != CAVS_OK) return result;
    result = cavs_parse_pb_picture_header(
        payload.data, payload.bit_size, sequence, picture);
    cavs_unit_payload_release(config, &payload);
    return result;
}

cavs_result cavs_unit_parse_slice(
    const cavs_decoder_config *config, uint8_t start_code,
    const uint8_t *data, size_t size, const cavs_slice_context *context,
    cavs_slice_header *slice, cavs_unit_payload *payload) {
    cavs_result result;
    if (context == NULL || slice == NULL || payload == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    result = unescape(config, data, size, payload);
    if (result != CAVS_OK) return result;
    result = cavs_parse_slice_header(
        start_code, payload->data, payload->bit_size, context, slice);
    if (result != CAVS_OK) cavs_unit_payload_release(config, payload);
    return result;
}

void cavs_unit_payload_release(
    const cavs_decoder_config *config, cavs_unit_payload *payload) {
    if (config == NULL || payload == NULL) return;
    if (payload->data != NULL)
        config->free(config->allocator_opaque, payload->data);
    payload->data = NULL;
    payload->bit_size = 0U;
}
