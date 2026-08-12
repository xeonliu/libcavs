/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "codec/loop_filter.h"
#include "dsp/loop_filter.h"
#include "test.h"
#include <string.h>

static void init_inter_macroblock(cavs_macroblock *macroblock,
                                  int32_t motion_x) {
    memset(macroblock, 0, sizeof(*macroblock));
    macroblock->partition_count = 1U;
    macroblock->partition[0].width = 16U;
    macroblock->partition[0].height = 16U;
    macroblock->partition[0].motion[CAVS_PRED_FORWARD].valid = 1U;
    macroblock->partition[0].motion[CAVS_PRED_FORWARD].reference_index = 0;
    macroblock->partition[0].motion[CAVS_PRED_FORWARD].x = motion_x;
}

void test_loop_filter(void) {
    cavs_macroblock p;
    cavs_macroblock q;
    uint8_t strength;
    uint8_t samples[8] = { 0U, 10U, 10U, 10U, 14U, 14U, 14U, 0U };
    uint8_t unchanged[8];
    init_inter_macroblock(&p, 0);
    init_inter_macroblock(&q, 0);
    TEST_CHECK(cavs_loop_filter_boundary_strength(
                   &p, 0U, &q, 0U, CAVS_PICTURE_P, 4U,
                   &strength) == CAVS_OK);
    TEST_CHECK(strength == 0U);
    q.partition[0].motion[CAVS_PRED_FORWARD].x = 4;
    TEST_CHECK(cavs_loop_filter_boundary_strength(
                   &p, 0U, &q, 0U, CAVS_PICTURE_P, 4U,
                   &strength) == CAVS_OK);
    TEST_CHECK(strength == 1U);
    q.is_intra = 1U;
    TEST_CHECK(cavs_loop_filter_boundary_strength(
                   &p, 0U, &q, 0U, CAVS_PICTURE_P, 4U,
                   &strength) == CAVS_OK);
    TEST_CHECK(strength == 2U);

    cavs_dsp_loop_filter_segment_c(
        &samples[4], 1, 1, 1U, 2U, 10U, 5U, 2U, 0);
    TEST_CHECK(samples[3] == 11U && samples[4] == 13U);
    memcpy(unchanged, samples, sizeof(samples));
    cavs_dsp_loop_filter_segment_c(
        &samples[4], 1, 1, 1U, 2U, 2U, 1U, 0U, 0);
    TEST_CHECK(memcmp(samples, unchanged, sizeof(samples)) == 0);
}
