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
    uint8_t data[64];
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

/* Writes the order-0 code form used by ue(v) syntax elements. */
static void write_ue(test_bitwriter *writer, uint32_t value) {
    uint32_t code = value + 1U;
    unsigned width = 0U;
    uint32_t scan = code;
    while (scan != 0U) {
        ++width;
        scan >>= 1;
    }
    write_bits(writer, 0U, width - 1U);
    write_bits(writer, code, width);
}

/* Writes the Table 43 signed-to-CodeNum mapping. */
static void write_se(test_bitwriter *writer, int32_t value) {
    uint32_t code = value > 0 ? (uint32_t)value * 2U - 1U :
                               (uint32_t)(-value) * 2U;
    write_ue(writer, code);
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
    assert(writer->position == 14U * 8U);
}

/* Builds a progressive baseline I-picture header with filtering disabled. */
static void make_baseline_i_picture(test_bitwriter *writer) {
    memset(writer, 0, sizeof(*writer));
    write_bits(writer, UINT32_C(0xffff), 16U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 7U, 8U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 32U, 6U);
    write_bits(writer, 0U, 4U);
    write_bits(writer, 1U, 1U);
}

/* Builds an interlaced low-delay header exercising conditional fields. */
static void make_interlaced_i_picture(test_bitwriter *writer) {
    uint32_t time_code = (12U << 18) | (34U << 12) | (56U << 6) | 20U;
    memset(writer, 0, sizeof(*writer));
    write_bits(writer, 100U, 16U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, time_code, 24U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 9U, 8U);
    write_ue(writer, 3U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 10U, 6U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 4U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_se(writer, -8);
    write_se(writer, 8);
}

/* Builds a broadcast header with independently selected weighting index. */
static void make_broadcast_i_picture(test_bitwriter *writer, uint8_t parameter_index) {
    static const int8_t deltas[6] = { -128, -3, 0, 4, 17, 127 };
    unsigned index;
    memset(writer, 0, sizeof(*writer));
    write_bits(writer, UINT32_C(0x1234), 16U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, UINT32_C(0x55), 7U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 22U, 8U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 45U, 6U);
    write_bits(writer, 0U, 4U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_se(writer, -16);
    write_se(writer, 16);
    write_bits(writer, parameter_index, 2U);
    write_bits(writer, 2U, 2U);
    if (parameter_index == 1U) {
        for (index = 0; index < 6U; ++index) write_se(writer, deltas[index]);
    }
    write_bits(writer, 1U, 1U);
}

/* Builds a progressive baseline P-picture header. */
static void make_baseline_p_picture(test_bitwriter *writer, uint8_t coding_type) {
    memset(writer, 0, sizeof(*writer));
    write_bits(writer, 50U, 16U);
    write_bits(writer, coding_type, 2U);
    write_bits(writer, 8U, 8U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 22U, 6U);
    if (coding_type != 2U) write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 3U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 1U, 1U);
}

/* Builds an interlaced baseline B field-picture header. */
static void make_baseline_b_field_picture(test_bitwriter *writer) {
    memset(writer, 0, sizeof(*writer));
    write_bits(writer, 70U, 16U);
    write_bits(writer, 2U, 2U);
    write_bits(writer, 11U, 8U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 18U, 6U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 3U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
}

/* Builds a broadcast B frame header with its implicit reference flag. */
static void make_broadcast_b_picture(test_bitwriter *writer) {
    memset(writer, 0, sizeof(*writer));
    write_bits(writer, UINT32_C(0x4321), 16U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 3U, 7U);
    write_bits(writer, 2U, 2U);
    write_bits(writer, 12U, 8U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 30U, 6U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 2U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 1U, 1U);
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

/* Covers profile-specific I-picture branches and normative value ranges. */
static void test_i_picture_headers(void) {
    test_bitwriter writer;
    cavs_sequence_info sequence;
    cavs_i_picture_header picture;

    memset(&sequence, 0, sizeof(sequence));
    sequence.profile_id = UINT8_C(0x20);
    sequence.progressive_sequence = 1U;
    make_baseline_i_picture(&writer);
    assert(cavs_parse_i_picture_header(writer.data, writer.position,
                                       &sequence, &picture) == CAVS_OK);
    assert(picture.bbv_delay == UINT32_C(0xffff));
    assert(picture.picture_distance == 7U && picture.picture_qp == 32U);
    assert(picture.progressive_frame == 1U && picture.picture_structure == 1U);
    assert(picture.loop_filter_disable == 1U);
    assert(cavs_parse_i_picture_header(writer.data, writer.position - 1U,
                                       &sequence, &picture) == CAVS_ERR_CORRUPT_BITSTREAM);

    sequence.progressive_sequence = 0U;
    sequence.low_delay = 1U;
    make_interlaced_i_picture(&writer);
    assert(cavs_parse_i_picture_header(writer.data, writer.position,
                                       &sequence, &picture) == CAVS_OK);
    assert(picture.has_time_code == 1U && picture.bbv_check_times == 3U);
    assert(picture.picture_structure == 0U && picture.skip_mode_flag == 1U);
    assert(picture.alpha_c_offset == -8 && picture.beta_offset == 8);

    memset(&sequence, 0, sizeof(sequence));
    sequence.profile_id = UINT8_C(0x48);
    sequence.progressive_sequence = 1U;
    make_broadcast_i_picture(&writer, 1U);
    assert(cavs_parse_i_picture_header(writer.data, writer.position,
                                       &sequence, &picture) == CAVS_OK);
    assert(picture.bbv_delay == ((UINT32_C(0x1234) << 7) | UINT32_C(0x55)));
    assert(picture.weighting_quant_flag == 1U);
    assert(picture.chroma_quant_parameter_delta_cb == -16);
    assert(picture.chroma_quant_parameter_delta_cr == 16);
    assert(picture.weighting_quant_parameter_delta1[0] == -128);
    assert(picture.weighting_quant_parameter_delta1[5] == 127);
    assert(picture.advanced_entropy_enabled == 1U);

    make_broadcast_i_picture(&writer, 3U);
    assert(cavs_parse_i_picture_header(writer.data, writer.position,
                                       &sequence, &picture) == CAVS_ERR_CORRUPT_BITSTREAM);
}

/* Covers P/B coding types, field conditionals, and broadcast-only fields. */
static void test_pb_picture_headers(void) {
    test_bitwriter writer;
    cavs_sequence_info sequence;
    cavs_pb_picture_header picture;

    memset(&sequence, 0, sizeof(sequence));
    sequence.profile_id = UINT8_C(0x20);
    sequence.progressive_sequence = 1U;
    make_baseline_p_picture(&writer, 1U);
    assert(cavs_parse_pb_picture_header(writer.data, writer.position,
                                        &sequence, &picture) == CAVS_OK);
    assert(picture.picture_coding_type == 1U && picture.picture_qp == 22U);
    assert(picture.picture_reference_flag == 0U && picture.skip_mode_flag == 1U);
    assert(cavs_parse_pb_picture_header(writer.data, writer.position - 1U,
                                        &sequence, &picture) == CAVS_ERR_CORRUPT_BITSTREAM);
    make_baseline_p_picture(&writer, 0U);
    assert(cavs_parse_pb_picture_header(writer.data, writer.position,
                                        &sequence, &picture) == CAVS_ERR_CORRUPT_BITSTREAM);

    sequence.progressive_sequence = 0U;
    make_baseline_b_field_picture(&writer);
    assert(cavs_parse_pb_picture_header(writer.data, writer.position,
                                        &sequence, &picture) == CAVS_OK);
    assert(picture.picture_coding_type == 2U && picture.picture_structure == 0U);
    assert(picture.advanced_prediction_mode_disable == 1U);
    assert(picture.picture_reference_flag == 0U);

    memset(&sequence, 0, sizeof(sequence));
    sequence.profile_id = UINT8_C(0x48);
    sequence.progressive_sequence = 1U;
    make_broadcast_b_picture(&writer);
    assert(cavs_parse_pb_picture_header(writer.data, writer.position,
                                        &sequence, &picture) == CAVS_OK);
    assert(picture.bbv_delay == ((UINT32_C(0x4321) << 7) | 3U));
    assert(picture.picture_coding_type == 2U && picture.picture_reference_flag == 1U);
    assert(picture.pb_field_enhanced_flag == 1U);
    assert(picture.weighting_quant_flag == 0U);
    assert(picture.advanced_entropy_enabled == 1U);
}

/* Entry point called by the shared unit-test executable. */
void test_syntax(void) {
    test_start_codes();
    test_sequence_headers();
    test_i_picture_headers();
    test_pb_picture_headers();
}
