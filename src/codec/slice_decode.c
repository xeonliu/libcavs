/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 6.3, 7.4 and 9.3 slice reconstruction order.
 */
#include "codec/slice_decode.h"
#include <string.h>

/* Returns the exclusive syntax-row bound for a field; no codec decision. */
static uint16_t field_end_row(const cavs_picture *picture, uint8_t field) {
    uint16_t field_rows = (uint16_t)(picture->macroblock_height / 2U);
    if (field == CAVS_FIELD_BOTH) return picture->macroblock_height;
    if ((field == CAVS_FIELD_TOP) == (picture->top_field_first != 0U))
        return field_rows;
    return picture->macroblock_height;
}

cavs_result cavs_slice_cursor_init_range(
    const cavs_picture *picture, uint8_t field, uint16_t slice_row,
    uint16_t end_row, size_t header_bits, size_t payload_bits,
    cavs_slice_cursor *cursor) {
    cavs_slice_cursor parsed;
    uint32_t x;
    uint32_t y;
    ptrdiff_t step;
    cavs_result result;
    if (picture == NULL || cursor == NULL || picture->macroblock_width == 0U ||
        header_bits > payload_bits)
        return CAVS_ERR_INVALID_ARGUMENT;
    result = cavs_picture_field_position(picture, field, slice_row, 0U,
                                         &x, &y, &step);
    if (result != CAVS_OK) return result;
    if (end_row <= slice_row ||
        cavs_picture_field_position(picture, field, (uint16_t)(end_row - 1U),
                                    0U, &x, &y, &step) != CAVS_OK)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    (void)x;
    (void)y;
    (void)step;
    memset(&parsed, 0, sizeof(parsed));
    parsed.bit_offset = header_bits;
    parsed.bit_size = payload_bits;
    parsed.macroblock_address =
        (uint32_t)slice_row * picture->macroblock_width;
    parsed.row = slice_row;
    parsed.start_row = slice_row;
    parsed.end_row = end_row;
    parsed.field = field;
    *cursor = parsed;
    return CAVS_OK;
}

cavs_result cavs_slice_cursor_init(const cavs_picture *picture, uint8_t field,
                                   uint16_t slice_row, size_t header_bits,
                                   size_t payload_bits,
                                   cavs_slice_cursor *cursor) {
    uint16_t end_row;
    if (picture == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    end_row = field_end_row(picture, field);
    return cavs_slice_cursor_init_range(picture, field, slice_row, end_row,
                                        header_bits, payload_bits, cursor);
}

cavs_result cavs_slice_decode(
    cavs_slice_cursor *cursor, cavs_slice_macroblock_reader read_macroblock,
    void *reader_opaque,
    const cavs_broadcast_reconstruction_context *reconstruction) {
    cavs_picture *picture;
    if (cursor == NULL || read_macroblock == NULL || reconstruction == NULL ||
        reconstruction->picture == NULL || cursor->finished != 0U ||
        cursor->field != reconstruction->field ||
        cursor->bit_offset > cursor->bit_size)
        return CAVS_ERR_INVALID_ARGUMENT;
    picture = reconstruction->picture;
    if (picture->macroblock_width == 0U ||
        cursor->column >= picture->macroblock_width ||
        cursor->row >= picture->macroblock_height)
        return CAVS_ERR_INVALID_STATE;
    if (cursor->end_row <= cursor->start_row ||
        cursor->end_row > field_end_row(picture, cursor->field))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    for (;;) {
        cavs_macroblock macroblock;
        cavs_result result;
        memset(&macroblock, 0, sizeof(macroblock));
        result = read_macroblock(reader_opaque, cursor->macroblock_address,
                                 &macroblock);
        if (result == CAVS_EOF) {
            if (cursor->column != 0U || cursor->row != cursor->end_row)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            cursor->finished = 1U;
            return CAVS_OK;
        }
        if (result != CAVS_OK) return result;
        if (macroblock.address != cursor->macroblock_address ||
            macroblock.row != cursor->row ||
            macroblock.column != cursor->column ||
            macroblock.end_bit_offset < cursor->bit_offset ||
            macroblock.end_bit_offset > cursor->bit_size)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        result = cavs_broadcast_reconstruct_macroblock(reconstruction,
                                                       &macroblock);
        if (result != CAVS_OK) return result;
        cursor->bit_offset = macroblock.end_bit_offset;
        ++cursor->macroblock_address;
        ++cursor->column;
        if (cursor->column == picture->macroblock_width) {
            cursor->column = 0U;
            ++cursor->row;
            if (cursor->row > cursor->end_row)
                return CAVS_ERR_CORRUPT_BITSTREAM;
        }
        if (cursor->row == cursor->end_row) {
            result = read_macroblock(reader_opaque,
                                     cursor->macroblock_address, &macroblock);
            if (result != CAVS_EOF)
                return result == CAVS_OK ? CAVS_ERR_CORRUPT_BITSTREAM : result;
            cursor->finished = 1U;
            return CAVS_OK;
        }
    }
}
