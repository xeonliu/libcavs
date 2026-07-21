/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Independent syntax vectors for start codes and common sequence headers.
 */
#include "syntax.h"
#include <assert.h>
#include <string.h>

typedef struct test_bitwriter {
    uint8_t data[14];
    size_t position;
} test_bitwriter;

/* Appends one fixed-width field to a compact test-only sequence header. */
static void write_bits(test_bitwriter *writer, uint32_t value, unsigned width) {
    unsigned index;
    for (index = 0; index < width; ++index) {
        unsigned shift = width - index - 1U;
        unsigned bit = (value >> shift) & 1U;
        writer->data[writer->position / 8U] |=
            (uint8_t)(bit << (7U - (writer->position % 8U)));
        ++writer->position;
    }
}

/* Builds the common 112-bit syntax from Part 2 Table 15 / Part 16 Table 14. */
static void make_sequence_header(uint8_t profile, test_bitwriter *writer) {
    memset(writer, 0, sizeof(*writer));
    write_bits(writer, profile, 8U);
    write_bits(writer, UINT32_C(0x22), 8U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 1920U, 14U);
    write_bits(writer, 1080U, 14U);
    write_bits(writer, 1U, 2U);
    write_bits(writer, 1U, 3U);
    write_bits(writer, 3U, 4U);
    write_bits(writer, 3U, 4U);
    write_bits(writer, 1000U, 18U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 2U, 12U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 4U, 18U);
    write_bits(writer, 0U, 3U);
    assert(writer->position == sizeof(writer->data) * 8U);
}

/* Tests every explicit and ranged start-code classification. */
static void test_start_codes(void) {
    assert(cavs_classify_start_code(0U) == CAVS_UNIT_SLICE);
    assert(cavs_classify_start_code(UINT8_C(0xaf)) == CAVS_UNIT_SLICE);
    assert(cavs_classify_start_code(UINT8_C(0xb0)) == CAVS_UNIT_SEQUENCE_HEADER);
    assert(cavs_classify_start_code(UINT8_C(0xb1)) == CAVS_UNIT_SEQUENCE_END);
    assert(cavs_classify_start_code(UINT8_C(0xb2)) == CAVS_UNIT_USER_DATA);
    assert(cavs_classify_start_code(UINT8_C(0xb3)) == CAVS_UNIT_I_PICTURE);
    assert(cavs_classify_start_code(UINT8_C(0xb5)) == CAVS_UNIT_EXTENSION);
    assert(cavs_classify_start_code(UINT8_C(0xb6)) == CAVS_UNIT_PB_PICTURE);
    assert(cavs_classify_start_code(UINT8_C(0xb7)) == CAVS_UNIT_VIDEO_EDIT);
    assert(cavs_classify_start_code(UINT8_C(0xb8)) == CAVS_UNIT_RESERVED);
    assert(cavs_classify_start_code(UINT8_C(0xff)) == CAVS_UNIT_SYSTEM);
}

/* Verifies supported profiles and the public interpretation of common fields. */
static void test_sequence_headers(void) {
    test_bitwriter writer;
    cavs_sequence_info sequence;
    make_sequence_header(UINT8_C(0x20), &writer);
    assert(cavs_parse_sequence_header(writer.data, sizeof(writer.data), &sequence) == CAVS_OK);
    assert(sequence.profile_id == UINT8_C(0x20));
    assert(sequence.display_width == 1920U && sequence.display_height == 1080U);
    assert(sequence.format == CAVS_YUV420P8);
    assert(sequence.bit_rate == UINT64_C(210115200));
    assert(sequence.bbv_buffer_size_bits == UINT64_C(65536));
    make_sequence_header(UINT8_C(0x48), &writer);
    assert(cavs_parse_sequence_header(writer.data, sizeof(writer.data), &sequence) == CAVS_OK);
    assert(cavs_parse_sequence_header(writer.data, 1U, &sequence) == CAVS_ERR_CORRUPT_BITSTREAM);
    writer.data[1] = UINT8_C(0xff);
    assert(cavs_parse_sequence_header(writer.data, sizeof(writer.data), &sequence) == CAVS_ERR_UNSUPPORTED_LEVEL);
    make_sequence_header(UINT8_C(0x20), &writer);
    writer.data[9] &= UINT8_C(0xf7);
    assert(cavs_parse_sequence_header(writer.data, sizeof(writer.data), &sequence) == CAVS_ERR_CORRUPT_BITSTREAM);
    make_sequence_header(UINT8_C(0x48), &writer);
    writer.data[0] = UINT8_C(0x24);
    assert(cavs_parse_sequence_header(writer.data, sizeof(writer.data), &sequence) == CAVS_ERR_UNSUPPORTED_PROFILE);
}

/* Entry point called by the shared unit-test executable. */
void test_syntax(void) {
    test_start_codes();
    test_sequence_headers();
}
