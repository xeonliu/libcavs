/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "codec/slice_decode.h"
#include "test.h"
#include <string.h>

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
}
