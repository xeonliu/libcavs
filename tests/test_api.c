/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Public decoder API state-machine tests.
 */
#include <cavs/cavs.h>
#include <assert.h>

/* Supplies one half of an intentionally invalid allocator pair. */
static void dummy_free(void *opaque, void *ptr) { (void)opaque; (void)ptr; }

/* Verifies argument validation, drain behavior, and reset behavior. */
int main(void) {
    static const uint8_t valid_prefix[] = { 0, 0, 1, 0xb0 };
    static const uint8_t invalid_prefix[] = { 0, 0, 2, 0xb0 };
    cavs_decoder *decoder = NULL;
    cavs_decoder_config bad_config = { 0 };
    cavs_packet packet = { valid_prefix, sizeof(valid_prefix), 1, 2, NULL };
    cavs_event event;

    bad_config.alloc = NULL;
    bad_config.free = dummy_free;
    assert(cavs_decoder_create(&bad_config, &decoder) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_decoder_create(NULL, &decoder) == CAVS_OK);
    assert(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    assert(cavs_decoder_send_nal(decoder, &packet) == CAVS_ERR_UNSUPPORTED_PROFILE);
    packet.data = invalid_prefix;
    assert(cavs_decoder_send_nal(decoder, &packet) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(cavs_decoder_flush(decoder) == CAVS_OK);
    assert(cavs_decoder_receive_event(decoder, &event) == CAVS_OK);
    assert(event.type == CAVS_EVENT_END);
    assert(cavs_decoder_receive_event(decoder, &event) == CAVS_EOF);
    assert(cavs_decoder_send_nal(decoder, &packet) == CAVS_ERR_INVALID_STATE);
    assert(cavs_decoder_reset(decoder) == CAVS_OK);
    assert(cavs_decoder_receive_event(decoder, &event) == CAVS_AGAIN);
    cavs_decoder_destroy(decoder);
    return 0;
}
