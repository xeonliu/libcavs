/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013 baseline 8x8 prediction and reconstruction tests.
 */
#include "prediction.h"
#include "reconstruction.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define ALL_REFERENCES UINT32_C(0x1ffff)
#define DC_REFERENCES UINT32_C(0x003ff)

static int64_t floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static uint8_t clip_sample(int64_t value) {
    if (value < 0) return 0U;
    if (value > 255) return 255U;
    return (uint8_t)value;
}

static uint8_t filter(const uint8_t samples[17], unsigned center) {
    unsigned before = center == 0U ? 0U : center - 1U;
    unsigned after = center >= 16U ? 16U : center + 1U;
    if (center > 16U) center = 16U;
    return (uint8_t)((samples[before] + 2U * samples[center] +
                      samples[after] + 2U) >> 2U);
}

static cavs_intra_references_8x8 make_references(void) {
    cavs_intra_references_8x8 references;
    unsigned index;
    memset(&references, 0, sizeof(references));
    for (index = 0U; index < 17U; ++index) {
        references.top[index] = (uint8_t)(20U + index * 7U);
        references.left[index] = (uint8_t)(20U + index * 5U);
    }
    references.top_available = ALL_REFERENCES;
    references.left_available = ALL_REFERENCES;
    return references;
}

static void test_luma_modes(void) {
    cavs_intra_references_8x8 references = make_references();
    uint8_t prediction[64];
    unsigned x;
    unsigned y;
    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_VERTICAL_8X8, prediction) == CAVS_OK);
    for (y = 0U; y < 8U; ++y)
        for (x = 0U; x < 8U; ++x)
            assert(prediction[y * 8U + x] == references.top[x + 1U]);

    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_HORIZONTAL_8X8, prediction) == CAVS_OK);
    for (y = 0U; y < 8U; ++y)
        for (x = 0U; x < 8U; ++x)
            assert(prediction[y * 8U + x] == references.left[y + 1U]);

    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_DC_8X8, prediction) == CAVS_OK);
    for (y = 0U; y < 8U; ++y)
        for (x = 0U; x < 8U; ++x)
            assert(prediction[y * 8U + x] ==
                   (uint8_t)((filter(references.top, x + 1U) +
                              filter(references.left, y + 1U)) >> 1U));

    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_DOWN_LEFT_8X8, prediction) == CAVS_OK);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x) {
            unsigned center = x + y + 2U;
            assert(prediction[y * 8U + x] ==
                   (uint8_t)((filter(references.top, center) +
                              filter(references.left, center)) >> 1U));
        }
    }

    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_DOWN_RIGHT_8X8, prediction) == CAVS_OK);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x) {
            const uint8_t *samples;
            unsigned distance;
            uint8_t expected;
            if (x == y) {
                expected = (uint8_t)((references.left[1] +
                    2U * references.top[0] + references.top[1] + 2U) >> 2U);
            } else {
                samples = x > y ? references.top : references.left;
                distance = x > y ? x - y : y - x;
                expected = (uint8_t)((samples[distance + 1U] +
                    2U * samples[distance] + samples[distance - 1U] + 2U) >> 2U);
            }
            assert(prediction[y * 8U + x] == expected);
        }
    }
}

static void test_dc_availability(void) {
    cavs_intra_references_8x8 references = make_references();
    uint8_t prediction[64];
    unsigned x;
    unsigned y;
    references.left_available = 0U;
    references.top_available = DC_REFERENCES & ~UINT32_C(1);
    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_DC_8X8, prediction) == CAVS_OK);
    for (y = 0U; y < 8U; ++y)
        for (x = 0U; x < 8U; ++x)
            assert(prediction[y * 8U + x] == 128U);

    references.top_available = DC_REFERENCES;
    references.left_available = UINT32_C(1);
    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_DC_8X8, prediction) == CAVS_OK);
    for (y = 0U; y < 8U; ++y)
        for (x = 0U; x < 8U; ++x)
            assert(prediction[y * 8U + x] == filter(references.top, x + 1U));

    references = make_references();
    references.top_available &= ~(UINT32_C(1) << 8U);
    memset(prediction, 0xa5, sizeof(prediction));
    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_VERTICAL_8X8, prediction) ==
           CAVS_ERR_CORRUPT_BITSTREAM);
    for (x = 0U; x < 64U; ++x) assert(prediction[x] == 0xa5U);
}

static void reference_plane(const cavs_intra_references_8x8 *references,
                            uint8_t prediction[64]) {
    int horizontal = 0;
    int vertical = 0;
    int a;
    int b;
    int c;
    unsigned index;
    unsigned x;
    unsigned y;
    for (index = 0U; index < 4U; ++index) {
        horizontal += (int)(index + 1U) *
            ((int)references->top[5U + index] - references->top[3U - index]);
        vertical += (int)(index + 1U) *
            ((int)references->left[5U + index] - references->left[3U - index]);
    }
    a = ((int)references->top[8] + references->left[8]) << 4;
    b = (int)floor_shift(17 * (int64_t)horizontal + 16, 5U);
    c = (int)floor_shift(17 * (int64_t)vertical + 16, 5U);
    for (y = 0U; y < 8U; ++y) {
        for (x = 0U; x < 8U; ++x) {
            int64_t value = a + ((int)x - 3) * (int64_t)b +
                            ((int)y - 3) * (int64_t)c + 16;
            prediction[y * 8U + x] = clip_sample(floor_shift(value, 5U));
        }
    }
}

static void test_chroma_modes(void) {
    cavs_intra_references_8x8 references = make_references();
    uint8_t expected[64];
    uint8_t prediction[64];
    unsigned index;
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_DC_8X8, prediction) == CAVS_OK);
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_HORIZONTAL_8X8, prediction) == CAVS_OK);
    for (index = 0U; index < 64U; ++index)
        assert(prediction[index] == references.left[index / 8U + 1U]);
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_VERTICAL_8X8, prediction) == CAVS_OK);
    for (index = 0U; index < 64U; ++index)
        assert(prediction[index] == references.top[index % 8U + 1U]);
    reference_plane(&references, expected);
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_PLANE_8X8, prediction) == CAVS_OK);
    assert(memcmp(prediction, expected, sizeof(prediction)) == 0);

    for (index = 0U; index < 17U; ++index) {
        references.top[index] = 100U;
        references.left[index] = 100U;
    }
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_PLANE_8X8, prediction) == CAVS_OK);
    for (index = 0U; index < 64U; ++index) assert(prediction[index] == 100U);

    for (index = 0U; index < 17U; ++index) {
        references.top[index] = index < 4U ? 255U : 0U;
        references.left[index] = index < 4U ? 255U : 0U;
    }
    reference_plane(&references, expected);
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_PLANE_8X8, prediction) == CAVS_OK);
    assert(memcmp(prediction, expected, sizeof(prediction)) == 0);
    assert(prediction[0] == 254U && prediction[63] == 0U);

    for (index = 0U; index < 17U; ++index) {
        references.top[index] = index < 4U ? 0U : 255U;
        references.left[index] = index < 4U ? 0U : 255U;
    }
    reference_plane(&references, expected);
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_PLANE_8X8, prediction) == CAVS_OK);
    assert(memcmp(prediction, expected, sizeof(prediction)) == 0);
    assert(prediction[63] == 255U);
}

static void test_invalid_references(void) {
    cavs_intra_references_8x8 references = make_references();
    uint8_t prediction[64];
    unsigned mode;
    references.left[0] = (uint8_t)(references.top[0] + 1U);
    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_DC_8X8, prediction) == CAVS_ERR_INVALID_ARGUMENT);
    references = make_references();
    references.top_available |= UINT32_C(1) << 20U;
    assert(cavs_predict_intra_chroma_8x8(&references,
               CAVS_INTRA_CHROMA_DC_8X8, prediction) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_predict_intra_luma_8x8(NULL,
               CAVS_INTRA_LUMA_DC_8X8, prediction) == CAVS_ERR_INVALID_ARGUMENT);
    references = make_references();
    assert(cavs_predict_intra_luma_8x8(&references,
               (cavs_intra_luma_mode_8x8)5, prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_predict_intra_luma_8x8(&references,
               (cavs_intra_luma_mode_8x8)-1, prediction) ==
           CAVS_ERR_INVALID_ARGUMENT);

    memset(&references, 0, sizeof(references));
    for (mode = 0U; mode <= 4U; ++mode) {
        if (mode == CAVS_INTRA_LUMA_DC_8X8) continue;
        assert(cavs_predict_intra_luma_8x8(
                   &references, (cavs_intra_luma_mode_8x8)mode,
                   prediction) == CAVS_ERR_CORRUPT_BITSTREAM);
    }
    for (mode = 1U; mode <= 3U; ++mode) {
        assert(cavs_predict_intra_chroma_8x8(
                   &references, (cavs_intra_chroma_mode_8x8)mode,
                   prediction) == CAVS_ERR_CORRUPT_BITSTREAM);
    }
}

static void test_sample_reconstruction(void) {
    uint8_t forward[64];
    uint8_t backward[64];
    uint8_t output[64];
    int16_t residual[64];
    unsigned index;
    for (index = 0U; index < 64U; ++index) {
        forward[index] = (uint8_t)(index * 4U);
        backward[index] = (uint8_t)(255U - index * 4U);
        residual[index] = index == 0U ? -200 : index == 63U ? 200 : 1;
    }
    assert(cavs_reconstruct_samples_8x8(forward, NULL, residual, output) == CAVS_OK);
    assert(output[0] == 0U && output[1] == 5U && output[63] == 255U);
    assert(cavs_reconstruct_samples_8x8(forward, backward, residual, output) ==
           CAVS_OK);
    assert(output[1] == 129U);
    assert(cavs_reconstruct_samples_8x8(NULL, backward, residual, output) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_reconstruct_samples_8x8(forward, backward, NULL, output) ==
           CAVS_ERR_INVALID_ARGUMENT);
}

static void test_prediction_residual_pipeline(void) {
    cavs_intra_references_8x8 references;
    int32_t coefficients[64];
    int16_t residual[64];
    uint8_t prediction[64];
    uint8_t reconstructed[64];
    unsigned index;
    memset(&references, 0, sizeof(references));
    memset(coefficients, 0, sizeof(coefficients));
    coefficients[0] = 16;
    assert(cavs_predict_intra_luma_8x8(&references,
               CAVS_INTRA_LUMA_DC_8X8, prediction) == CAVS_OK);
    assert(cavs_inverse_transform_8x8(coefficients, residual) == CAVS_OK);
    assert(cavs_reconstruct_samples_8x8(prediction, NULL, residual,
                                        reconstructed) == CAVS_OK);
    for (index = 0U; index < 64U; ++index)
        assert(reconstructed[index] == 129U);
}

void test_prediction(void) {
    test_luma_modes();
    test_dc_availability();
    test_chroma_modes();
    test_invalid_references();
    test_sample_reconstruction();
    test_prediction_residual_pipeline();
}

#undef DC_REFERENCES
#undef ALL_REFERENCES
