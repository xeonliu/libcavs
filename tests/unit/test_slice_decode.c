/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 6.3, 7.4 and 9.3 slice-row range tests.
 */
#include "codec/slice_decode.h"
#include "codec/broadcast_decode.h"
#include "dsp/prediction.h"
#include "test.h"
#include <string.h>

typedef struct slice_reader_state {
    uint32_t next_address;
    uint32_t end_address;
    size_t bit_offset;
} slice_reader_state;

/*
 * Supplies zero-residual intra macroblocks for GB/T 20090.16-2016 6.3,
 * 7.4 and 9.3 range/cursor lifecycle tests.
 */
static cavs_result read_zero_intra(void *opaque, uint32_t address,
                                   cavs_macroblock *macroblock) {
    slice_reader_state *state = (slice_reader_state *)opaque;
    if (state == NULL || macroblock == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (address >= state->end_address) return CAVS_EOF;
    if (address != state->next_address) return CAVS_ERR_CORRUPT_BITSTREAM;
    memset(macroblock, 0, sizeof(*macroblock));
    macroblock->address = address;
    macroblock->row = (uint16_t)(address / 2U);
    macroblock->column = (uint16_t)(address % 2U);
    macroblock->type = CAVS_MB_I_8X8;
    macroblock->is_intra = 1U;
    macroblock->transform_8x8 = 1U;
    macroblock->qp = 0U;
    macroblock->intra_chroma_mode = CAVS_INTRA_CHROMA_DC_8X8;
    macroblock->intra_luma_mode[0] = CAVS_INTRA_LUMA_DC_8X8;
    macroblock->intra_luma_mode[1] = CAVS_INTRA_LUMA_DC_8X8;
    macroblock->intra_luma_mode[2] = CAVS_INTRA_LUMA_DC_8X8;
    macroblock->intra_luma_mode[3] = CAVS_INTRA_LUMA_DC_8X8;
    macroblock->end_bit_offset = state->bit_offset;
    ++state->next_address;
    return CAVS_OK;
}

void test_slice_decode(void) {
    cavs_picture picture;
    cavs_slice_cursor cursor;
    memset(&picture, 0, sizeof(picture));
    picture.format = CAVS_YUV420P8;
    picture.coded_width = 32U;
    picture.coded_height = 64U;
    picture.stride[0] = 32;
    picture.macroblock_width = 2U;
    picture.macroblock_height = 4U;
    picture.field_picture = 1U;
    picture.top_field_first = 1U;

    {
        uint8_t field;
        uint16_t start_row;
        uint16_t end_row;
        TEST_CHECK(cavs_broadcast_field_for_row(
                       &picture, 0U, &field) == CAVS_OK &&
                   field == CAVS_FIELD_TOP);
        TEST_CHECK(cavs_broadcast_field_for_row(
                       &picture, 2U, &field) == CAVS_OK &&
                   field == CAVS_FIELD_BOTTOM);
        TEST_CHECK(cavs_broadcast_field_range(
                       &picture, CAVS_FIELD_TOP, &start_row,
                       &end_row) == CAVS_OK && start_row == 0U &&
                   end_row == 2U);
        TEST_CHECK(cavs_broadcast_field_range(
                       &picture, CAVS_FIELD_BOTTOM, &start_row,
                       &end_row) == CAVS_OK && start_row == 2U &&
                   end_row == 4U);
    }

    TEST_CHECK(cavs_slice_cursor_init(
                   &picture, CAVS_FIELD_TOP, 0U, 13U, 101U,
                   &cursor) == CAVS_OK);
    TEST_CHECK(cursor.bit_offset == 13U && cursor.bit_size == 101U);
    TEST_CHECK(cursor.macroblock_address == 0U && cursor.row == 0U &&
               cursor.column == 0U && cursor.field == CAVS_FIELD_TOP);
    TEST_CHECK(cavs_slice_cursor_init(
                   &picture, CAVS_FIELD_BOTTOM, 2U, 17U, 99U,
                   &cursor) == CAVS_OK);
    TEST_CHECK(cursor.macroblock_address == 4U && cursor.row == 2U &&
               cursor.field == CAVS_FIELD_BOTTOM);
    TEST_CHECK(cavs_slice_cursor_init(
                   &picture, CAVS_FIELD_BOTTOM, 0U, 0U, 8U,
                   &cursor) == CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(cavs_slice_cursor_init(
                   &picture, CAVS_FIELD_TOP, 0U, 9U, 8U,
                   &cursor) == CAVS_ERR_INVALID_ARGUMENT);

    {
        uint8_t luma[32U * 64U];
        uint8_t cb[16U * 32U];
        uint8_t cr[16U * 32U];
        cavs_macroblock macroblocks[8];
        cavs_broadcast_reconstruction_context reconstruction;
        slice_reader_state state;
        memset(luma, 128, sizeof(luma));
        memset(cb, 128, sizeof(cb));
        memset(cr, 128, sizeof(cr));
        picture.plane[0] = luma;
        picture.plane[1] = cb;
        picture.plane[2] = cr;
        picture.stride[1] = 16;
        picture.stride[2] = 16;
        picture.macroblocks = macroblocks;
        picture.macroblock_count = 8U;
        memset(&reconstruction, 0, sizeof(reconstruction));
        reconstruction.picture = &picture;
        reconstruction.field = CAVS_FIELD_TOP;
        state.next_address = 0U;
        state.end_address = 2U;
        state.bit_offset = 13U;
        TEST_CHECK(cavs_slice_cursor_init_range(
                       &picture, CAVS_FIELD_TOP, 0U, 1U, 13U, 101U,
                       &cursor) == CAVS_OK);
        TEST_CHECK(cavs_slice_decode(&cursor, read_zero_intra, &state,
                                     &reconstruction) == CAVS_OK);
        TEST_CHECK(cursor.finished != 0U && cursor.row == 1U &&
                   picture.completed_fields == 0U);

        /* GB/T 20090.16-2016 7.4.14/9.3: an early slice EOF is corrupt. */
        state.next_address = 0U;
        state.end_address = 1U;
        TEST_CHECK(cavs_slice_cursor_init_range(
                       &picture, CAVS_FIELD_TOP, 0U, 1U, 13U, 101U,
                       &cursor) == CAVS_OK);
        TEST_CHECK(cavs_slice_decode(&cursor, read_zero_intra, &state,
                                     &reconstruction) ==
                   CAVS_ERR_CORRUPT_BITSTREAM);
    }
}
