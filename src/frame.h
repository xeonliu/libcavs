/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Decoder-owned frame allocation and picture-lifetime adapters.
 */
#ifndef CAVS_FRAME_H
#define CAVS_FRAME_H

#include <cavs/cavs.h>
#include "picture.h"

typedef struct cavs_frame_parameters {
    const cavs_sequence_info *sequence;
    cavs_picture_type picture_type;
    uint8_t picture_structure;
    uint8_t top_field_first;
    uint8_t repeat_first_field;
    uint8_t picture_distance;
    int64_t pts;
    int64_t dts;
    size_t macroblock_size;
} cavs_frame_parameters;

cavs_result cavs_frame_allocate(
    const cavs_decoder_config *config, const cavs_frame_parameters *parameters,
    cavs_frame **frame);
void cavs_frame_release(cavs_frame **frame);

cavs_picture *cavs_frame_picture(cavs_frame *frame);
cavs_frame *cavs_picture_frame(cavs_picture *picture);
void *cavs_frame_plane(cavs_frame *frame, unsigned plane);

uint8_t cavs_frame_filtered_fields(const cavs_frame *frame);
void cavs_frame_mark_filtered(cavs_frame *frame, uint8_t field);

void cavs_frame_retain_picture(void *opaque, cavs_picture *picture);
void cavs_frame_release_picture(void *opaque, cavs_picture *picture);

#ifdef CAVS_TESTING
unsigned cavs_frame_test_reference_count(const cavs_frame *frame);
#endif

#endif
