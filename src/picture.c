/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "picture.h"
#include <limits.h>

/*
 * GB/T 20090.16-2016 3.25, 6.2 and 9.8.2; GB/T 20090.2-2013 3.27 and
 * 6.2. This maps a syntax macroblock row to frame storage. It performs
 * coordinate arithmetic only and introduces no codec decision.
 */
cavs_result cavs_picture_field_position(
    const cavs_picture *picture, uint8_t field, uint16_t slice_row,
    uint16_t macroblock_column, uint32_t *sample_x, uint32_t *sample_y,
    ptrdiff_t *line_step) {
    uint32_t field_rows;
    uint32_t field_ordinal;
    uint32_t local_row;
    uint32_t y;

    if (picture == NULL || sample_x == NULL || sample_y == NULL ||
        line_step == NULL || picture->format != CAVS_YUV420P8 ||
        picture->coded_width == 0U || picture->coded_height == 0U ||
        picture->macroblock_width == 0U || picture->macroblock_height == 0U ||
        picture->stride[0] <= 0 || macroblock_column >= picture->macroblock_width)
        return CAVS_ERR_INVALID_ARGUMENT;

    if (field == CAVS_FIELD_BOTH) {
        if (slice_row >= picture->macroblock_height ||
            picture->stride[0] > PTRDIFF_MAX)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        y = (uint32_t)slice_row * 16U;
        if (y >= picture->coded_height)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        *sample_x = (uint32_t)macroblock_column * 16U;
        *sample_y = y;
        *line_step = picture->stride[0];
        return CAVS_OK;
    }

    if ((field != CAVS_FIELD_TOP && field != CAVS_FIELD_BOTTOM) ||
        (picture->coded_height & 31U) != 0U ||
        (picture->macroblock_height & 1U) != 0U ||
        picture->stride[0] > PTRDIFF_MAX / 2)
        return CAVS_ERR_INVALID_ARGUMENT;
    field_rows = (uint32_t)picture->macroblock_height / 2U;
    field_ordinal = field == CAVS_FIELD_TOP ?
        (picture->top_field_first != 0U ? 0U : 1U) :
        (picture->top_field_first != 0U ? 1U : 0U);
    if ((uint32_t)slice_row < field_ordinal * field_rows ||
        (uint32_t)slice_row >= (field_ordinal + 1U) * field_rows)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    local_row = (uint32_t)slice_row - field_ordinal * field_rows;
    y = local_row * 32U + (field == CAVS_FIELD_BOTTOM ? 1U : 0U);
    if (y >= picture->coded_height)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *sample_x = (uint32_t)macroblock_column * 16U;
    *sample_y = y;
    *line_step = picture->stride[0] * 2;
    return CAVS_OK;
}
