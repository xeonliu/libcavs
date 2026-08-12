/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Public decoder API state-machine tests.
 */
#include <cavs/cavs.h>
#include "test.h"
#include <string.h>

/* Supplies one half of an intentionally invalid allocator pair. */
static void dummy_free(void *opaque, void *ptr) { (void)opaque; (void)ptr; }

/* Appends a fixed-width value to an Annex-B sequence-header test packet. */
static void append_bits(uint8_t *data, size_t *position, uint32_t value, unsigned width) {
    unsigned index;
    for (index = 0; index < width; ++index) {
        unsigned shift = width - index - 1U;
        data[*position / 8U] |= (uint8_t)(((value >> shift) & 1U) <<
                                         (7U - (*position % 8U)));
        ++*position;
    }
}

/* Creates one valid baseline sequence unit for decoder integration tests. */
static void make_sequence_nal(uint8_t data[18]) {
    size_t position = 32U;
    memset(data, 0, 18U);
    data[2] = 1U;
    data[3] = UINT8_C(0xb0);
    append_bits(data, &position, UINT32_C(0x20), 8U);
    append_bits(data, &position, UINT32_C(0x22), 8U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 720U, 14U);
    append_bits(data, &position, 576U, 14U);
    append_bits(data, &position, 1U, 2U);
    append_bits(data, &position, 1U, 3U);
    append_bits(data, &position, 3U, 4U);
    append_bits(data, &position, 3U, 4U);
    append_bits(data, &position, 100U, 18U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 0U, 12U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 2U, 18U);
    append_bits(data, &position, 0U, 3U);
    TEST_CHECK(position == 18U * 8U);
}

/* Creates a progressive baseline I-picture header for decoder dispatch. */
static void make_i_picture_nal(uint8_t data[10]) {
    size_t position = 32U;
    memset(data, 0, 10U);
    data[2] = 1U;
    data[3] = UINT8_C(0xb3);
    append_bits(data, &position, UINT32_C(0xffff), 16U);
    append_bits(data, &position, 0U, 1U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 2U, 8U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 0U, 1U);
    append_bits(data, &position, 0U, 1U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 20U, 6U);
    append_bits(data, &position, 0U, 4U);
    append_bits(data, &position, 1U, 1U);
    TEST_CHECK(position == 74U);
}

/* Creates a progressive baseline P-picture header for decoder dispatch. */
static void make_pb_picture_nal(uint8_t data[10]) {
    size_t position = 32U;
    memset(data, 0, 10U);
    data[2] = 1U;
    data[3] = UINT8_C(0xb6);
    append_bits(data, &position, 40U, 16U);
    append_bits(data, &position, 1U, 2U);
    append_bits(data, &position, 3U, 8U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 0U, 1U);
    append_bits(data, &position, 0U, 1U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 18U, 6U);
    append_bits(data, &position, 0U, 1U);
    append_bits(data, &position, 0U, 1U);
    append_bits(data, &position, 0U, 3U);
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 1U, 1U);
    TEST_CHECK(position == 76U);
}

/* Creates one baseline I-picture slice header before its macroblock data. */
static void make_slice_nal(uint8_t data[5]) {
    size_t position = 32U;
    memset(data, 0, 5U);
    data[2] = 1U;
    data[3] = 0U;
    append_bits(data, &position, 1U, 1U);
    append_bits(data, &position, 20U, 6U);
    TEST_CHECK(position == 39U);
}

/* Verifies argument validation, drain behavior, and reset behavior. */
int main(void) {
    static const uint8_t picture_prefix[] = { 0, 0, 1, 0xb3 };
    static const uint8_t invalid_prefix[] = { 0, 0, 2, 0xb0 };
    static const uint8_t user_data[] = { 0, 0, 1, 0xb2, 'a', 'v', 's' };
    static const uint8_t extension[] = { 0, 0, 1, 0xb5, 0x91, 0x27 };
    uint8_t sequence_nal[18];
    uint8_t i_picture_nal[10];
    uint8_t pb_picture_nal[10];
    uint8_t slice_nal[5];
    cavs_decoder *decoder = NULL;
    cavs_decoder_config bad_config = { 0 };
    cavs_packet packet = { picture_prefix, sizeof(picture_prefix), 1, 2, NULL };
    cavs_event event;

    bad_config.alloc = NULL;
    bad_config.free = dummy_free;
    TEST_CHECK(cavs_decoder_create(&bad_config, &decoder) == CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_decoder_create(NULL, &decoder) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_ERR_INVALID_STATE);
    make_sequence_nal(sequence_nal);
    packet.data = sequence_nal;
    packet.size = sizeof(sequence_nal);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_OK);
    TEST_CHECK(event.type == CAVS_EVENT_SEQUENCE);
    TEST_CHECK(event.sequence.profile_id == UINT8_C(0x20));
    TEST_CHECK(event.sequence.display_width == 720U && event.sequence.display_height == 576U);
    make_i_picture_nal(i_picture_nal);
    packet.data = i_picture_nal;
    packet.size = sizeof(i_picture_nal);
    packet.pts = 101;
    packet.dts = 99;
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    make_slice_nal(slice_nal);
    packet.data = slice_nal;
    packet.size = sizeof(slice_nal);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    make_pb_picture_nal(pb_picture_nal);
    packet.data = pb_picture_nal;
    packet.size = sizeof(pb_picture_nal);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    packet.data = sequence_nal;
    packet.size = sizeof(sequence_nal);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    packet.data = user_data;
    packet.size = sizeof(user_data);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_OK);
    packet.data = extension;
    packet.size = sizeof(extension);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_AGAIN);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_OK);
    TEST_CHECK(event.type == CAVS_EVENT_METADATA && event.size == 3U);
    TEST_CHECK(memcmp(event.data, "avs", 3U) == 0);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_OK);
    TEST_CHECK(event.type == CAVS_EVENT_RAW_EXTENSION && event.size == 2U);
    TEST_CHECK(event.data[0] == UINT8_C(0x91) && event.data[1] == UINT8_C(0x27));
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    packet.data = invalid_prefix;
    packet.size = sizeof(invalid_prefix);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(cavs_decoder_flush(decoder) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_OK);
    TEST_CHECK(event.type == CAVS_EVENT_END);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_EOF);
    TEST_CHECK(cavs_decoder_send_nal(decoder, &packet) == CAVS_ERR_INVALID_STATE);
    TEST_CHECK(cavs_decoder_reset(decoder) == CAVS_OK);
    TEST_CHECK(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    cavs_decoder_destroy(decoder);
    return 0;
}
