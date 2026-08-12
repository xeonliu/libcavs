/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Decoder-owned unfiltered picture state shared by reconstruction and DPB.
 */
#ifndef CAVS_IMAGE_H
#define CAVS_IMAGE_H

#include <cavs/cavs.h>
#include "macroblock.h"
#include <stddef.h>
#include <stdint.h>

#define CAVS_FIELD_TOP UINT8_C(1)
#define CAVS_FIELD_BOTTOM UINT8_C(2)
#define CAVS_FIELD_BOTH (CAVS_FIELD_TOP | CAVS_FIELD_BOTTOM)

typedef struct cavs_picture {
    uint8_t *plane[3];
    ptrdiff_t stride[3];
    uint32_t coded_width;
    uint32_t coded_height;
    uint32_t display_width;
    uint32_t display_height;
    cavs_pixel_format format;
    cavs_picture_type picture_type;
    uint16_t picture_distance;
    int64_t pts;
    int64_t dts;
    uint16_t macroblock_width;
    uint16_t macroblock_height;
    cavs_macroblock *macroblocks;
    size_t macroblock_count;
    uint8_t completed_fields;
    uint8_t top_field_first;
    uint8_t repeat_first_field;
    uint8_t field_picture;
    uint8_t filtered;
    uint8_t is_reference;
    void *owner;
} cavs_picture;

/*
 * GB/T 20090.16-2016 6.2, 7.4 and 9.1 map field slice rows to alternating
 * frame lines. GB/T 20090.2-2013 shares the coordinate convention. This
 * helper performs coordinate mapping only and introduces no codec decision.
 */
cavs_result cavs_picture_field_position(
    const cavs_picture *picture, uint8_t field, uint16_t slice_row,
    uint16_t macroblock_column, uint32_t *sample_x, uint32_t *sample_y,
    ptrdiff_t *line_step);

#endif
