/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013 chroma interpolation tests.
 */
#include "motion.h"
#include "test.h"
#include <stdint.h>
#include <string.h>

static void fill_motion_plane(uint8_t *plane, size_t width, size_t height,
                              size_t stride) {
    size_t x;
    size_t y;
    memset(plane, 0xcc, height * stride);
    for (y = 0U; y < height; ++y)
        for (x = 0U; x < width; ++x)
            plane[y * stride + x] = (uint8_t)(y * 16U + x);
}

static void fill_luma_pattern(uint8_t *plane, size_t width, size_t height,
                              size_t stride) {
    size_t x;
    size_t y;
    memset(plane, 0xcc, height * stride);
    for (y = 0U; y < height; ++y)
        for (x = 0U; x < width; ++x)
            plane[y * stride + x] =
                (uint8_t)((x * 37U + y * 53U + x * y * 11U + 17U) & 255U);
}

static cavs_motion_candidate motion_candidate(
    int32_t x, int32_t y, uint16_t distance, int8_t reference_index) {
    cavs_motion_candidate candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.vector.x = x;
    candidate.vector.y = y;
    candidate.block_distance = distance;
    candidate.reference_index = reference_index;
    candidate.available = 1U;
    candidate.same_direction = 1U;
    return candidate;
}

static void clear_motion_candidates(
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT]) {
    unsigned index;
    memset(candidates, 0, sizeof(*candidates) * CAVS_MOTION_NEIGHBOR_COUNT);
    for (index = 0U; index < CAVS_MOTION_NEIGHBOR_COUNT; ++index) {
        candidates[index].block_distance = 1U;
        candidates[index].reference_index = -1;
    }
}

static void test_motion_prediction_normalization(void) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector prediction;
    clear_motion_candidates(candidates);
    candidates[CAVS_MOTION_NEIGHBOR_A] = motion_candidate(7, -9, 3U, 0);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 3U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 7 && prediction.y == -9);

    clear_motion_candidates(candidates);
    candidates[CAVS_MOTION_NEIGHBOR_D] = motion_candidate(-12, 18, 5U, 1);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 1, 5U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == -12 && prediction.y == 18);

    candidates[CAVS_MOTION_NEIGHBOR_C] = motion_candidate(99, 99, 2U, 1);
    candidates[CAVS_MOTION_NEIGHBOR_C].intra = 1U;
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 1, 5U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 0 && prediction.y == 0);

    clear_motion_candidates(candidates);
    candidates[0] = motion_candidate(50, 60, 1U, 0);
    candidates[0].same_direction = 0U;
    candidates[1] = motion_candidate(-3, 4, 1U, 0);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == -3 && prediction.y == 4);
}

static void test_motion_prediction_partition_shortcuts(void) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector prediction;
    clear_motion_candidates(candidates);
    candidates[0] = motion_candidate(1, 11, 1U, 2);
    candidates[1] = motion_candidate(2, 12, 1U, 2);
    candidates[2] = motion_candidate(3, 13, 1U, 2);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 2, 1U, CAVS_MOTION_PARTITION_8X16_LEFT,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 1 && prediction.y == 11);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 2, 1U, CAVS_MOTION_PARTITION_8X16_RIGHT,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 3 && prediction.y == 13);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 2, 1U, CAVS_MOTION_PARTITION_16X8_TOP,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 2 && prediction.y == 12);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 2, 1U, CAVS_MOTION_PARTITION_16X8_BOTTOM,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 1 && prediction.y == 11);
}

static void set_median_candidates(
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT],
    int32_t a, int32_t b, int32_t c) {
    clear_motion_candidates(candidates);
    candidates[0] = motion_candidate(a, 0, 1U, 0);
    candidates[1] = motion_candidate(b, 0, 1U, 0);
    candidates[2] = motion_candidate(c, 0, 1U, 0);
}

static void test_motion_prediction_median_selection(void) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector prediction;
    set_median_candidates(candidates, 0, 10, 15);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 15);
    set_median_candidates(candidates, 0, 10, 30);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 0);
    set_median_candidates(candidates, 0, 30, 20);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 30);
}

static void test_motion_prediction_scaling(void) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector prediction;
    clear_motion_candidates(candidates);
    candidates[0] = motion_candidate(10, -11, 2U, 0);
    candidates[1] = motion_candidate(40, -44, 4U, 0);
    candidates[2] = motion_candidate(30, -33, 6U, 0);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 4U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 20 && prediction.y == -22);

    candidates[0] = motion_candidate(4095, 4095, 1U, 0);
    candidates[1] = candidates[0];
    candidates[2] = candidates[0];
    prediction.x = 77;
    prediction.y = 88;
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 511U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 2092545 && prediction.y == 2092545);
}

static void test_motion_difference_decoding(void) {
    cavs_luma_motion_vector prediction = {4000, -4000};
    cavs_luma_motion_vector difference = {95, -96};
    cavs_luma_motion_vector decoded = {77, 88};
    TEST_CHECK(cavs_decode_luma_motion(
               &prediction, &difference, CAVS_LUMA_MOTION_QUARTER,
               &decoded) == CAVS_OK);
    TEST_CHECK(decoded.x == 4095 && decoded.y == -4096);
    difference.x = 96;
    decoded.x = 77;
    decoded.y = 88;
    TEST_CHECK(cavs_decode_luma_motion(
               &prediction, &difference, CAVS_LUMA_MOTION_QUARTER,
               &decoded) == CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(decoded.x == 77 && decoded.y == 88);
    prediction.x = -8000;
    prediction.y = 8000;
    difference.x = -192;
    difference.y = 191;
    TEST_CHECK(cavs_decode_luma_motion(
               &prediction, &difference, CAVS_LUMA_MOTION_EIGHTH,
               &decoded) == CAVS_OK);
    TEST_CHECK(decoded.x == -8192 && decoded.y == 8191);
    prediction.x = 5000;
    prediction.y = -5000;
    difference.x = -1000;
    difference.y = 1000;
    TEST_CHECK(cavs_decode_luma_motion(
               &prediction, &difference, CAVS_LUMA_MOTION_QUARTER,
               &decoded) == CAVS_OK);
    TEST_CHECK(decoded.x == 4000 && decoded.y == -4000);
}

static void test_motion_prediction_invalid(void) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector prediction = {77, 88};
    clear_motion_candidates(candidates);
    candidates[0] = motion_candidate(1, 2, 0U, 0);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(prediction.x == 77 && prediction.y == 88);
    candidates[0] = motion_candidate(1, 2, 1U, 0);
    candidates[0].available = 2U;
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_predict_luma_motion(
               NULL, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
    clear_motion_candidates(candidates);
    candidates[0] = motion_candidate(8191, -8192, 1U, 0);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_EIGHTH, &prediction) == CAVS_OK);
    TEST_CHECK(prediction.x == 8191 && prediction.y == -8192);
    prediction.x = 77;
    prediction.y = 88;
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_QUARTER, &prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 4, 1U, CAVS_MOTION_PARTITION_OTHER,
               CAVS_LUMA_MOTION_EIGHTH, &prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, (cavs_motion_partition_position)5,
               CAVS_LUMA_MOTION_EIGHTH, &prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_predict_luma_motion(
               candidates, 0, 1U, CAVS_MOTION_PARTITION_OTHER,
               (cavs_luma_motion_precision)1, &prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
}

static void test_p_skip_motion(void) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector motion = {77, 88};
    clear_motion_candidates(candidates);
    candidates[0] = motion_candidate(4, 8, 2U, 0);
    candidates[1] = motion_candidate(8, 16, 4U, 0);
    candidates[2] = motion_candidate(12, 24, 6U, 0);
    TEST_CHECK(cavs_derive_p_skip_motion(
               candidates, 4U, CAVS_LUMA_MOTION_QUARTER,
               &motion) == CAVS_OK);
    TEST_CHECK(motion.x == 8 && motion.y == 16);

    candidates[0].available = 0U;
    TEST_CHECK(cavs_derive_p_skip_motion(
               candidates, 4U, CAVS_LUMA_MOTION_QUARTER,
               &motion) == CAVS_OK);
    TEST_CHECK(motion.x == 0 && motion.y == 0);
    candidates[0] = motion_candidate(0, 0, 2U, 0);
    TEST_CHECK(cavs_derive_p_skip_motion(
               candidates, 4U, CAVS_LUMA_MOTION_QUARTER,
               &motion) == CAVS_OK);
    TEST_CHECK(motion.x == 0 && motion.y == 0);

    candidates[0].intra = 1U;
    candidates[1] = motion_candidate(9, -7, 4U, 0);
    candidates[2].available = 0U;
    TEST_CHECK(cavs_derive_p_skip_motion(
               candidates, 4U, CAVS_LUMA_MOTION_QUARTER,
               &motion) == CAVS_OK);
    TEST_CHECK(motion.x == 9 && motion.y == -7);

    motion.x = 77;
    motion.y = 88;
    TEST_CHECK(cavs_derive_p_skip_motion(
               candidates, 0U, CAVS_LUMA_MOTION_QUARTER,
               &motion) == CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(motion.x == 77 && motion.y == 88);
}

static void test_symmetric_motion(void) {
    cavs_luma_motion_vector forward = {3, -3};
    cavs_bidirectional_motion motion;
    cavs_bidirectional_motion unchanged;
    memset(&motion, 0xa5, sizeof(motion));
    TEST_CHECK(cavs_derive_symmetric_motion(
               &forward, 1, 1U, 3U, 2U,
               CAVS_LUMA_MOTION_QUARTER, &motion) == CAVS_OK);
    TEST_CHECK(motion.forward.x == 3 && motion.forward.y == -3);
    TEST_CHECK(motion.forward_reference_index == 1);
    TEST_CHECK(motion.backward_reference_index == 1);
    TEST_CHECK(motion.backward.x == -2 && motion.backward.y == 2);

    TEST_CHECK(cavs_derive_symmetric_motion(
               &forward, 1, 0U, 3U, 2U,
               CAVS_LUMA_MOTION_QUARTER, &motion) == CAVS_OK);
    TEST_CHECK(motion.backward_reference_index == 0);

    memset(&motion, 0xa5, sizeof(motion));
    unchanged = motion;
    TEST_CHECK(cavs_derive_symmetric_motion(
               &forward, 1, 1U, 0U, 2U,
               CAVS_LUMA_MOTION_QUARTER, &motion) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(memcmp(&motion, &unchanged, sizeof(motion)) == 0);
    forward.x = 4095;
    forward.y = -4096;
    TEST_CHECK(cavs_derive_symmetric_motion(
               &forward, 0, 1U, 1U, 511U,
               CAVS_LUMA_MOTION_QUARTER, &motion) ==
           CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(memcmp(&motion, &unchanged, sizeof(motion)) == 0);
}

static void test_direct_motion(void) {
    cavs_luma_motion_vector colocated = {3, -3};
    cavs_bidirectional_motion motion;
    cavs_bidirectional_motion unchanged;
    memset(&motion, 0, sizeof(motion));
    TEST_CHECK(cavs_derive_direct_motion(
               &colocated, 0, 1, 1U, 1U, 5U, 2U, 3U,
               CAVS_LUMA_MOTION_QUARTER, &motion) == CAVS_OK);
    TEST_CHECK(motion.forward_reference_index == 0);
    TEST_CHECK(motion.backward_reference_index == 1);
    TEST_CHECK(motion.forward.x == 1 && motion.forward.y == -1);
    TEST_CHECK(motion.backward.x == -1 && motion.backward.y == 1);

    colocated.x = 0;
    colocated.y = 0;
    TEST_CHECK(cavs_derive_direct_motion(
               &colocated, 2, 3, 1U, 1U, 7U, 4U, 6U,
               CAVS_LUMA_MOTION_EIGHTH, &motion) == CAVS_OK);
    TEST_CHECK(motion.forward.x == 0 && motion.forward.y == 0);
    TEST_CHECK(motion.backward.x == 0 && motion.backward.y == 0);

    colocated.y = 3;
    TEST_CHECK(cavs_derive_direct_motion(
               &colocated, 0, 1, 1U, 0U, 5U, 2U, 3U,
               CAVS_LUMA_MOTION_QUARTER, &motion) == CAVS_OK);
    TEST_CHECK(motion.forward.y == 2 && motion.backward.y == -3);
    colocated.y = -3;
    TEST_CHECK(cavs_derive_direct_motion(
               &colocated, 0, 1, 0U, 1U, 1U, 1U, 1U,
               CAVS_LUMA_MOTION_QUARTER, &motion) == CAVS_OK);
    TEST_CHECK(motion.forward.y == -1 && motion.backward.y == 1);

    memset(&motion, 0xa5, sizeof(motion));
    unchanged = motion;
    TEST_CHECK(cavs_derive_direct_motion(
               &colocated, 0, 1, 1U, 1U, 0U, 2U, 3U,
               CAVS_LUMA_MOTION_QUARTER, &motion) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(memcmp(&motion, &unchanged, sizeof(motion)) == 0);
    colocated.x = 4095;
    colocated.y = -4096;
    TEST_CHECK(cavs_derive_direct_motion(
               &colocated, 0, 1, 1U, 1U, 1U, 511U, 511U,
               CAVS_LUMA_MOTION_QUARTER, &motion) ==
           CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(memcmp(&motion, &unchanged, sizeof(motion)) == 0);
}

static void test_chroma_integer_and_stride(void) {
    uint8_t plane[12U * 16U];
    uint8_t prediction[8U * 10U];
    size_t x;
    size_t y;
    fill_motion_plane(plane, 12U, 12U, 16U);
    memset(prediction, 0xa5, sizeof(prediction));
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 12U, 12U, 16U, 3U, 2U, 8U, 8U, 0, 0,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 10U) == CAVS_OK);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x)
            TEST_CHECK(prediction[y * 10U + x] == plane[(y + 2U) * 16U + x + 3U]);
        TEST_CHECK(prediction[y * 10U + 8U] == 0xa5U);
        TEST_CHECK(prediction[y * 10U + 9U] == 0xa5U);
    }
}

static void test_chroma_motion_derivation(void) {
    int32_t chroma_x = 99;
    int32_t chroma_y = 99;
    TEST_CHECK(cavs_derive_chroma_motion(
               CAVS_YUV420P8, -13, 17, &chroma_x, &chroma_y) == CAVS_OK);
    TEST_CHECK(chroma_x == -13 && chroma_y == 17);
    TEST_CHECK(cavs_derive_chroma_motion(
               CAVS_YUV422P8, -13, 17, &chroma_x, &chroma_y) == CAVS_OK);
    TEST_CHECK(chroma_x == -13 && chroma_y == 34);
    chroma_x = 99;
    chroma_y = 99;
    TEST_CHECK(cavs_derive_chroma_motion(
               CAVS_YUV422P8, 0, INT32_MAX, &chroma_x, &chroma_y) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(chroma_x == 99 && chroma_y == 99);
    TEST_CHECK(cavs_derive_chroma_motion(
               (cavs_pixel_format)2, 0, 0, &chroma_x, &chroma_y) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_derive_chroma_motion(
               CAVS_YUV420P8, 0, 0, NULL, &chroma_y) ==
           CAVS_ERR_INVALID_ARGUMENT);
}

static void test_chroma_fractional(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[4];
    uint32_t expected;
    fill_motion_plane(plane, 8U, 8U, 8U);
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 2U, 2U, 1U, 1U, 3, 5,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    expected = (5U * 3U * plane[2U * 8U + 2U] +
                3U * 3U * plane[2U * 8U + 3U] +
                5U * 5U * plane[3U * 8U + 2U] +
                3U * 5U * plane[3U * 8U + 3U] + 32U) >> 6U;
    TEST_CHECK(prediction[0] == expected);

    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 2U, 2U, 1U, 1U, 7, 11,
               CAVS_CHROMA_MOTION_SIXTEENTH, prediction, 1U) == CAVS_OK);
    expected = (9U * 5U * plane[2U * 8U + 2U] +
                7U * 5U * plane[2U * 8U + 3U] +
                9U * 11U * plane[3U * 8U + 2U] +
                7U * 11U * plane[3U * 8U + 3U] + 128U) >> 8U;
    TEST_CHECK(prediction[0] == expected);
}

static void test_chroma_all_fractional_phases(void) {
    uint8_t plane[4] = {13U, 79U, 151U, 241U};
    uint8_t prediction[1];
    unsigned precision;
    for (precision = 3U; precision <= 4U; ++precision) {
        uint32_t denominator = UINT32_C(1) << precision;
        uint32_t scale = denominator * denominator;
        uint32_t dy;
        for (dy = 0U; dy < denominator; ++dy) {
            uint32_t dx;
            for (dx = 0U; dx < denominator; ++dx) {
                uint32_t expected =
                    ((denominator - dx) * (denominator - dy) * plane[0] +
                     dx * (denominator - dy) * plane[1] +
                     (denominator - dx) * dy * plane[2] +
                     dx * dy * plane[3] + scale / 2U) / scale;
                TEST_CHECK(cavs_interpolate_chroma_block(
                           plane, 2U, 2U, 2U, 0U, 0U, 1U, 1U,
                           (int32_t)dx, (int32_t)dy,
                           (cavs_chroma_motion_precision)precision,
                           prediction, 1U) == CAVS_OK);
                TEST_CHECK(prediction[0] == expected);
            }
        }
    }
}

static void test_chroma_negative_motion(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[4];
    uint32_t expected;
    fill_motion_plane(plane, 8U, 8U, 8U);
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 4U, 4U, 1U, 1U, -1, -1,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    expected = (plane[3U * 8U + 3U] + 7U * plane[3U * 8U + 4U] +
                7U * plane[4U * 8U + 3U] + 49U * plane[4U * 8U + 4U] +
                32U) >> 6U;
    TEST_CHECK(prediction[0] == expected);
}

static void test_chroma_edge_replacement(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[4];
    fill_motion_plane(plane, 8U, 8U, 8U);
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, -1, -1,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == plane[0]);
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, INT32_MIN, INT32_MIN,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == plane[0]);
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 7U, 7U, 1U, 1U, INT32_MAX, INT32_MAX,
               CAVS_CHROMA_MOTION_SIXTEENTH, prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == plane[7U * 8U + 7U]);
}

static void test_chroma_invalid_atomic(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[16];
    uint8_t unchanged[16];
    fill_motion_plane(plane, 8U, 8U, 8U);
    memset(prediction, 0xa5, sizeof(prediction));
    memcpy(unchanged, prediction, sizeof(prediction));
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 7U, 0U, 0U, 1U, 1U, 0, 0,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(memcmp(prediction, unchanged, sizeof(prediction)) == 0);
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 1U, 1U, 8U, 8U, 0, 0,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 8U) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, 0, 0,
               (cavs_chroma_motion_precision)2, prediction, 1U) ==
           CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_interpolate_chroma_block(
               NULL, 8U, 8U, 8U, 0U, 0U, 1U, 1U, 0, 0,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) ==
           CAVS_ERR_INVALID_ARGUMENT);
}

static void test_luma_all_quarter_phases(void) {
    static const uint8_t expected[16] = {
        41U, 55U, 50U, 90U,
        59U, 87U, 97U, 127U,
        58U, 100U, 133U, 165U,
        102U, 135U, 169U, 181U
    };
    uint8_t plane[10U * 12U];
    uint8_t prediction[1];
    unsigned fraction_y;
    fill_luma_pattern(plane, 10U, 10U, 12U);
    for (fraction_y = 0U; fraction_y < 4U; ++fraction_y) {
        unsigned fraction_x;
        for (fraction_x = 0U; fraction_x < 4U; ++fraction_x) {
            TEST_CHECK(cavs_interpolate_luma_block_quarter(
                       plane, 10U, 10U, 12U, 4U, 4U, 1U, 1U,
                       (int32_t)fraction_x, (int32_t)fraction_y,
                       prediction, 1U) == CAVS_OK);
            TEST_CHECK(prediction[0] == expected[fraction_y * 4U + fraction_x]);
        }
    }
}

static void test_luma_block_and_stride(void) {
    uint8_t plane[12U * 16U];
    uint8_t prediction[8U * 10U];
    size_t x;
    size_t y;
    fill_motion_plane(plane, 12U, 12U, 16U);
    memset(prediction, 0xa5, sizeof(prediction));
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 12U, 12U, 16U, 2U, 2U, 8U, 8U, 4, 4,
               prediction, 10U) == CAVS_OK);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x)
            TEST_CHECK(prediction[y * 10U + x] ==
                   plane[(y + 3U) * 16U + x + 3U]);
        TEST_CHECK(prediction[y * 10U + 8U] == 0xa5U);
        TEST_CHECK(prediction[y * 10U + 9U] == 0xa5U);
    }
}

static void test_luma_clipping(void) {
    uint8_t plane[6U * 6U];
    uint8_t prediction[1];
    size_t y;
    memset(plane, 0U, sizeof(plane));
    for (y = 0U; y < 6U; ++y) {
        plane[y * 6U + 1U] = 255U;
        plane[y * 6U + 4U] = 255U;
    }
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 6U, 6U, 6U, 2U, 2U, 1U, 1U, 2, 0,
               prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == 0U);
    for (y = 0U; y < 6U; ++y) {
        plane[y * 6U + 1U] = 0U;
        plane[y * 6U + 2U] = 255U;
        plane[y * 6U + 3U] = 255U;
        plane[y * 6U + 4U] = 0U;
    }
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 6U, 6U, 6U, 2U, 2U, 1U, 1U, 2, 0,
               prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == 255U);
}

static void test_luma_negative_and_edges(void) {
    uint8_t plane[10U * 10U];
    uint8_t prediction[1];
    fill_luma_pattern(plane, 10U, 10U, 10U);
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 10U, 10U, 10U, 0U, 0U, 1U, 1U, -1, -1,
               prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == 11U);
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 10U, 10U, 10U, 0U, 0U, 1U, 1U,
               INT32_MIN, INT32_MIN, prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == plane[0]);
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 10U, 10U, 10U, 9U, 9U, 1U, 1U,
               INT32_MAX, INT32_MAX, prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == plane[9U * 10U + 9U]);
}

static void test_luma_in_place(void) {
    uint8_t plane[16U * 16U];
    size_t index;
    fill_motion_plane(plane, 16U, 16U, 16U);
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 16U, 16U, 16U, 0U, 0U, 16U, 16U, 0, 0,
               plane, 16U) == CAVS_OK);
    for (index = 0U; index < sizeof(plane); ++index)
        TEST_CHECK(plane[index] == (uint8_t)((index / 16U) * 16U + index % 16U));
}

static void test_luma_invalid_atomic(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[16];
    uint8_t unchanged[16];
    fill_motion_plane(plane, 8U, 8U, 8U);
    memset(prediction, 0xa5, sizeof(prediction));
    memcpy(unchanged, prediction, sizeof(prediction));
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 8U, 8U, 8U, 0U, 0U, 0U, 1U, 0, 0,
               prediction, 1U) == CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(memcmp(prediction, unchanged, sizeof(prediction)) == 0);
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, 0, 0,
               prediction, 0U) == CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_interpolate_luma_block_quarter(
               NULL, 8U, 8U, 8U, 0U, 0U, 1U, 1U, 0, 0,
               prediction, 1U) == CAVS_ERR_INVALID_ARGUMENT);
}

static void test_luma_all_eighth_phases(void) {
    static const uint8_t expected[64] = {
        41U, 34U, 55U, 39U, 50U, 65U, 90U, 99U,
        36U, 51U, 58U, 64U, 66U, 89U, 107U, 125U,
        59U, 60U, 87U, 81U, 97U, 110U, 127U, 147U,
        45U, 68U, 83U, 97U, 107U, 131U, 149U, 168U,
        58U, 72U, 100U, 109U, 133U, 156U, 165U, 194U,
        75U, 97U, 116U, 135U, 159U, 169U, 184U, 199U,
        102U, 117U, 135U, 155U, 169U, 186U, 181U, 208U,
        113U, 137U, 157U, 176U, 200U, 203U, 210U, 217U
    };
    uint8_t plane[10U * 12U];
    uint8_t prediction[1];
    unsigned fraction_y;
    fill_luma_pattern(plane, 10U, 10U, 12U);
    for (fraction_y = 0U; fraction_y < 8U; ++fraction_y) {
        unsigned fraction_x;
        for (fraction_x = 0U; fraction_x < 8U; ++fraction_x) {
            TEST_CHECK(cavs_interpolate_luma_block_eighth(
                       plane, 10U, 10U, 12U, 4U, 4U, 1U, 1U,
                       (int32_t)fraction_x, (int32_t)fraction_y,
                       prediction, 1U) == CAVS_OK);
            TEST_CHECK(prediction[0] == expected[fraction_y * 8U + fraction_x]);
        }
    }
}

static void test_luma_eighth_quarter_equivalence(void) {
    uint8_t plane[10U * 10U];
    uint8_t eighth[1];
    uint8_t quarter[1];
    int32_t motion_y;
    fill_luma_pattern(plane, 10U, 10U, 10U);
    for (motion_y = -6; motion_y <= 6; motion_y += 2) {
        int32_t motion_x;
        for (motion_x = -6; motion_x <= 6; motion_x += 2) {
            TEST_CHECK(cavs_interpolate_luma_block_eighth(
                       plane, 10U, 10U, 10U, 4U, 4U, 1U, 1U,
                       motion_x, motion_y, eighth, 1U) == CAVS_OK);
            TEST_CHECK(cavs_interpolate_luma_block_quarter(
                       plane, 10U, 10U, 10U, 4U, 4U, 1U, 1U,
                       motion_x / 2, motion_y / 2, quarter, 1U) == CAVS_OK);
            TEST_CHECK(eighth[0] == quarter[0]);
        }
    }
}

static void test_luma_eighth_constant_and_clipping(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[1];
    unsigned fraction_y;
    memset(plane, 123, sizeof(plane));
    for (fraction_y = 0U; fraction_y < 8U; ++fraction_y) {
        unsigned fraction_x;
        for (fraction_x = 0U; fraction_x < 8U; ++fraction_x) {
            TEST_CHECK(cavs_interpolate_luma_block_eighth(
                       plane, 8U, 8U, 8U, 3U, 3U, 1U, 1U,
                       (int32_t)fraction_x, (int32_t)fraction_y,
                       prediction, 1U) == CAVS_OK);
            TEST_CHECK(prediction[0] == 123U);
        }
    }

    memset(plane, 0, sizeof(plane));
    for (fraction_y = 0U; fraction_y < 8U; ++fraction_y)
        plane[fraction_y * 8U + 2U] = 255U;
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 8U, 8U, 8U, 3U, 3U, 1U, 1U, 1, 0,
               prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == 0U);

    memset(plane, 0, sizeof(plane));
    for (fraction_y = 0U; fraction_y < 8U; ++fraction_y) {
        plane[fraction_y * 8U + 3U] = 255U;
        plane[fraction_y * 8U + 4U] = 255U;
    }
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 8U, 8U, 8U, 3U, 3U, 1U, 1U, 1, 0,
               prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == 255U);
}

static void test_luma_eighth_block_and_stride(void) {
    uint8_t plane[12U * 16U];
    uint8_t prediction[8U * 10U];
    size_t x;
    size_t y;
    fill_motion_plane(plane, 12U, 12U, 16U);
    memset(prediction, 0xa5, sizeof(prediction));
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 12U, 12U, 16U, 2U, 2U, 8U, 8U, 8, 8,
               prediction, 10U) == CAVS_OK);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x)
            TEST_CHECK(prediction[y * 10U + x] ==
                   plane[(y + 3U) * 16U + x + 3U]);
        TEST_CHECK(prediction[y * 10U + 8U] == 0xa5U);
        TEST_CHECK(prediction[y * 10U + 9U] == 0xa5U);
    }
}

static void test_luma_eighth_negative_and_edges(void) {
    uint8_t plane[10U * 10U];
    uint8_t prediction[1];
    fill_luma_pattern(plane, 10U, 10U, 10U);
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 10U, 10U, 10U, 0U, 0U, 1U, 1U, -1, -1,
               prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == 14U);
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 10U, 10U, 10U, 0U, 0U, 1U, 1U,
               INT32_MIN, INT32_MIN, prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == plane[0]);
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 10U, 10U, 10U, 9U, 9U, 1U, 1U,
               INT32_MAX, INT32_MAX, prediction, 1U) == CAVS_OK);
    TEST_CHECK(prediction[0] == plane[9U * 10U + 9U]);
}

static void test_luma_eighth_in_place_and_invalid(void) {
    uint8_t plane[16U * 16U];
    uint8_t original[16U * 16U];
    uint8_t expected[16U * 16U];
    uint8_t unchanged[16U * 16U];
    fill_luma_pattern(plane, 16U, 16U, 16U);
    memcpy(original, plane, sizeof(plane));
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               original, 16U, 16U, 16U, 0U, 0U, 16U, 16U, 3, 5,
               expected, 16U) == CAVS_OK);
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 16U, 16U, 16U, 0U, 0U, 16U, 16U, 3, 5,
               plane, 16U) == CAVS_OK);
    TEST_CHECK(memcmp(plane, expected, sizeof(plane)) == 0);
    memcpy(unchanged, plane, sizeof(plane));
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               plane, 16U, 16U, 16U, 0U, 0U, 0U, 16U, 0, 0,
               plane, 16U) == CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(memcmp(plane, unchanged, sizeof(plane)) == 0);
    TEST_CHECK(cavs_interpolate_luma_block_eighth(
               NULL, 16U, 16U, 16U, 0U, 0U, 1U, 1U, 0, 0,
               plane, 16U) == CAVS_ERR_INVALID_ARGUMENT);
}

void test_motion(void) {
    test_motion_prediction_normalization();
    test_motion_prediction_partition_shortcuts();
    test_motion_prediction_median_selection();
    test_motion_prediction_scaling();
    test_motion_difference_decoding();
    test_motion_prediction_invalid();
    test_p_skip_motion();
    test_symmetric_motion();
    test_direct_motion();
    test_chroma_motion_derivation();
    test_chroma_integer_and_stride();
    test_chroma_fractional();
    test_chroma_all_fractional_phases();
    test_chroma_negative_motion();
    test_chroma_edge_replacement();
    test_chroma_invalid_atomic();
    test_luma_all_quarter_phases();
    test_luma_block_and_stride();
    test_luma_clipping();
    test_luma_negative_and_edges();
    test_luma_in_place();
    test_luma_invalid_atomic();
    test_luma_all_eighth_phases();
    test_luma_eighth_quarter_equivalence();
    test_luma_eighth_constant_and_clipping();
    test_luma_eighth_block_and_stride();
    test_luma_eighth_negative_and_edges();
    test_luma_eighth_in_place_and_invalid();
}
