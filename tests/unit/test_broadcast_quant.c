/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 9.9 weighted-quantization vectors. The expected values
 * below are derived from the PDF's parameter vectors and selector matrices;
 * they are not copied from a decoder implementation.
 */
#include "codec/broadcast_quant.h"
#include "codec/broadcast_reconstruction.h"
#include "test.h"
#include <string.h>

static void test_default_model(void) {
    static const uint8_t expected[8] = { 128U, 128U, 128U, 116U,
                                         116U, 116U, 128U, 128U };
    int8_t delta[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t matrix[64];
    unsigned index;
    TEST_CHECK(cavs_broadcast_build_weight_matrix(0U, 0U, delta, delta,
                                                   matrix) == CAVS_OK);
    for (index = 0U; index < 8U; ++index)
        TEST_CHECK(matrix[index] == expected[index]);
    TEST_CHECK(matrix[16] == 128U && matrix[18] == 106U &&
               matrix[20] == 98U && matrix[63] == 128U);
}

static void test_parameter_sets_and_models(void) {
    int8_t delta1[6] = { 1, 2, 3, 4, 5, 6 };
    int8_t delta2[6] = { -1, -2, -3, -4, -5, -6 };
    uint8_t matrix[64];
    TEST_CHECK(cavs_broadcast_build_weight_matrix(1U, 1U, delta1, delta2,
                                                   matrix) == CAVS_OK);
    TEST_CHECK(matrix[0] == 136U && matrix[3] == 165U && matrix[6] == 219U);
    TEST_CHECK(cavs_broadcast_build_weight_matrix(2U, 2U, delta1, delta2,
                                                   matrix) == CAVS_OK);
    TEST_CHECK(matrix[0] == 127U && matrix[3] == 111U && matrix[5] == 112U);
}

static void test_bounds_and_atomicity(void) {
    int8_t delta1[6] = { 0, 0, 0, 0, 0, 0 };
    int8_t delta2[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t matrix[64];
    uint8_t unchanged[64];
    memset(matrix, 0x5a, sizeof(matrix));
    memcpy(unchanged, matrix, sizeof(matrix));
    delta1[0] = 120;
    TEST_CHECK(cavs_broadcast_build_weight_matrix(1U, 0U, delta1, delta2,
                                                   matrix) == CAVS_OK);
    TEST_CHECK(matrix[0] == 255U);
    delta1[0] = 121;
    memcpy(matrix, unchanged, sizeof(matrix));
    TEST_CHECK(cavs_broadcast_build_weight_matrix(1U, 0U, delta1, delta2,
                                                   matrix) == CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(memcmp(matrix, unchanged, sizeof(matrix)) == 0);
    TEST_CHECK(cavs_broadcast_build_weight_matrix(3U, 0U, delta1, delta2,
                                                   matrix) == CAVS_ERR_INVALID_ARGUMENT);
    TEST_CHECK(cavs_broadcast_build_weight_matrix(0U, 3U, delta1, delta2,
                                                   matrix) == CAVS_ERR_INVALID_ARGUMENT);
}

static void test_weighted_inverse_quantization(void) {
    int16_t quant[64] = { 0 };
    uint8_t matrix[64];
    int32_t coefficients[64];
    int32_t expected[64];
    int32_t unchanged[64];
    int8_t delta[6] = { 0, 0, 0, 0, 0, 0 };
    unsigned index;
    TEST_CHECK(cavs_broadcast_build_weight_matrix(0U, 0U, delta, delta,
                                                   matrix) == CAVS_OK);
    quant[0] = 1;
    quant[1] = -1;
    TEST_CHECK(cavs_broadcast_inverse_quantize_8x8_weighted(
                   quant, 0U, matrix, coefficients) == CAVS_OK);
    TEST_CHECK(cavs_broadcast_inverse_quantize_8x8(
                   quant, 0U, expected) == CAVS_OK);
    TEST_CHECK(memcmp(coefficients, expected, sizeof(coefficients)) == 0);
    memset(matrix, 128, sizeof(matrix));
    quant[0] = 17;
    quant[1] = -17;
    TEST_CHECK(cavs_broadcast_inverse_quantize_8x8_weighted(
                   quant, 63U, matrix, coefficients) == CAVS_OK);
    TEST_CHECK(cavs_broadcast_inverse_quantize_8x8(
                   quant, 63U, expected) == CAVS_OK);
    TEST_CHECK(memcmp(coefficients, expected, sizeof(coefficients)) == 0);
    for (index = 0U; index < 64U; ++index) {
        unchanged[index] = INT32_C(0x5a5a5a5a);
    }
    memcpy(coefficients, unchanged, sizeof(coefficients));
    quant[63] = INT16_C(2047);
    TEST_CHECK(cavs_broadcast_inverse_quantize_8x8_weighted(
                   quant, 63U, matrix, coefficients) == CAVS_ERR_CORRUPT_BITSTREAM);
    TEST_CHECK(memcmp(coefficients, unchanged, sizeof(coefficients)) == 0);
}

void test_broadcast_quant(void) {
    test_default_model();
    test_parameter_sets_and_models();
    test_bounds_and_atomicity();
    test_weighted_inverse_quantization();
}
