/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 6.2-6.3, 7.4-7.5, 8.2 and 9.3 pipeline vectors.
 * The payloads below are independently generated Basic-entropy samples; they
 * do not copy code, control flow, or output from /tmp/libcavs-reference.
 */
#include "picture_pipeline.h"
#include "codec/broadcast_basic_macroblock.h"
#include "codec/coefficients.h"
#include "codec/macroblock.h"
#include "test.h"
#include <cavs/cavs.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct bit_writer {
    uint8_t data[128];
    size_t bits;
} bit_writer;

static void writer_bit(bit_writer *writer, uint8_t value) {
    if (value != 0U)
        writer->data[writer->bits / 8U] |=
            (uint8_t)(UINT8_C(1) << (7U - (writer->bits % 8U)));
    ++writer->bits;
}

/* GB/T 20090.16-2016 8.2: write one order-zero ue(v) code. */
static void writer_ue(bit_writer *writer, uint32_t value) {
    uint32_t code = value + 1U;
    unsigned width = 0U;
    uint32_t copy = code;
    while (copy != 0U) {
        ++width;
        copy >>= 1U;
    }
    while (width > 1U) {
        writer_bit(writer, 0U);
        --width;
    }
    for (width = 0U; code != 0U; ++width) code >>= 1U;
    while (width != 0U) {
        --width;
        writer_bit(writer, (uint8_t)(((value + 1U) >> width) & 1U));
    }
}

/* GB/T 20090.2-2013 8.2: write the bounded order-k VLC form used by Annex D. */
static void writer_ue_k(bit_writer *writer, uint32_t value, unsigned order) {
    unsigned zeros = 0U;
    for (;;) {
        unsigned width = zeros + order;
        uint64_t base = (UINT64_C(1) << width) -
            (UINT64_C(1) << order);
        uint64_t span = UINT64_C(1) << width;
        if ((uint64_t)value < base + span) {
            unsigned index;
            for (index = 0U; index < zeros; ++index) writer_bit(writer, 0U);
            writer_bit(writer, 1U);
            value = (uint32_t)((uint64_t)value - base);
            for (index = width; index != 0U; --index)
                writer_bit(writer, (uint8_t)((value >> (index - 1U)) & 1U));
            return;
        }
        ++zeros;
    }
}

/* First-entry coefficient vectors are derived from GB/T 20090.2-2013 Annex D. */
static void writer_single_coefficient(bit_writer *writer,
                                      cavs_basic_block_kind kind) {
    writer_ue_k(writer, 0U, kind == CAVS_BASIC_INTER_LUMA ? 3U : 2U);
    writer_ue_k(writer, kind == CAVS_BASIC_INTRA_LUMA ? 8U :
                       (kind == CAVS_BASIC_INTER_LUMA ? 2U : 0U),
                kind == CAVS_BASIC_CHROMA ? 0U : 2U);
}

static void writer_full_intra_blocks(bit_writer *writer) {
    unsigned index;
    for (index = 0U; index < CAVS_MB_8X8_BLOCKS; ++index)
        writer_single_coefficient(writer, index < 4U ?
            CAVS_BASIC_INTRA_LUMA : CAVS_BASIC_CHROMA);
}

/* GB/T 20090.16-2016 7.5/9.2/9.7: two implicit-I macroblocks. */
static size_t make_intra_slice(uint8_t data[128]) {
    bit_writer writer;
    unsigned macroblock;
    memset(&writer, 0, sizeof(writer));
    for (macroblock = 0U; macroblock < 2U; ++macroblock) {
        unsigned block;
        for (block = 0U; block < 4U; ++block) writer_bit(&writer, 1U);
        writer_ue(&writer, 0U); /* intra chroma mode */
        writer_full_intra_blocks(&writer); /* implicit Table 42 CBP 63 */
    }
    memcpy(data, writer.data, 128U);
    return writer.bits;
}

/* GB/T 20090.16-2016 7.4/9.2: second-field Basic skip-run slice. */
static size_t make_second_field_slice(uint8_t data[128]) {
    bit_writer writer;
    memset(&writer, 0, sizeof(writer));
    writer_bit(&writer, 0U); /* slice_weighting_flag */
    writer_ue(&writer, 2U); /* mb_skip_run covers the two-row slice */
    memcpy(data, writer.data, 128U);
    return writer.bits;
}

static void *test_alloc(void *opaque, size_t size) {
    (void)opaque;
    return malloc(size);
}

static void test_free(void *opaque, void *pointer) {
    (void)opaque;
    free(pointer);
}

static void run_field_order_case(uint8_t top_field_first) {
    cavs_decoder_config config;
    cavs_sequence_info sequence;
    cavs_i_picture_header picture;
    cavs_packet packet;
    cavs_picture_pipeline *pipeline = NULL;
    cavs_frame *frame = NULL;
    uint8_t intra[128];
    uint8_t second_field[128];
    size_t intra_bits;
    size_t second_field_bits;
    memset(&config, 0, sizeof(config));
    config.alloc = test_alloc;
    config.free = test_free;
    memset(&sequence, 0, sizeof(sequence));
    sequence.profile_id = UINT8_C(0x48);
    sequence.level_id = UINT8_C(0x2a);
    sequence.display_width = 32U;
    sequence.display_height = 64U;
    sequence.format = CAVS_YUV420P8;
    sequence.progressive_sequence = 0U;
    memset(&picture, 0, sizeof(picture));
    picture.picture_structure = 0U;
    picture.progressive_frame = 0U;
    picture.top_field_first = top_field_first;
    picture.fixed_picture_qp = 1U;
    picture.picture_qp = 20U;
    picture.skip_mode_flag = 1U;
    picture.loop_filter_disable = 1U;
    picture.picture_distance = 0U;
    picture.advanced_entropy_enabled = 0U;
    memset(&packet, 0, sizeof(packet));
    intra_bits = make_intra_slice(intra);
    second_field_bits = make_second_field_slice(second_field);
    {
        cavs_broadcast_basic_decoder basic;
        cavs_broadcast_basic_mb_context basic_context;
        cavs_macroblock basic_mb;
        memset(&basic_context, 0, sizeof(basic_context));
        basic_context.profile_id = UINT8_C(0x48);
        basic_context.format = CAVS_YUV420P8;
        basic_context.picture_type = CAVS_PICTURE_I;
        basic_context.progressive_frame = 0U;
        basic_context.picture_structure = 0U;
        basic_context.fixed_qp = 1U;
        basic_context.previous_qp = 20U;
        basic_context.macroblock_width = 2U;
        basic_context.macroblock_height = 4U;
        TEST_CHECK(cavs_broadcast_basic_init(
                       &basic, intra, intra_bits, 0U) == CAVS_OK);
        basic_context.macroblock_index = 0U;
        {
            cavs_result result = cavs_decode_broadcast_basic_macroblock(
                &basic, &basic_context, &basic_mb);
            TEST_CHECK(result == CAVS_OK);
        }
        basic_context.macroblock_index = 1U;
        basic_context.left = &basic_mb;
        {
            cavs_result result = cavs_decode_broadcast_basic_macroblock(
                &basic, &basic_context, &basic_mb);
            TEST_CHECK(result == CAVS_OK);
        }
    }

    TEST_CHECK(cavs_picture_pipeline_create(&config, &pipeline) == CAVS_OK);
    cavs_picture_pipeline_set_sequence(pipeline, &sequence);
    TEST_CHECK(cavs_picture_pipeline_begin_i(pipeline, &picture, &packet) ==
               CAVS_OK);

    /* GB/T 20090.16-2016 7.4/9.3: two slices complete the first field. */
    TEST_CHECK(cavs_picture_pipeline_decode_slice(
                   pipeline, 0U, intra, (intra_bits + 7U) / 8U) == CAVS_OK);
    {
        cavs_result result = cavs_picture_pipeline_decode_slice(
            pipeline, 1U, intra, (intra_bits + 7U) / 8U);
        TEST_CHECK(result == CAVS_OK);
    }
    /* The next row commits the first field and starts the second field. */
    TEST_CHECK(cavs_picture_pipeline_decode_slice(
                   pipeline, 2U, second_field,
                   (second_field_bits + 7U) / 8U) == CAVS_OK);
    TEST_CHECK(cavs_picture_pipeline_decode_slice(
                   pipeline, 3U, second_field,
                   (second_field_bits + 7U) / 8U) == CAVS_OK);
    TEST_CHECK(cavs_picture_pipeline_finish_picture(pipeline) == CAVS_OK);
    TEST_CHECK(cavs_picture_pipeline_flush(pipeline) == CAVS_OK);
    TEST_CHECK(cavs_picture_pipeline_pop_frame(pipeline, &frame) == CAVS_OK);
    TEST_CHECK(frame != NULL);
    cavs_frame_unref(&frame);
    TEST_CHECK(cavs_picture_pipeline_pop_frame(pipeline, &frame) == CAVS_AGAIN);

    /* GB/T 20090.16-2016 7.4/9.3: no slice follows a committed picture. */
    TEST_CHECK(cavs_picture_pipeline_decode_slice(
                   pipeline, 0U, intra, (intra_bits + 7U) / 8U) ==
               CAVS_ERR_INVALID_STATE);
    cavs_picture_pipeline_destroy(pipeline);
}

void test_broadcast_pipeline(void) {
    /* GB/T 20090.16-2016 6.2/6.3: both display-field orders are normative. */
    run_field_order_case(1U);
    run_field_order_case(0U);
}
