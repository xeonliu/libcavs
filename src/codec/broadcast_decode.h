/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Profile-0x48 broadcast picture support and slice reconstruction.
 */
#ifndef CAVS_BROADCAST_DECODE_H
#define CAVS_BROADCAST_DECODE_H

#include <cavs/cavs.h>
#include "codec/broadcast_reconstruction.h"
#include "codec/loop_filter.h"
#include "codec/syntax.h"
#include "dpb.h"

typedef struct cavs_broadcast_decode_context {
    const cavs_decoder_config *config;
    const cavs_sequence_info *sequence;
    cavs_dpb *dpb;
    cavs_frame *frame;
    cavs_picture_type picture_type;
    const cavs_i_picture_header *i_picture;
    const cavs_pb_picture_header *pb_picture;
    const cavs_slice_header *slice;
    cavs_macroblock_prediction_420 *prediction;
    uint16_t slice_id;
} cavs_broadcast_decode_context;

int cavs_broadcast_picture_supported(
    const cavs_sequence_info *sequence, cavs_picture_type picture_type,
    const cavs_i_picture_header *i_picture,
    const cavs_pb_picture_header *pb_picture);

/* GB/T 20090.16-2016 6.3 and 7.4 map a slice start row to its physical field. */
cavs_result cavs_broadcast_field_for_row(const cavs_picture *picture,
                                          uint16_t row, uint8_t *field);

/* Returns the inclusive start and exclusive end rows of one physical field. */
cavs_result cavs_broadcast_field_range(const cavs_picture *picture,
                                        uint8_t field, uint16_t *start_row,
                                        uint16_t *end_row);

/* Returns the exclusive syntax-row bound of one physical field. */
cavs_result cavs_broadcast_field_end_row(const cavs_picture *picture,
                                          uint8_t field, uint16_t *end_row);

/*
 * GB/T 20090.16-2016 7.4 and 9.2-9.10 decode one slice in the explicit row
 * interval [slice->macroblock_row, end_row). A zero decoded_field means that
 * this slice did not complete its field; the caller owns the completion commit.
 */
cavs_result cavs_broadcast_decode_slice(
    const cavs_broadcast_decode_context *context,
    const uint8_t *data, size_t bit_size, uint16_t end_row,
    uint8_t *decoded_field);

/* Derives Part 16 9.11 filter controls from the current picture headers. */
cavs_result cavs_broadcast_loop_filter_config(
    cavs_picture_type picture_type, const cavs_i_picture_header *i_picture,
    const cavs_pb_picture_header *pb_picture, const cavs_picture *picture,
    cavs_loop_filter_config *filter);

#endif
