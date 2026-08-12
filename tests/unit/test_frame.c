/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "codec/macroblock.h"
#include "frame.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

typedef struct allocation_state {
    unsigned outstanding;
} allocation_state;

static void *counting_alloc(void *opaque, size_t size) {
    allocation_state *state = (allocation_state *)opaque;
    void *memory = malloc(size);
    if (memory != NULL) ++state->outstanding;
    return memory;
}

static void counting_free(void *opaque, void *memory) {
    allocation_state *state = (allocation_state *)opaque;
    if (memory != NULL) {
        TEST_CHECK(state->outstanding != 0U);
        --state->outstanding;
        free(memory);
    }
}

void test_frame(void) {
    allocation_state allocations = { 0U };
    cavs_decoder_config config;
    cavs_sequence_info sequence;
    cavs_frame_parameters parameters;
    cavs_frame *frame = NULL;
    cavs_frame *reference;
    cavs_picture *picture;
    memset(&config, 0, sizeof(config));
    memset(&sequence, 0, sizeof(sequence));
    memset(&parameters, 0, sizeof(parameters));
    config.alloc = counting_alloc;
    config.free = counting_free;
    config.allocator_opaque = &allocations;
    sequence.progressive_sequence = 1U;
    sequence.display_width = 17U;
    sequence.display_height = 33U;
    sequence.format = CAVS_YUV420P8;
    parameters.sequence = &sequence;
    parameters.picture_type = CAVS_PICTURE_I;
    parameters.picture_structure = 1U;
    parameters.pts = 7;
    parameters.dts = 5;
    parameters.macroblock_size = sizeof(cavs_macroblock);

    TEST_CHECK(cavs_frame_allocate(&config, &parameters, &frame) == CAVS_OK);
    TEST_CHECK(allocations.outstanding == 3U);
    TEST_CHECK(frame->coded_width == 32U && frame->coded_height == 48U);
    TEST_CHECK(cavs_frame_test_reference_count(frame) == 1U);
    picture = cavs_frame_picture(frame);
    TEST_CHECK(picture != NULL && cavs_picture_frame(picture) == frame);
    reference = cavs_frame_ref(frame);
    TEST_CHECK(reference == frame && cavs_frame_test_reference_count(frame) == 2U);
    cavs_frame_retain_picture(NULL, picture);
    TEST_CHECK(cavs_frame_test_reference_count(frame) == 3U);
    cavs_frame_release_picture(NULL, picture);
    TEST_CHECK(cavs_frame_test_reference_count(frame) == 2U);
    cavs_frame_unref(&reference);
    TEST_CHECK(reference == NULL && cavs_frame_test_reference_count(frame) == 1U);
    cavs_frame_unref(&frame);
    TEST_CHECK(frame == NULL && allocations.outstanding == 0U);
}
