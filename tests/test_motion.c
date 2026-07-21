/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013 chroma interpolation tests.
 */
#include "motion.h"
#include <assert.h>
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

static void test_chroma_integer_and_stride(void) {
    uint8_t plane[12U * 16U];
    uint8_t prediction[8U * 10U];
    size_t x;
    size_t y;
    fill_motion_plane(plane, 12U, 12U, 16U);
    memset(prediction, 0xa5, sizeof(prediction));
    assert(cavs_interpolate_chroma_block(
               plane, 12U, 12U, 16U, 3U, 2U, 8U, 8U, 0, 0,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 10U) == CAVS_OK);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x)
            assert(prediction[y * 10U + x] == plane[(y + 2U) * 16U + x + 3U]);
        assert(prediction[y * 10U + 8U] == 0xa5U);
        assert(prediction[y * 10U + 9U] == 0xa5U);
    }
}

static void test_chroma_motion_derivation(void) {
    int32_t chroma_x = 99;
    int32_t chroma_y = 99;
    assert(cavs_derive_chroma_motion(
               CAVS_YUV420P8, -13, 17, &chroma_x, &chroma_y) == CAVS_OK);
    assert(chroma_x == -13 && chroma_y == 17);
    assert(cavs_derive_chroma_motion(
               CAVS_YUV422P8, -13, 17, &chroma_x, &chroma_y) == CAVS_OK);
    assert(chroma_x == -13 && chroma_y == 34);
    chroma_x = 99;
    chroma_y = 99;
    assert(cavs_derive_chroma_motion(
               CAVS_YUV422P8, 0, INT32_MAX, &chroma_x, &chroma_y) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(chroma_x == 99 && chroma_y == 99);
    assert(cavs_derive_chroma_motion(
               (cavs_pixel_format)2, 0, 0, &chroma_x, &chroma_y) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_derive_chroma_motion(
               CAVS_YUV420P8, 0, 0, NULL, &chroma_y) ==
           CAVS_ERR_INVALID_ARGUMENT);
}

static void test_chroma_fractional(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[4];
    uint32_t expected;
    fill_motion_plane(plane, 8U, 8U, 8U);
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 2U, 2U, 1U, 1U, 3, 5,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    expected = (5U * 3U * plane[2U * 8U + 2U] +
                3U * 3U * plane[2U * 8U + 3U] +
                5U * 5U * plane[3U * 8U + 2U] +
                3U * 5U * plane[3U * 8U + 3U] + 32U) >> 6U;
    assert(prediction[0] == expected);

    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 2U, 2U, 1U, 1U, 7, 11,
               CAVS_CHROMA_MOTION_SIXTEENTH, prediction, 1U) == CAVS_OK);
    expected = (9U * 5U * plane[2U * 8U + 2U] +
                7U * 5U * plane[2U * 8U + 3U] +
                9U * 11U * plane[3U * 8U + 2U] +
                7U * 11U * plane[3U * 8U + 3U] + 128U) >> 8U;
    assert(prediction[0] == expected);
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
                assert(cavs_interpolate_chroma_block(
                           plane, 2U, 2U, 2U, 0U, 0U, 1U, 1U,
                           (int32_t)dx, (int32_t)dy,
                           (cavs_chroma_motion_precision)precision,
                           prediction, 1U) == CAVS_OK);
                assert(prediction[0] == expected);
            }
        }
    }
}

static void test_chroma_negative_motion(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[4];
    uint32_t expected;
    fill_motion_plane(plane, 8U, 8U, 8U);
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 4U, 4U, 1U, 1U, -1, -1,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    expected = (plane[3U * 8U + 3U] + 7U * plane[3U * 8U + 4U] +
                7U * plane[4U * 8U + 3U] + 49U * plane[4U * 8U + 4U] +
                32U) >> 6U;
    assert(prediction[0] == expected);
}

static void test_chroma_edge_replacement(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[4];
    fill_motion_plane(plane, 8U, 8U, 8U);
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, -1, -1,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    assert(prediction[0] == plane[0]);
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, INT32_MIN, INT32_MIN,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) == CAVS_OK);
    assert(prediction[0] == plane[0]);
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 7U, 7U, 1U, 1U, INT32_MAX, INT32_MAX,
               CAVS_CHROMA_MOTION_SIXTEENTH, prediction, 1U) == CAVS_OK);
    assert(prediction[0] == plane[7U * 8U + 7U]);
}

static void test_chroma_invalid_atomic(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[16];
    uint8_t unchanged[16];
    fill_motion_plane(plane, 8U, 8U, 8U);
    memset(prediction, 0xa5, sizeof(prediction));
    memcpy(unchanged, prediction, sizeof(prediction));
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 7U, 0U, 0U, 1U, 1U, 0, 0,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 1U) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(memcmp(prediction, unchanged, sizeof(prediction)) == 0);
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 1U, 1U, 8U, 8U, 0, 0,
               CAVS_CHROMA_MOTION_EIGHTH, prediction, 8U) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_interpolate_chroma_block(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, 0, 0,
               (cavs_chroma_motion_precision)2, prediction, 1U) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_interpolate_chroma_block(
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
            assert(cavs_interpolate_luma_block_quarter(
                       plane, 10U, 10U, 12U, 4U, 4U, 1U, 1U,
                       (int32_t)fraction_x, (int32_t)fraction_y,
                       prediction, 1U) == CAVS_OK);
            assert(prediction[0] == expected[fraction_y * 4U + fraction_x]);
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
    assert(cavs_interpolate_luma_block_quarter(
               plane, 12U, 12U, 16U, 2U, 2U, 8U, 8U, 4, 4,
               prediction, 10U) == CAVS_OK);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x)
            assert(prediction[y * 10U + x] ==
                   plane[(y + 3U) * 16U + x + 3U]);
        assert(prediction[y * 10U + 8U] == 0xa5U);
        assert(prediction[y * 10U + 9U] == 0xa5U);
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
    assert(cavs_interpolate_luma_block_quarter(
               plane, 6U, 6U, 6U, 2U, 2U, 1U, 1U, 2, 0,
               prediction, 1U) == CAVS_OK);
    assert(prediction[0] == 0U);
    for (y = 0U; y < 6U; ++y) {
        plane[y * 6U + 1U] = 0U;
        plane[y * 6U + 2U] = 255U;
        plane[y * 6U + 3U] = 255U;
        plane[y * 6U + 4U] = 0U;
    }
    assert(cavs_interpolate_luma_block_quarter(
               plane, 6U, 6U, 6U, 2U, 2U, 1U, 1U, 2, 0,
               prediction, 1U) == CAVS_OK);
    assert(prediction[0] == 255U);
}

static void test_luma_negative_and_edges(void) {
    uint8_t plane[10U * 10U];
    uint8_t prediction[1];
    fill_luma_pattern(plane, 10U, 10U, 10U);
    assert(cavs_interpolate_luma_block_quarter(
               plane, 10U, 10U, 10U, 0U, 0U, 1U, 1U, -1, -1,
               prediction, 1U) == CAVS_OK);
    assert(prediction[0] == 11U);
    assert(cavs_interpolate_luma_block_quarter(
               plane, 10U, 10U, 10U, 0U, 0U, 1U, 1U,
               INT32_MIN, INT32_MIN, prediction, 1U) == CAVS_OK);
    assert(prediction[0] == plane[0]);
    assert(cavs_interpolate_luma_block_quarter(
               plane, 10U, 10U, 10U, 9U, 9U, 1U, 1U,
               INT32_MAX, INT32_MAX, prediction, 1U) == CAVS_OK);
    assert(prediction[0] == plane[9U * 10U + 9U]);
}

static void test_luma_in_place(void) {
    uint8_t plane[16U * 16U];
    size_t index;
    fill_motion_plane(plane, 16U, 16U, 16U);
    assert(cavs_interpolate_luma_block_quarter(
               plane, 16U, 16U, 16U, 0U, 0U, 16U, 16U, 0, 0,
               plane, 16U) == CAVS_OK);
    for (index = 0U; index < sizeof(plane); ++index)
        assert(plane[index] == (uint8_t)((index / 16U) * 16U + index % 16U));
}

static void test_luma_invalid_atomic(void) {
    uint8_t plane[8U * 8U];
    uint8_t prediction[16];
    uint8_t unchanged[16];
    fill_motion_plane(plane, 8U, 8U, 8U);
    memset(prediction, 0xa5, sizeof(prediction));
    memcpy(unchanged, prediction, sizeof(prediction));
    assert(cavs_interpolate_luma_block_quarter(
               plane, 8U, 8U, 8U, 0U, 0U, 0U, 1U, 0, 0,
               prediction, 1U) == CAVS_ERR_INVALID_ARGUMENT);
    assert(memcmp(prediction, unchanged, sizeof(prediction)) == 0);
    assert(cavs_interpolate_luma_block_quarter(
               plane, 8U, 8U, 8U, 0U, 0U, 1U, 1U, 0, 0,
               prediction, 0U) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_interpolate_luma_block_quarter(
               NULL, 8U, 8U, 8U, 0U, 0U, 1U, 1U, 0, 0,
               prediction, 1U) == CAVS_ERR_INVALID_ARGUMENT);
}

void test_motion(void) {
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
}
