/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.5-7.6, 8.2 and 9.1 basic-entropy vectors.
 * Samples are generated from the normative syntax and the independent
 * Annex-D VLC tests; the reference decoder is not used as an oracle.
 */
#include "codec/broadcast_basic_macroblock.h"
#include "codec/coefficients.h"
#include "test.h"
#include <string.h>

typedef struct basic_writer {
    uint8_t data[512];
    size_t bit_pos;
} basic_writer;

static void write_bits(basic_writer *writer, uint32_t value, unsigned width) {
    unsigned index;
    for (index = 0U; index < width; ++index) {
        unsigned shift = width - index - 1U;
        writer->data[writer->bit_pos / 8U] |=
            (uint8_t)(((value >> shift) & 1U) <<
                      (7U - (writer->bit_pos % 8U)));
        ++writer->bit_pos;
    }
}

static void write_ue(basic_writer *writer, uint32_t value) {
    uint32_t code = value + 1U;
    uint32_t scan = code;
    unsigned width = 0U;
    while (scan != 0U) {
        ++width;
        scan >>= 1U;
    }
    write_bits(writer, 0U, width - 1U);
    write_bits(writer, code, width);
}

static void write_ue_k(basic_writer *writer, uint32_t value, unsigned order) {
    unsigned zeros = 0U;
    for (;;) {
        unsigned width = zeros + order;
        uint64_t base = (UINT64_C(1) << width) -
            (UINT64_C(1) << order);
        uint64_t span = UINT64_C(1) << width;
        if ((uint64_t)value < base + span) {
            write_bits(writer, 0U, zeros);
            write_bits(writer, 1U, 1U);
            write_bits(writer, (uint32_t)((uint64_t)value - base), width);
            return;
        }
        ++zeros;
    }
}

static void write_se(basic_writer *writer, int32_t value) {
    uint32_t code = value > 0 ? (uint32_t)value * 2U - 1U :
        (uint32_t)(-value) * 2U;
    write_ue(writer, code);
}

/* First-entry coefficient vectors use the normative Annex-D orders. */
static void write_single_coefficient(basic_writer *writer,
                                     cavs_basic_block_kind kind) {
    write_ue_k(writer, 0U, kind == CAVS_BASIC_INTER_LUMA ? 3U : 2U);
    write_ue_k(writer, kind == CAVS_BASIC_INTRA_LUMA ? 8U :
                       (kind == CAVS_BASIC_INTER_LUMA ? 2U : 0U),
               kind == CAVS_BASIC_CHROMA ? 0U : 2U);
}

static cavs_broadcast_basic_mb_context default_context(cavs_picture_type type) {
    cavs_broadcast_basic_mb_context context;
    memset(&context, 0, sizeof(context));
    context.profile_id = UINT8_C(0x48);
    context.format = CAVS_YUV420P8;
    context.picture_type = type;
    context.progressive_frame = 1U;
    context.picture_structure = 1U;
    context.picture_reference_flag = 1U;
    context.fixed_qp = 1U;
    context.previous_qp = 20U;
    context.macroblock_width = 1U;
    context.macroblock_height = 1U;
    return context;
}

static void write_intra_header(basic_writer *writer) {
    unsigned index;
    for (index = 0U; index < 4U; ++index) write_bits(writer, 1U, 1U);
    write_ue(writer, 0U);
}

static void write_full_intra_blocks(basic_writer *writer) {
    unsigned index;
    for (index = 0U; index < CAVS_MB_8X8_BLOCKS; ++index)
        write_single_coefficient(writer, index < 4U ?
            CAVS_BASIC_INTRA_LUMA : CAVS_BASIC_CHROMA);
}

static void test_implicit_i_and_coefficients(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_I);
    cavs_macroblock macroblock;
    memset(&writer, 0, sizeof(writer));
    write_intra_header(&writer);
    write_full_intra_blocks(&writer);
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.type == CAVS_MB_I_8X8 && macroblock.is_intra != 0U);
    TEST_CHECK(macroblock.coded_block_pattern == 63U);
    TEST_CHECK(macroblock.coefficient_count_8x8[0] == 1U &&
               macroblock.coefficient_count_8x8[5] == 1U);
    TEST_CHECK(macroblock.coefficients_8x8[0][0] == 1 &&
               macroblock.end_bit_offset == writer.bit_pos);
}

static void test_p_motion_and_cbp(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_macroblock macroblock;
    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 1U); /* Table 55 P_16x16. */
    write_se(&writer, 0);
    write_se(&writer, 0);
    write_ue(&writer, 19U); /* Table 42 inter CodeNum 19 -> MbCBP 1. */
    write_single_coefficient(&writer, CAVS_BASIC_INTER_LUMA);
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.type == CAVS_MB_P_16X16 &&
               macroblock.partition_count == 1U);
    TEST_CHECK(macroblock.partition[0].motion[CAVS_PRED_FORWARD].valid != 0U);
    TEST_CHECK(macroblock.coded_block_pattern == 1U &&
               macroblock.coefficient_count_8x8[0] == 1U);
}

static void test_b_8x8_and_skip_run(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_B);
    cavs_macroblock macroblock;
    uint32_t skip_run;
    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 23U); /* Table 56 B_8x8. */
    write_bits(&writer, 0U, 2U);
    write_bits(&writer, 1U, 2U);
    write_bits(&writer, 2U, 2U);
    write_bits(&writer, 3U, 2U);
    write_se(&writer, 0);
    write_se(&writer, 0);
    write_se(&writer, 0);
    write_se(&writer, 0);
    write_se(&writer, 0);
    write_se(&writer, 0);
    write_ue(&writer, 0U); /* Table 42 inter CodeNum 0 -> MbCBP 0. */
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.type == CAVS_MB_B_INTER &&
               macroblock.partition_count == 4U &&
               macroblock.partition[0].direction == CAVS_PRED_BIDIRECTIONAL &&
               macroblock.partition[1].direction == CAVS_PRED_FORWARD &&
               macroblock.partition[2].direction == CAVS_PRED_BACKWARD &&
               macroblock.partition[3].direction == CAVS_PRED_SYMMETRIC);

    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 2U);
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_broadcast_basic_skip_run(&decoder, 3U, &skip_run) == CAVS_OK &&
               skip_run == 2U && decoder.bit_offset == writer.bit_pos);
}

static void test_truncation_atomic(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_I);
    cavs_macroblock macroblock;
    cavs_macroblock unchanged;
    memset(&writer, 0, sizeof(writer));
    write_intra_header(&writer);
    write_full_intra_blocks(&writer);
    memset(&macroblock, 0xa5, sizeof(macroblock));
    unchanged = macroblock;
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data,
                                         writer.bit_pos - 1U, 0U) == CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(&decoder, &context,
                                                      &macroblock) ==
               CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(memcmp(&macroblock, &unchanged, sizeof(macroblock)) == 0);
}

/* GB/T 20090.16-2016 7.5.7-7.5.9: reference/MVD order and Basic weighting. */
static void test_basic_field_type_and_motion_order(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_macroblock macroblock;
    unsigned index;
    memset(&writer, 0, sizeof(writer));
    context.picture_structure = 0U;
    context.picture_reference_flag = 0U;
    context.progressive_frame = 0U;
    context.macroblock_width = 4U;
    context.macroblock_height = 2U;
    context.macroblock_index = 1U;
    write_ue(&writer, 4U); /* Table 55 P_8x8. */
    for (index = 0U; index < 4U; ++index) write_bits(&writer, index & 3U, 2U);
    write_se(&writer, -4096); write_se(&writer, 4095);
    write_se(&writer, -1);   write_se(&writer, 1);
    write_se(&writer, 0);    write_se(&writer, 0);
    write_se(&writer, 2);    write_se(&writer, -2);
    write_ue(&writer, 0U); /* Table 42 inter CodeNum 0 -> MbCBP 0. */
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.type == CAVS_MB_P_8X8 &&
               macroblock.partition_count == 4U);
    for (index = 0U; index < 4U; ++index) {
        TEST_CHECK(macroblock.partition[index].motion[CAVS_PRED_FORWARD].valid != 0U);
        TEST_CHECK(macroblock.partition[index].motion[CAVS_PRED_FORWARD].reference_index ==
                   (int8_t)(index & 3U));
    }
    TEST_CHECK(macroblock.partition[0].motion[CAVS_PRED_FORWARD].x == -4096);
    TEST_CHECK(macroblock.partition[0].motion[CAVS_PRED_FORWARD].y == 4095);
    TEST_CHECK(macroblock.partition[3].motion[CAVS_PRED_FORWARD].x == 2);
    TEST_CHECK(macroblock.partition[3].motion[CAVS_PRED_FORWARD].y == -2);

    /* An I second field follows the P-like Table 55 path, section 9.2(a)(2). */
    memset(&writer, 0, sizeof(writer));
    context = default_context(CAVS_PICTURE_I);
    context.picture_structure = 0U;
    context.progressive_frame = 0U;
    context.macroblock_width = 1U;
    context.macroblock_height = 2U;
    context.macroblock_index = 1U;
    write_ue(&writer, 1U); /* P_16x16 syntax for the predicted second field. */
    write_se(&writer, 0); write_se(&writer, 0);
    write_ue(&writer, 0U);
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.type == CAVS_MB_P_16X16 && macroblock.is_intra == 0U);
}

/* GB/T 20090.16-2016 Table 42 and 7.5.9: all CBP codes and no AEC-only bit. */
static void test_basic_cbp_table_and_weighting_boundary(void) {
    static const uint8_t expected[64] = {
        0, 15, 63, 31, 16, 32, 47, 13, 14, 11, 12, 5, 10, 7, 48, 3,
        2, 8, 4, 1, 61, 55, 59, 62, 29, 27, 23, 19, 30, 28, 9, 6,
        60, 21, 44, 26, 51, 35, 18, 20, 24, 53, 17, 37, 39, 45, 58, 43,
        42, 46, 36, 33, 34, 40, 52, 49, 50, 56, 25, 22, 54, 57, 41, 38
    };
    unsigned code;
    for (code = 0U; code < 64U; ++code) {
        basic_writer writer;
        cavs_broadcast_basic_decoder decoder;
        cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_P);
        cavs_macroblock macroblock;
        unsigned block;
        memset(&writer, 0, sizeof(writer));
        context.slice_weighting_flag = 1U;
        context.mb_weighting_flag = 1U;
        write_ue(&writer, 1U); /* Table 55 P_16x16. */
        write_se(&writer, 0); write_se(&writer, 0);
        write_ue(&writer, code);
        for (block = 0U; block < 6U; ++block) {
            if ((expected[code] & (UINT8_C(1) << block)) != 0U)
                write_single_coefficient(&writer, block >= 4U ?
                    CAVS_BASIC_CHROMA : CAVS_BASIC_INTER_LUMA);
        }
        TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data,
                                             writer.bit_pos, 0U) == CAVS_OK);
        TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                       &decoder, &context, &macroblock) == CAVS_OK);
        TEST_CHECK(macroblock.coded_block_pattern == expected[code]);
        TEST_CHECK(macroblock.weighting_prediction == 0U);
        TEST_CHECK(macroblock.end_bit_offset == writer.bit_pos);
    }
}

/* GB/T 20090.16-2016 Table 42 intra column via section 9.2 embedded CBP. */
static void test_basic_intra_cbp_table(void) {
    static const uint8_t expected[64] = {
        63, 15, 31, 47, 0, 14, 13, 11, 7, 5, 10, 8, 12, 61, 4, 55,
        1, 2, 59, 3, 62, 9, 6, 29, 45, 51, 23, 39, 27, 46, 53, 30,
        43, 37, 60, 16, 21, 28, 19, 35, 42, 26, 44, 32, 58, 24, 20, 17,
        18, 48, 22, 33, 25, 49, 40, 36, 34, 50, 52, 54, 41, 56, 38, 57
    };
    unsigned code;
    for (code = 0U; code < 64U; ++code) {
        basic_writer writer;
        cavs_broadcast_basic_decoder decoder;
        cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_I);
        cavs_macroblock macroblock;
        unsigned block;
        memset(&writer, 0, sizeof(writer));
        context.picture_structure = 0U;
        context.progressive_frame = 0U;
        context.macroblock_width = 1U;
        context.macroblock_height = 2U;
        context.macroblock_index = 1U;
        write_ue(&writer, 5U + code); /* explicit I_8x8 plus embedded CBP. */
        write_intra_header(&writer);
        for (block = 0U; block < 6U; ++block) {
            if ((expected[code] & (UINT8_C(1) << block)) != 0U)
                write_single_coefficient(&writer, block >= 4U ?
                    CAVS_BASIC_CHROMA : CAVS_BASIC_INTRA_LUMA);
        }
        TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data,
                                             writer.bit_pos, 0U) == CAVS_OK);
        TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                       &decoder, &context, &macroblock) == CAVS_OK);
        TEST_CHECK(macroblock.type == CAVS_MB_I_8X8 &&
                   macroblock.coded_block_pattern == expected[code] &&
                   macroblock.end_bit_offset == writer.bit_pos);
    }
}

/* GB/T 20090.16-2016 Table 56/57: Direct and symmetric MVD consumption. */
static void test_basic_b_direct_and_symmetric(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_B);
    cavs_macroblock macroblock;
    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 1U); /* B_Direct_16x16 has no coded MVD. */
    write_ue(&writer, 0U);
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.type == CAVS_MB_B_DIRECT &&
               macroblock.partition[0].motion[CAVS_PRED_FORWARD].valid == 0U &&
               macroblock.end_bit_offset == writer.bit_pos);

    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 4U); /* B_Sym_16x16: only forward MVD is coded. */
    write_se(&writer, -3); write_se(&writer, 4);
    write_ue(&writer, 0U);
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.partition[0].motion[CAVS_PRED_FORWARD].valid != 0U &&
               macroblock.partition[0].motion[CAVS_PRED_FORWARD].x == -3 &&
               macroblock.partition[0].motion[CAVS_PRED_FORWARD].y == 4 &&
               macroblock.partition[0].motion[CAVS_PRED_BACKWARD].valid == 0U &&
               macroblock.end_bit_offset == writer.bit_pos);
}

/* GB/T 20090.16-2016 7.5.7-7.5.8: B references precede all B MVDs. */
static void test_basic_b_reference_order(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_B);
    cavs_macroblock macroblock;
    memset(&writer, 0, sizeof(writer));
    context.picture_reference_flag = 0U;
    write_ue(&writer, 9U); /* Table 56 B_Fwd_Bck_16x8. */
    write_bits(&writer, 1U, 1U); /* all forward references first */
    write_bits(&writer, 0U, 1U); /* then all backward references */
    write_se(&writer, 7);  write_se(&writer, -8); /* forward MVD */
    write_se(&writer, -9); write_se(&writer, 10); /* backward MVD */
    write_ue(&writer, 0U); /* Table 42 inter CodeNum 0 -> MbCBP 0. */
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.partition_count == 2U &&
               macroblock.partition[0].direction == CAVS_PRED_FORWARD &&
               macroblock.partition[1].direction == CAVS_PRED_BACKWARD);
    TEST_CHECK(macroblock.partition[0].motion[CAVS_PRED_FORWARD].reference_index == 1 &&
               macroblock.partition[1].motion[CAVS_PRED_BACKWARD].reference_index == 0);
    TEST_CHECK(macroblock.partition[0].motion[CAVS_PRED_FORWARD].x == 7 &&
               macroblock.partition[0].motion[CAVS_PRED_FORWARD].y == -8 &&
               macroblock.partition[1].motion[CAVS_PRED_BACKWARD].x == -9 &&
               macroblock.partition[1].motion[CAVS_PRED_BACKWARD].y == 10);
}

/* GB/T 20090.16-2016 9.8: QP delta is present only for nonzero CBP. */
static void test_basic_qp_delta_presence(void) {
    basic_writer writer;
    cavs_broadcast_basic_decoder decoder;
    cavs_broadcast_basic_mb_context context = default_context(CAVS_PICTURE_P);
    cavs_macroblock macroblock;
    memset(&writer, 0, sizeof(writer));
    context.fixed_qp = 0U;
    context.previous_qp = 20U;
    write_ue(&writer, 1U);
    write_se(&writer, 0); write_se(&writer, 0);
    write_ue(&writer, 19U); /* MbCBP 1, so mb_qp_delta follows. */
    write_se(&writer, -2);
    write_single_coefficient(&writer, CAVS_BASIC_INTER_LUMA);
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.qp_delta == -2 && macroblock.qp == 18U);

    memset(&writer, 0, sizeof(writer));
    write_ue(&writer, 1U);
    write_se(&writer, 0); write_se(&writer, 0);
    write_ue(&writer, 0U); /* MbCBP 0: no mb_qp_delta is present. */
    TEST_CHECK(cavs_broadcast_basic_init(&decoder, writer.data, writer.bit_pos, 0U) ==
               CAVS_OK);
    TEST_CHECK(cavs_decode_broadcast_basic_macroblock(
                   &decoder, &context, &macroblock) == CAVS_OK);
    TEST_CHECK(macroblock.qp_delta == 0 && macroblock.qp == 20U &&
               macroblock.end_bit_offset == writer.bit_pos);
}

void test_broadcast_basic_macroblock(void) {
    test_implicit_i_and_coefficients();
    test_p_motion_and_cbp();
    test_b_8x8_and_skip_run();
    test_truncation_atomic();
    test_basic_field_type_and_motion_order();
    test_basic_cbp_table_and_weighting_boundary();
    test_basic_intra_cbp_table();
    test_basic_b_direct_and_symmetric();
    test_basic_b_reference_order();
    test_basic_qp_delta_presence();
}
