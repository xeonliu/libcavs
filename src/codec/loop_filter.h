/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * In-loop deblocking for fully reconstructed decoder-owned pictures.
 */
#ifndef CAVS_LOOP_FILTER_H
#define CAVS_LOOP_FILTER_H

#include "picture.h"
#include "codec/macroblock.h"
#include <stdint.h>

typedef struct cavs_loop_filter_config {
    uint8_t enabled;
    uint8_t motion_unit;
    uint8_t first_field;
    int8_t alpha_c_offset;
    int8_t beta_offset;
    int8_t chroma_qp_delta_cb;
    int8_t chroma_qp_delta_cr;
} cavs_loop_filter_config;

/*
 * GB/T 20090.16-2016 9.11.2 and GB/T 20090.2-2013 9.12.2 derive
 * the strength of an 8x8 luma-block boundary from prediction type,
 * reference indices, and motion-vector differences. block indices use
 * raster order within a 16x16 luma macroblock.
 */
cavs_result cavs_loop_filter_boundary_strength(
    const cavs_macroblock *p, uint8_t p_block,
    const cavs_macroblock *q, uint8_t q_block,
    cavs_picture_type picture_type, uint8_t motion_unit,
    uint8_t *strength);

/*
 * GB/T 20090.16-2016 9.11, Figures 32-34, Tables 64-65, and
 * GB/T 20090.2-2013 9.12, Figures 44-46, Tables 73-74 filter a complete
 * reconstructed picture in macroblock decode order. Picture and slice
 * boundaries are excluded. Field pictures are filtered through their
 * interleaved physical lines.
 */
cavs_result cavs_loop_filter_picture(
    cavs_picture *picture, const cavs_loop_filter_config *config);

/*
 * GB/T 20090.16-2016 9.11.1 filters a decoded field before it can be used as
 * the reference for a later field. This internal entry filters exactly one
 * completed physical field and leaves picture->filtered for the complete-
 * picture owner to publish after all fields have completed.
 */
cavs_result cavs_loop_filter_field(
    cavs_picture *picture, const cavs_loop_filter_config *config,
    uint8_t field);

#endif
