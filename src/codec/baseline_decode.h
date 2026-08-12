/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Profile-0x20 baseline picture support and slice reconstruction.
 */
#ifndef CAVS_BASELINE_DECODE_H
#define CAVS_BASELINE_DECODE_H

#include <cavs/cavs.h>
#include "codec/syntax.h"

typedef struct cavs_baseline_decode_context {
    cavs_frame *frame;
    const cavs_i_picture_header *picture;
    const cavs_slice_header *slice;
    uint8_t *luma_modes;
    uint8_t *luma_intra;
    int *frame_has_data;
} cavs_baseline_decode_context;

int cavs_baseline_picture_supported(
    const cavs_sequence_info *sequence, const cavs_i_picture_header *picture);

/* GB/T 20090.2-2013 7.4 and 9.2-9.11 decode one baseline I slice row. */
cavs_result cavs_baseline_decode_slice(
    const cavs_baseline_decode_context *context,
    const uint8_t *data, size_t bit_size);

#endif
