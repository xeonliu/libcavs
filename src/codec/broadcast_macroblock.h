/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Broadcast-profile YUV420 advanced-entropy macroblock syntax.
 */
#ifndef CAVS_BROADCAST_MACROBLOCK_H
#define CAVS_BROADCAST_MACROBLOCK_H

#include "codec/advanced_entropy.h"
#include "codec/macroblock.h"

typedef struct cavs_broadcast_mb_context {
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
    /* GB/T 20090.16-2016 Table 25 slice_weighting_flag. */
    uint8_t slice_weighting_flag;
    uint8_t mb_weighting_flag;
    uint32_t macroblock_index;
    uint32_t macroblock_width;
    uint32_t macroblock_height;
    uint16_t slice_id;
    const cavs_macroblock *left;
    const cavs_macroblock *top;
} cavs_broadcast_mb_context;

typedef struct cavs_broadcast_slice_decoder {
    cavs_ae_decoder arithmetic;
    cavs_ae_context contexts[CAVS_AE_CONTEXT_COUNT];
    uint8_t last_stuffing_bit;
    uint8_t has_stuffing_bit;
} cavs_broadcast_slice_decoder;

/*
 * GB/T 20090.16-2016 7.4 and 8.4.2 reset all advanced-entropy state for
 * each byte-aligned slice payload.
 */
cavs_result cavs_broadcast_slice_init(cavs_broadcast_slice_decoder *decoder,
                                      const uint8_t *data, size_t bit_size,
                                      size_t bit_offset);

/*
 * GB/T 20090.16-2016 7.4, 8.3 Table 44 decodes mb_skip_run and, for a
 * nonzero run, validates the following advanced-entropy stuffing bit.
 */
cavs_result cavs_broadcast_decode_skip_run(
    cavs_broadcast_slice_decoder *decoder, uint32_t maximum,
    uint32_t *skip_run);

/*
 * GB/T 20090.16-2016 7.4.14 and 8.4 require the last macroblock stuffing bin
 * in a slice to equal one. Remaining unshifted source bits belong to arithmetic
 * finalization and are not interpreted as byte-aligned padding syntax.
 */
cavs_result cavs_broadcast_slice_finish(
    const cavs_broadcast_slice_decoder *decoder);

/*
 * GB/T 20090.16-2016 7.5-7.6, 8.3-8.4 and 9.2-9.8 decode one complete
 * profile-0x48 YUV420 macroblock. Quantized coefficients are returned after
 * inverse scan in row-major coefficients_8x8; output is unchanged on error.
 */
cavs_result cavs_decode_broadcast_macroblock(
    cavs_broadcast_slice_decoder *decoder,
    const cavs_broadcast_mb_context *context, cavs_macroblock *macroblock);

#endif
