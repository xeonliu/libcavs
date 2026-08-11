/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Independent baseline YUV420 basic-entropy macroblock syntax vectors.
 */
#include "macroblock.h"
#include <assert.h>
#include <string.h>

typedef struct mb_bitwriter {
    uint8_t data[128];
    size_t position;
} mb_bitwriter;

static void write_bits(mb_bitwriter *writer, uint32_t value, unsigned width) {
    unsigned index;
    for (index = 0U; index < width; ++index) {
        unsigned shift = width - index - 1U;
        uint32_t bit = (value >> shift) & UINT32_C(1);
        writer->data[writer->position / 8U] |=
            (uint8_t)(bit << (7U - (writer->position % 8U)));
        ++writer->position;
    }
}

static void write_ue(mb_bitwriter *writer, uint32_t value) {
    uint32_t code = value + 1U;
    uint32_t scan = code;
    unsigned width = 0U;
    while (scan != 0U) { ++width; scan >>= 1; }
    write_bits(writer, 0U, width - 1U);
    write_bits(writer, code, width);
}

static void write_se(mb_bitwriter *writer, int32_t value) {
    uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
    uint32_t code = value > 0 ? magnitude * 2U - 1U : magnitude * 2U;
    write_ue(writer, code);
}

static void write_ue_k(mb_bitwriter *writer, uint32_t value, unsigned order) {
    unsigned zeros = 0U;
    unsigned suffix_bits;
    uint64_t base;
    uint64_t span;
    for (;;) {
        suffix_bits = zeros + order;
        base = (UINT64_C(1) << suffix_bits) - (UINT64_C(1) << order);
        span = UINT64_C(1) << suffix_bits;
        if ((uint64_t)value < base + span) break;
        ++zeros;
    }
    write_bits(writer, 0U, zeros);
    write_bits(writer, 1U, 1U);
    write_bits(writer, (uint32_t)((uint64_t)value - base), suffix_bits);
}

static void write_single_coefficient(mb_bitwriter *writer,
                                     cavs_basic_block_kind kind) {
    write_ue_k(writer, 0U, kind == CAVS_BASIC_INTER_LUMA ? 3U : 2U);
    write_ue_k(writer, kind == CAVS_BASIC_INTRA_LUMA ? 8U :
                       (kind == CAVS_BASIC_INTER_LUMA ? 2U : 0U),
               kind == CAVS_BASIC_CHROMA ? 0U : 2U);
}

static cavs_baseline420_mb_context default_context(cavs_picture_type picture_type) {
    cavs_baseline420_mb_context context;
    memset(&context, 0, sizeof(context));
    context.profile_id = UINT8_C(0x20);
    context.format = CAVS_YUV420P8;
    context.picture_type = picture_type;
    context.picture_structure = 1U;
    context.picture_reference_flag = 1U;
    context.previous_qp = 20U;
    context.reference_index_bits = 1U;
    context.macroblock_width = 2U;
    context.macroblock_height = 2U;
    return context;
}

static void write_intra_prediction(mb_bitwriter *writer) {
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 2U, 2U);
    write_bits(writer, 1U, 1U);
    write_bits(writer, 0U, 1U);
    write_bits(writer, 0U, 2U);
    write_ue(writer, 2U);
}

static void test_implicit_i_macroblock(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_I);
    cavs_baseline420_mb_header header;
    mb_bitwriter writer;
    memset(&writer, 0, sizeof(writer));
    write_bits(&writer, 5U, 3U);
    write_intra_prediction(&writer);
    write_se(&writer, -2);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 3U,
                                            &context, &header) == CAVS_OK);
    assert(header.type_index == 5U && header.is_intra == 1U);
    assert(header.raw_type == 0U && header.motion_vector_count == 0U);
    assert(header.prediction_mode_flag[0] == 1U);
    assert(header.prediction_mode_flag[1] == 0U);
    assert(header.intra_luma_prediction_mode[1] == 2U);
    assert(header.intra_chroma_prediction_mode == 2U);
    assert(header.coded_block_pattern == 63U);
    assert(header.qp_delta == -2 && header.qp == 18U);
    assert(header.end_bit_offset == writer.position);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position - 1U, 3U,
                                            &context, &header) ==
           CAVS_ERR_CORRUPT_BITSTREAM);
}

static void test_p_skip_and_partitions(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_baseline420_mb_header header;
    mb_bitwriter writer;
    static const int32_t differences[8] = { 1, -1, 2, -2, 3, -3, 4, -4 };
    unsigned index;

    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 0U);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) == CAVS_OK);
    assert(header.is_skipped == 1U && header.end_bit_offset == 1U);

    memset(&writer, 0, sizeof(writer));
    context.picture_reference_flag = 0U;
    write_ue(&writer, 4U);
    for (index = 0U; index < 4U; ++index) write_bits(&writer, index & 1U, 1U);
    for (index = 0U; index < 8U; ++index) write_se(&writer, differences[index]);
    write_ue(&writer, 0U);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) == CAVS_OK);
    assert(header.type_index == 4U && header.motion_vector_count == 4U);
    assert(header.reference_index[1] == 1U && header.reference_index[3] == 1U);
    assert(header.motion_vector_difference_x[2] == 3);
    assert(header.motion_vector_difference_y[3] == -4);
    assert(header.coded_block_pattern == 0U && header.qp == 20U);
}

static void test_b_8x8_macroblock(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_B);
    cavs_baseline420_mb_header header;
    mb_bitwriter writer;
    unsigned index;
    memset(&writer, 0, sizeof(writer));
    context.fixed_qp = 1U;
    context.mb_weighting_flag = 1U;
    write_ue(&writer, 23U);
    write_bits(&writer, 0U, 2U);
    write_bits(&writer, 1U, 2U);
    write_bits(&writer, 2U, 2U);
    write_bits(&writer, 3U, 2U);
    for (index = 0U; index < 3U; ++index) {
        write_se(&writer, (int32_t)index + 1);
        write_se(&writer, -((int32_t)index + 1));
    }
    write_bits(&writer, 1U, 1U);
    write_ue(&writer, 2U);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) == CAVS_OK);
    assert(header.type_index == 23U && header.motion_vector_count == 3U);
    assert(header.partition_type[0] == 0U && header.partition_type[3] == 3U);
    assert(header.weighting_prediction == 1U);
    assert(header.coded_block_pattern == 63U);
}

static void test_table45_mappings(void) {
    static const uint8_t expected_intra[64] = {
        63, 15, 31, 47, 0, 14, 13, 11, 7, 5, 10, 8, 12, 61, 4, 55,
        1, 2, 59, 3, 62, 9, 6, 29, 45, 51, 23, 39, 27, 46, 53, 30,
        43, 37, 60, 16, 21, 28, 19, 35, 42, 26, 44, 32, 58, 24, 20, 17,
        18, 48, 22, 33, 25, 49, 40, 36, 34, 50, 52, 54, 41, 56, 38, 57
    };
    static const uint8_t expected_inter[64] = {
        0, 15, 63, 31, 16, 32, 47, 13, 14, 11, 12, 5, 10, 7, 48, 3,
        2, 8, 4, 1, 61, 55, 59, 62, 29, 27, 23, 19, 30, 28, 9, 6,
        60, 21, 44, 26, 51, 35, 18, 20, 24, 53, 17, 37, 39, 45, 58, 43,
        42, 46, 36, 33, 34, 40, 52, 49, 50, 56, 25, 22, 54, 57, 41, 38
    };
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_baseline420_mb_header header;
    mb_bitwriter writer;
    unsigned code;
    context.fixed_qp = 1U;
    for (code = 0U; code < 64U; ++code) {
        memset(&writer, 0, sizeof(writer));
        write_ue(&writer, 1U);
        write_se(&writer, 0);
        write_se(&writer, 0);
        write_ue(&writer, code);
        assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                                &context, &header) == CAVS_OK);
        assert(header.coded_block_pattern == expected_inter[code]);

        memset(&writer, 0, sizeof(writer));
        write_ue(&writer, 5U + code);
        write_bits(&writer, 1U, 1U);
        write_bits(&writer, 1U, 1U);
        write_bits(&writer, 1U, 1U);
        write_bits(&writer, 1U, 1U);
        write_ue(&writer, 0U);
        assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                                &context, &header) == CAVS_OK);
        assert(header.coded_block_pattern == expected_intra[code]);
    }
}

static void test_type_tables(void) {
    static const uint8_t p_counts[6] = { 0, 1, 2, 2, 4, 0 };
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_baseline420_mb_header header;
    mb_bitwriter writer;
    unsigned type;
    unsigned index;
    context.fixed_qp = 1U;
    for (type = 0U; type <= 5U; ++type) {
        memset(&writer, 0, sizeof(writer));
        write_ue(&writer, type);
        if (type == 5U) {
            write_bits(&writer, 1U, 1U);
            write_bits(&writer, 1U, 1U);
            write_bits(&writer, 1U, 1U);
            write_bits(&writer, 1U, 1U);
            write_ue(&writer, 0U);
        } else if (type != 0U) {
            for (index = 0U; index < p_counts[type]; ++index) {
                write_se(&writer, 0);
                write_se(&writer, 0);
            }
            write_ue(&writer, 0U);
        }
        assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                                &context, &header) == CAVS_OK);
        assert(header.type_index == type);
        assert(header.motion_vector_count == p_counts[type]);
    }

    context = default_context(CAVS_PICTURE_B);
    context.fixed_qp = 1U;
    for (type = 0U; type <= 24U; ++type) {
        unsigned expected = type >= 2U && type <= 4U ? 1U :
                            (type >= 5U && type <= 22U ? 2U : 0U);
        memset(&writer, 0, sizeof(writer));
        write_ue(&writer, type);
        if (type == 23U) {
            for (index = 0U; index < 4U; ++index) write_bits(&writer, 0U, 2U);
            write_ue(&writer, 0U);
        } else if (type == 24U) {
            write_bits(&writer, 1U, 1U);
            write_bits(&writer, 1U, 1U);
            write_bits(&writer, 1U, 1U);
            write_bits(&writer, 1U, 1U);
            write_ue(&writer, 0U);
        } else if (type != 0U) {
            for (index = 0U; index < expected; ++index) {
                write_se(&writer, 0);
                write_se(&writer, 0);
            }
            write_ue(&writer, 0U);
        }
        assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                                &context, &header) == CAVS_OK);
        assert(header.type_index == type);
        assert(header.motion_vector_count == expected);
    }
}

static void test_interlaced_i_second_field(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_I);
    cavs_baseline420_mb_header header;
    mb_bitwriter writer;
    context.picture_structure = 0U;
    context.macroblock_index = 2U;
    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 0U);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) == CAVS_OK);
    assert(header.type_index == 0U && header.is_skipped == 1U);

    context.macroblock_index = 1U;
    memset(&writer, 0, sizeof(writer));
    write_intra_prediction(&writer);
    write_se(&writer, 0);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) == CAVS_OK);
    assert(header.type_index == 5U && header.is_intra == 1U);
}

static void test_invalid_macroblocks(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_baseline420_mb_header header;
    mb_bitwriter writer;
    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 1U);
    write_se(&writer, 4096);
    write_se(&writer, 0);
    write_ue(&writer, 0U);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) ==
           CAVS_ERR_CORRUPT_BITSTREAM);
    context.format = CAVS_YUV422P8;
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) ==
           CAVS_ERR_INVALID_ARGUMENT);

    context = default_context(CAVS_PICTURE_I);
    context.previous_qp = 0U;
    memset(&writer, 0, sizeof(writer));
    write_intra_prediction(&writer);
    write_se(&writer, -1);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) ==
           CAVS_ERR_CORRUPT_BITSTREAM);

    context.fixed_qp = 1U;
    memset(&writer, 0, sizeof(writer));
    write_bits(&writer, 1U, 1U);
    write_bits(&writer, 1U, 1U);
    write_bits(&writer, 1U, 1U);
    write_bits(&writer, 1U, 1U);
    write_ue(&writer, 4U);
    assert(cavs_parse_baseline420_mb_header(writer.data, writer.position, 0U,
                                            &context, &header) ==
           CAVS_ERR_CORRUPT_BITSTREAM);
}

static void write_full_i_macroblock(mb_bitwriter *writer) {
    unsigned index;
    write_intra_prediction(writer);
    for (index = 0U; index < CAVS_BASELINE420_MB_BLOCKS; ++index)
        write_single_coefficient(writer, index < 4U ?
            CAVS_BASIC_INTRA_LUMA : CAVS_BASIC_CHROMA);
}

static void test_complete_i_macroblock_cursor(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_I);
    cavs_baseline420_macroblock first;
    cavs_baseline420_macroblock second;
    mb_bitwriter writer;
    size_t boundary;
    unsigned index;
    memset(&writer, 0, sizeof(writer));
    context.fixed_qp = 1U;
    context.macroblock_height = 1U;
    write_full_i_macroblock(&writer);
    boundary = writer.position;
    write_full_i_macroblock(&writer);
    assert((boundary & 7U) != 0U);
    assert(cavs_decode_baseline420_macroblock(
               writer.data, writer.position, 0U, &context, &first) == CAVS_OK);
    assert(first.header.is_intra == 1U && first.header.qp == 20U);
    assert(first.header.coded_block_pattern == 63U);
    assert(first.end_bit_offset == boundary);
    for (index = 0U; index < CAVS_BASELINE420_MB_BLOCKS; ++index) {
        assert(first.block_coded[index] == 1U);
        assert(first.block[index].count == 1U);
        assert(first.block[index].scan_coefficients[0] == 1);
    }
    context.macroblock_index = 1U;
    assert(cavs_decode_baseline420_macroblock(
               writer.data, writer.position, first.end_bit_offset,
               &context, &second) == CAVS_OK);
    assert(second.end_bit_offset == writer.position);
    assert(second.block[5].scan_coefficients[0] == 1);
}

static void test_complete_inter_and_empty_macroblocks(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_baseline420_macroblock macroblock;
    mb_bitwriter writer;
    unsigned index;
    memset(&writer, 0, sizeof(writer));
    context.fixed_qp = 1U;
    write_ue(&writer, 1U);
    write_se(&writer, 0);
    write_se(&writer, 0);
    write_ue(&writer, 19U);
    write_single_coefficient(&writer, CAVS_BASIC_INTER_LUMA);
    assert(cavs_decode_baseline420_macroblock(
               writer.data, writer.position, 0U,
               &context, &macroblock) == CAVS_OK);
    assert(macroblock.header.coded_block_pattern == 1U);
    assert(macroblock.block_coded[0] == 1U);
    assert(macroblock.block[0].scan_coefficients[0] == 1);
    for (index = 1U; index < CAVS_BASELINE420_MB_BLOCKS; ++index)
        assert(macroblock.block_coded[index] == 0U);
    assert(macroblock.end_bit_offset == writer.position);

    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 0U);
    assert(cavs_decode_baseline420_macroblock(
               writer.data, writer.position, 0U,
               &context, &macroblock) == CAVS_OK);
    assert(macroblock.header.is_skipped == 1U);
    assert(macroblock.end_bit_offset == writer.position);
    for (index = 0U; index < CAVS_BASELINE420_MB_BLOCKS; ++index)
        assert(macroblock.block_coded[index] == 0U);
}

static void test_complete_macroblock_truncation_atomic(void) {
    cavs_baseline420_mb_context context = default_context(CAVS_PICTURE_I);
    cavs_baseline420_macroblock macroblock;
    cavs_baseline420_macroblock unchanged;
    mb_bitwriter writer;
    memset(&writer, 0, sizeof(writer));
    context.fixed_qp = 1U;
    write_full_i_macroblock(&writer);
    memset(&macroblock, 0xa5, sizeof(macroblock));
    unchanged = macroblock;
    assert(cavs_decode_baseline420_macroblock(
               writer.data, writer.position - 1U, 0U,
               &context, &macroblock) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(memcmp(&macroblock, &unchanged, sizeof(macroblock)) == 0);
    assert(cavs_decode_baseline420_macroblock(
               writer.data, writer.position, 0U,
               &context, NULL) == CAVS_ERR_INVALID_ARGUMENT);
}

void test_macroblock(void) {
    test_implicit_i_macroblock();
    test_p_skip_and_partitions();
    test_b_8x8_macroblock();
    test_table45_mappings();
    test_type_tables();
    test_interlaced_i_second_field();
    test_invalid_macroblocks();
    test_complete_i_macroblock_cursor();
    test_complete_inter_and_empty_macroblocks();
    test_complete_macroblock_truncation_atomic();
}
