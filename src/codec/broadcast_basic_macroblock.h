/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.5-7.6, 8.2 and 9.1 basic-entropy macroblock syntax.
 */
#ifndef CAVS_BROADCAST_BASIC_MACROBLOCK_H
#define CAVS_BROADCAST_BASIC_MACROBLOCK_H

#include <cavs/cavs.h>
#include "codec/macroblock.h"
#include <stddef.h>
#include <stdint.h>

typedef struct cavs_broadcast_basic_mb_context {
    uint8_t profile_id;
    cavs_pixel_format format;
    cavs_picture_type picture_type;
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t skip_mode_flag;
    uint8_t picture_reference_flag;
    uint8_t fixed_qp;
    uint8_t previous_qp;
    int8_t previous_qp_delta;
    uint8_t slice_weighting_flag;
    uint8_t mb_weighting_flag;
    uint32_t macroblock_index;
    uint32_t macroblock_width;
    uint32_t macroblock_height;
    uint16_t slice_id;
    const cavs_macroblock *left;
    const cavs_macroblock *top;
} cavs_broadcast_basic_mb_context;

typedef struct cavs_broadcast_basic_decoder {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_offset;
} cavs_broadcast_basic_decoder;

/* GB/T 20090.16-2016 8.2 starts each basic slice at the supplied bit offset. */
cavs_result cavs_broadcast_basic_init(
    cavs_broadcast_basic_decoder *decoder, const uint8_t *data,
    size_t bit_size, size_t bit_offset);

/* GB/T 20090.16-2016 8.2, 9.3: decode one basic mb_skip_run or return its end. */
cavs_result cavs_broadcast_basic_skip_run(
    cavs_broadcast_basic_decoder *decoder, uint32_t maximum,
    uint32_t *skip_run);

/* Basic entropy has no arithmetic finalization bin; validate the byte range. */
cavs_result cavs_broadcast_basic_finish(
    const cavs_broadcast_basic_decoder *decoder);

/*
 * GB/T 20090.16-2016 7.5-7.6, 8.2 and 9.1 decode one complete YUV420
 * macroblock. The output and decoder are committed only on success.
 */
cavs_result cavs_decode_broadcast_basic_macroblock(
    cavs_broadcast_basic_decoder *decoder,
    const cavs_broadcast_basic_mb_context *context, cavs_macroblock *macroblock);

#endif
