/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Annex-B unit classification, unescaping, and syntax parser entry points.
 */
#ifndef CAVS_CODEC_UNIT_H
#define CAVS_CODEC_UNIT_H

#include <cavs/cavs.h>
#include "codec/syntax.h"

typedef struct cavs_unit_payload {
    uint8_t *data;
    size_t bit_size;
} cavs_unit_payload;

cavs_unit_type cavs_unit_classify(uint8_t start_code);
cavs_result cavs_unit_parse_sequence(
    const cavs_decoder_config *config, const uint8_t *data, size_t size,
    cavs_sequence_info *sequence);
cavs_result cavs_unit_parse_i_picture(
    const cavs_decoder_config *config, const uint8_t *data, size_t size,
    const cavs_sequence_info *sequence, cavs_i_picture_header *picture);
cavs_result cavs_unit_parse_pb_picture(
    const cavs_decoder_config *config, const uint8_t *data, size_t size,
    const cavs_sequence_info *sequence, cavs_pb_picture_header *picture);
cavs_result cavs_unit_parse_slice(
    const cavs_decoder_config *config, uint8_t start_code,
    const uint8_t *data, size_t size, const cavs_slice_context *context,
    cavs_slice_header *slice, cavs_unit_payload *payload);
void cavs_unit_payload_release(
    const cavs_decoder_config *config, cavs_unit_payload *payload);

#endif
