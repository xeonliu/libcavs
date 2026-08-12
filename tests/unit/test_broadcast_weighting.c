/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.4.6, 7.4.11 and 9.3 weighted-prediction vectors.
 * Values are independently calculated from the normative formula.
 */
#include "codec/broadcast_weighting.h"
#include "test.h"
#include <string.h>

static void test_weight_sample_formula_and_clip(void) {
    uint8_t value;
    TEST_CHECK(cavs_broadcast_weight_sample(100U, 64U, -3, &value) == CAVS_OK);
    TEST_CHECK(value == 197U);
    TEST_CHECK(cavs_broadcast_weight_sample(1U, 32U, 0, &value) == CAVS_OK);
    TEST_CHECK(value == 1U);
    TEST_CHECK(cavs_broadcast_weight_sample(255U, 0U, -128, &value) == CAVS_OK);
    TEST_CHECK(value == 0U);
    TEST_CHECK(cavs_broadcast_weight_sample(255U, 255U, 127, &value) == CAVS_OK);
    TEST_CHECK(value == 255U);
    TEST_CHECK(cavs_broadcast_weight_sample(0U, 32U, -1, NULL) ==
               CAVS_ERR_INVALID_ARGUMENT);
}

static void test_weight_block(void) {
    uint8_t samples[12] = { 0U, 10U, 20U, 30U, 40U, 50U,
                            60U, 70U, 80U, 90U, 100U, 110U };
    static const uint8_t expected[12] = { 0U, 10U, 20U, 30U, 40U, 50U,
                                          60U, 141U, 161U, 90U, 100U, 110U };
    TEST_CHECK(cavs_broadcast_weight_block(samples, 6U, 1U, 1U, 2U, 1U,
                                            64U, 1) == CAVS_OK);
    TEST_CHECK(memcmp(samples, expected, sizeof(samples)) == 0);
    TEST_CHECK(cavs_broadcast_weight_block(samples, 6U, 5U, 0U, 2U, 1U,
                                            32U, 0) == CAVS_ERR_INVALID_ARGUMENT);
}

static void test_parameter_mapping(void) {
    uint8_t index;
    TEST_CHECK(cavs_broadcast_weight_parameter_index(
                   CAVS_PICTURE_P, CAVS_PRED_FORWARD, 1U, &index) == CAVS_OK);
    TEST_CHECK(index == 1U);
    TEST_CHECK(cavs_broadcast_weight_parameter_index(
                   CAVS_PICTURE_B, CAVS_PRED_FORWARD, 1U, &index) == CAVS_OK);
    TEST_CHECK(index == 2U);
    TEST_CHECK(cavs_broadcast_weight_parameter_index(
                   CAVS_PICTURE_B, CAVS_PRED_BACKWARD, 1U, &index) == CAVS_OK);
    TEST_CHECK(index == 3U);
    TEST_CHECK(cavs_broadcast_weight_parameter_index(
                   CAVS_PICTURE_B, CAVS_PRED_BACKWARD, 4U, &index) ==
               CAVS_ERR_CORRUPT_BITSTREAM);
}

static void test_flag_combinations(void) {
    uint8_t enabled;
    TEST_CHECK(cavs_broadcast_should_weight(0U, 0U, 0U, 0U, &enabled) == CAVS_OK);
    TEST_CHECK(enabled == 0U);
    TEST_CHECK(cavs_broadcast_should_weight(0U, 1U, 1U, 0U, &enabled) == CAVS_OK);
    TEST_CHECK(enabled == 0U);
    TEST_CHECK(cavs_broadcast_should_weight(1U, 0U, 0U, 0U, &enabled) == CAVS_OK);
    TEST_CHECK(enabled == 1U);
    TEST_CHECK(cavs_broadcast_should_weight(1U, 1U, 0U, 0U, &enabled) == CAVS_OK);
    TEST_CHECK(enabled == 0U);
    TEST_CHECK(cavs_broadcast_should_weight(1U, 1U, 1U, 0U, &enabled) == CAVS_OK);
    TEST_CHECK(enabled == 1U);
    TEST_CHECK(cavs_broadcast_should_weight(1U, 0U, 1U, 1U, &enabled) == CAVS_OK);
    TEST_CHECK(enabled == 0U);
}

static void test_weight_then_average_sample(void) {
    uint8_t forward;
    uint8_t backward;
    /* Part 16 9.3: direction-wise weighting precedes B-picture averaging. */
    TEST_CHECK(cavs_broadcast_weight_sample(40U, 64U, 0, &forward) == CAVS_OK);
    TEST_CHECK(cavs_broadcast_weight_sample(160U, 32U, -16, &backward) ==
               CAVS_OK);
    TEST_CHECK(forward == 80U && backward == 144U);
    TEST_CHECK((uint8_t)(((unsigned)forward + backward + 1U) >> 1U) ==
               112U);
}

void test_broadcast_weighting(void) {
    test_weight_sample_formula_and_clip();
    test_weight_block();
    test_parameter_mapping();
    test_flag_combinations();
    test_weight_then_average_sample();
}
