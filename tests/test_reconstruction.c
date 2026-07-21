/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013 baseline 8x8 reconstruction-math tests.
 */
#include "reconstruction.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

static int64_t reference_floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static int64_t reference_clip(int64_t value) {
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return value;
}

static void test_inverse_scan(void) {
    static const int32_t expected_frame[64] = {
         0,  1,  5,  6, 14, 15, 27, 28,
         2,  4,  7, 13, 16, 26, 29, 42,
         3,  8, 12, 17, 25, 30, 41, 43,
         9, 11, 18, 24, 31, 40, 44, 53,
        10, 19, 23, 32, 39, 45, 52, 54,
        20, 22, 33, 38, 46, 51, 55, 60,
        21, 34, 37, 47, 50, 56, 59, 61,
        35, 36, 48, 49, 57, 58, 62, 63
    };
    static const int32_t expected_field[64] = {
         0,  3, 11, 16, 22, 32, 38, 55,
         1,  6, 12, 20, 25, 33, 42, 57,
         2,  7, 15, 21, 28, 37, 43, 58,
         4, 10, 19, 27, 31, 39, 47, 59,
         5, 14, 24, 30, 36, 44, 50, 60,
         8, 17, 26, 35, 41, 48, 52, 61,
         9, 18, 29, 40, 46, 51, 54, 62,
        13, 23, 34, 45, 49, 53, 56, 63
    };
    int32_t values[64];
    int32_t matrix[64];
    unsigned index;
    for (index = 0U; index < 64U; ++index) values[index] = (int32_t)index;
    assert(cavs_inverse_scan_8x8(values, CAVS_SCAN_8X8_FRAME, matrix) == CAVS_OK);
    assert(memcmp(matrix, expected_frame, sizeof(matrix)) == 0);
    assert(cavs_inverse_scan_8x8(values, CAVS_SCAN_8X8_FIELD, matrix) == CAVS_OK);
    assert(memcmp(matrix, expected_field, sizeof(matrix)) == 0);
    assert(cavs_inverse_scan_8x8(values, CAVS_SCAN_8X8_FRAME, values) == CAVS_OK);
    assert(memcmp(values, expected_frame, sizeof(values)) == 0);
    assert(cavs_inverse_scan_8x8(NULL, CAVS_SCAN_8X8_FRAME, matrix) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_inverse_scan_8x8(matrix, (cavs_scan_mode_8x8)2, values) ==
           CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_inverse_scan_8x8(matrix, (cavs_scan_mode_8x8)-1, values) ==
           CAVS_ERR_INVALID_ARGUMENT);
}

static void test_chroma_qp(void) {
    static const uint8_t expected[64] = {
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 42, 43, 43, 44, 44,
        45, 45, 46, 46, 47, 47, 48, 48, 48, 49, 49, 49, 50, 50, 50, 51
    };
    uint8_t mapped = 0U;
    unsigned qp;
    for (qp = 0U; qp < 64U; ++qp) {
        assert(cavs_map_chroma_qp((uint8_t)qp, 0, &mapped) == CAVS_OK);
        assert(mapped == expected[qp]);
    }
    assert(cavs_map_chroma_qp(40U, 3, &mapped) == CAVS_OK && mapped == 42U);
    assert(cavs_map_chroma_qp(63U, 1, &mapped) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(cavs_map_chroma_qp(0U, -1, &mapped) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(cavs_map_chroma_qp(64U, 0, &mapped) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_map_chroma_qp(0U, 0, NULL) == CAVS_ERR_INVALID_ARGUMENT);
}

static void test_inverse_quantization(void) {
    static const uint16_t dequant[64] = {
        32768,36061,38968,42495,46341,50535,55437,60424,
        32932,35734,38968,42495,46177,50535,55109,59933,
        65535,35734,38968,42577,46341,50617,55027,60097,
        32809,35734,38968,42454,46382,50576,55109,60056,
        65535,35734,38968,42495,46320,50515,55109,60076,
        65535,35744,38968,42495,46341,50535,55099,60087,
        65535,35734,38973,42500,46341,50535,55109,60097,
        32771,35734,38965,42497,46341,50535,55109,60099
    };
    static const uint8_t shifts[64] = {
        14,14,14,14,14,14,14,14,13,13,13,13,13,13,13,13,
        13,12,12,12,12,12,12,12,11,11,11,11,11,11,11,11,
        11,10,10,10,10,10,10,10,10,9,9,9,9,9,9,9,
        9,8,8,8,8,8,8,8,7,7,7,7,7,7,7,7
    };
    int32_t quant[64];
    int32_t predicted[64];
    int32_t output[64];
    int32_t unchanged[64];
    uint8_t weights[64];
    unsigned qp;
    unsigned index;
    memset(predicted, 0, sizeof(predicted));
    memset(weights, 128, sizeof(weights));
    for (qp = 0U; qp < 64U; ++qp) {
        int64_t expected;
        for (index = 0U; index < 64U; ++index)
            quant[index] = index == 0U ? 1 : -1;
        assert(cavs_inverse_quantize_8x8(quant, predicted, weights,
                                        (uint8_t)qp, output) == CAVS_OK);
        expected = reference_floor_shift(
            (int64_t)dequant[qp] + (INT64_C(1) << (shifts[qp] - 1U)),
            shifts[qp]);
        assert(output[0] == expected);
        expected = reference_floor_shift(
            reference_floor_shift((int64_t)-16 * dequant[qp], 4U) +
                (INT64_C(1) << (shifts[qp] - 1U)), shifts[qp]);
        assert(output[1] == expected);
    }
    memset(quant, 0, sizeof(quant));
    memset(predicted, 0, sizeof(predicted));
    memset(weights, 128, sizeof(weights));
    quant[0] = 17;
    predicted[0] = 17;
    assert(cavs_inverse_quantize_8x8(quant, predicted, weights, 63U,
                                    quant) == CAVS_OK);
    assert(quant[0] == 0);

    memset(quant, 0, sizeof(quant));
    quant[63] = 2048;
    memset(unchanged, 0x5a, sizeof(unchanged));
    memcpy(output, unchanged, sizeof(output));
    assert(cavs_inverse_quantize_8x8(quant, predicted, weights, 0U,
                                    output) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(memcmp(output, unchanged, sizeof(output)) == 0);
    quant[63] = 2047;
    assert(cavs_inverse_quantize_8x8(quant, predicted, weights, 63U,
                                    output) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(cavs_inverse_quantize_8x8(quant, predicted, weights, 64U,
                                    output) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_inverse_quantize_8x8(NULL, predicted, weights, 0U,
                                    output) == CAVS_ERR_INVALID_ARGUMENT);
}

static void reference_transform(const int32_t input[64], int16_t output[64]) {
    static const int8_t t[64] = {
         8,10,10, 9, 8, 6, 4, 2,  8, 9, 4,-2,-8,-10,-10,-6,
         8, 6,-4,-10,-8, 2,10, 9,  8, 2,-10,-6,8,9,-4,-10,
         8,-2,-10, 6, 8,-9,-4,10,  8,-6,-4,10,-8,-2,10,-9,
         8,-9, 4, 2,-8,10,-10,6,  8,-10,10,-9,8,-6,4,-2
    };
    int64_t horizontal[64];
    unsigned row, column, term;
    for (row = 0U; row < 8U; ++row) {
        for (column = 0U; column < 8U; ++column) {
            int64_t sum = 0;
            for (term = 0U; term < 8U; ++term)
                sum += (int64_t)input[row * 8U + term] *
                       t[column * 8U + term];
            horizontal[row * 8U + column] =
                reference_floor_shift(reference_clip(sum + 4), 3U);
        }
    }
    for (row = 0U; row < 8U; ++row) {
        for (column = 0U; column < 8U; ++column) {
            int64_t sum = 0;
            for (term = 0U; term < 8U; ++term)
                sum += (int64_t)t[row * 8U + term] *
                       horizontal[term * 8U + column];
            output[row * 8U + column] = (int16_t)reference_floor_shift(
                reference_clip(sum + 64), 7U);
        }
    }
}

static void test_inverse_transform(void) {
    int32_t coefficients[64];
    int16_t expected[64];
    int16_t output[64];
    int16_t unchanged[64];
    unsigned index;
    memset(coefficients, 0, sizeof(coefficients));
    assert(cavs_inverse_transform_8x8(coefficients, output) == CAVS_OK);
    for (index = 0U; index < 64U; ++index) assert(output[index] == 0);
    coefficients[0] = 16;
    assert(cavs_inverse_transform_8x8(coefficients, output) == CAVS_OK);
    for (index = 0U; index < 64U; ++index) assert(output[index] == 1);
    coefficients[0] = -16;
    assert(cavs_inverse_transform_8x8(coefficients, output) == CAVS_OK);
    for (index = 0U; index < 64U; ++index) assert(output[index] == -1);

    for (index = 0U; index < 64U; ++index)
        coefficients[index] = (int32_t)((index * 263U) % 4096U) - 2048;
    reference_transform(coefficients, expected);
    assert(cavs_inverse_transform_8x8(coefficients, output) == CAVS_OK);
    assert(memcmp(output, expected, sizeof(output)) == 0);

    for (index = 0U; index < 64U; ++index) coefficients[index] = 8191;
    reference_transform(coefficients, expected);
    assert(cavs_inverse_transform_8x8(coefficients, output) == CAVS_OK);
    assert(memcmp(output, expected, sizeof(output)) == 0);
    assert(output[0] == 255);

    for (index = 0U; index < 64U; ++index) coefficients[index] = -8192;
    reference_transform(coefficients, expected);
    assert(cavs_inverse_transform_8x8(coefficients, output) == CAVS_OK);
    assert(memcmp(output, expected, sizeof(output)) == 0);
    assert(output[0] == -256);

    memset(coefficients, 0, sizeof(coefficients));
    coefficients[63] = 8192;
    memset(unchanged, 0x3c, sizeof(unchanged));
    memcpy(output, unchanged, sizeof(output));
    assert(cavs_inverse_transform_8x8(coefficients, output) ==
           CAVS_ERR_CORRUPT_BITSTREAM);
    assert(memcmp(output, unchanged, sizeof(output)) == 0);
    assert(cavs_inverse_transform_8x8(NULL, output) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_inverse_transform_8x8(coefficients, NULL) ==
           CAVS_ERR_INVALID_ARGUMENT);
}

static void test_reconstruction_pipeline(void) {
    int32_t scan[64];
    int32_t matrix[64];
    int32_t predicted[64];
    int32_t coefficients[64];
    int16_t residual[64];
    uint8_t weights[64];
    unsigned index;
    memset(scan, 0, sizeof(scan));
    memset(predicted, 0, sizeof(predicted));
    memset(weights, 128, sizeof(weights));
    scan[0] = 8;
    assert(cavs_inverse_scan_8x8(scan, CAVS_SCAN_8X8_FRAME, matrix) == CAVS_OK);
    assert(cavs_inverse_quantize_8x8(matrix, predicted, weights, 0U,
                                    coefficients) == CAVS_OK);
    assert(coefficients[0] == 16);
    assert(cavs_inverse_transform_8x8(coefficients, residual) == CAVS_OK);
    for (index = 0U; index < 64U; ++index) assert(residual[index] == 1);
}

void test_reconstruction(void) {
    test_inverse_scan();
    test_chroma_qp();
    test_inverse_quantization();
    test_inverse_transform();
    test_reconstruction_pipeline();
}
