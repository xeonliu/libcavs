/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Public-API fuzz entry point for complete synthesized Annex-B units.
 */
#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef CAVS_AFL_STANDALONE
#include <stdio.h>
#endif

/* Routes arbitrary bytes through framing, syntax dispatch, drain, and cleanup. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    cavs_decoder *decoder = NULL;
    cavs_packet packet;
    cavs_event event;
    uint8_t *nal;
    size_t nal_size;
    if (size == 0U || size > SIZE_MAX - 3U) return 0;
    nal_size = size + 3U;
    nal = (uint8_t *)malloc(nal_size);
    if (nal == NULL) return 0;
    nal[0] = 0U;
    nal[1] = 0U;
    nal[2] = 1U;
    memcpy(nal + 3U, data, size);
    if (cavs_decoder_create(NULL, &decoder) == CAVS_OK) {
        packet.data = nal;
        packet.size = nal_size;
        packet.pts = 0;
        packet.dts = 0;
        packet.opaque = NULL;
        (void)cavs_decoder_send_nal(decoder, &packet);
        while (cavs_decoder_receive_event(decoder, &event) == CAVS_OK) {
        }
        (void)cavs_decoder_flush(decoder);
        while (cavs_decoder_receive_event(decoder, &event) == CAVS_OK) {
        }
        cavs_decoder_destroy(decoder);
    }
    free(nal);
    return 0;
}

#ifdef CAVS_AFL_STANDALONE
/* Reads one AFL test case from standard input and invokes the shared harness. */
int main(void) {
    const size_t capacity = 16U * 1024U * 1024U;
    uint8_t *data = (uint8_t *)malloc(capacity);
    size_t size;
    int result;
    if (data == NULL) return 1;
    size = fread(data, 1U, capacity, stdin);
    result = LLVMFuzzerTestOneInput(data, size);
    free(data);
    return result;
}
#endif
