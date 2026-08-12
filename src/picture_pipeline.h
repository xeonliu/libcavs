/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Decoder-facing current-picture and decoded-picture-buffer pipeline.
 */
#ifndef CAVS_PICTURE_PIPELINE_H
#define CAVS_PICTURE_PIPELINE_H

#include <cavs/cavs.h>
#include "codec/syntax.h"

typedef struct cavs_picture_pipeline cavs_picture_pipeline;

cavs_result cavs_picture_pipeline_create(
    const cavs_decoder_config *config, cavs_picture_pipeline **pipeline);
void cavs_picture_pipeline_destroy(cavs_picture_pipeline *pipeline);

void cavs_picture_pipeline_set_sequence(
    cavs_picture_pipeline *pipeline, const cavs_sequence_info *sequence);
int cavs_picture_pipeline_has_picture(const cavs_picture_pipeline *pipeline);
int cavs_picture_pipeline_has_frame(const cavs_picture_pipeline *pipeline);

cavs_result cavs_picture_pipeline_finish_picture(
    cavs_picture_pipeline *pipeline);
cavs_result cavs_picture_pipeline_begin_i(
    cavs_picture_pipeline *pipeline, const cavs_i_picture_header *header,
    const cavs_packet *packet);
cavs_result cavs_picture_pipeline_begin_pb(
    cavs_picture_pipeline *pipeline, const cavs_pb_picture_header *header,
    const cavs_packet *packet);
cavs_result cavs_picture_pipeline_decode_slice(
    cavs_picture_pipeline *pipeline, uint8_t start_code,
    const uint8_t *data, size_t size);

cavs_result cavs_picture_pipeline_pop_frame(
    cavs_picture_pipeline *pipeline, cavs_frame **frame);
cavs_result cavs_picture_pipeline_flush(cavs_picture_pipeline *pipeline);
cavs_result cavs_picture_pipeline_reset(cavs_picture_pipeline *pipeline);

#endif
