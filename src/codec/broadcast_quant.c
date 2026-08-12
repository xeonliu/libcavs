/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 9.9 and the WeightingQuantModel matrices shown there.
 * This module derives the matrix from parsed syntax and owns no decoder state.
 */
#include "codec/broadcast_quant.h"
#include <stddef.h>

/* GB/T 20090.16-2016 9.9 default and base parameter vectors. */
static const uint8_t parameter_default[6] = { 128U, 98U, 106U, 116U, 116U, 128U };
static const uint8_t parameter_base1[6] = { 135U, 143U, 143U, 160U, 160U, 213U };
static const uint8_t parameter_base2[6] = { 128U, 98U, 106U, 116U, 116U, 128U };

/*
 * GB/T 20090.16-2016 9.9 enumerates these three 8x8 distributions by the
 * six wqP entries (0..5). The table stores the enumerated selector, not a
 * decoder-specific weight matrix.
 */
static const uint8_t model_selector[3][64] = {
    {
        0,0,0,4,4,4,5,5, 0,0,3,3,3,3,5,5,
        0,3,2,2,1,1,5,5, 4,3,2,2,1,5,5,5,
        4,3,1,1,5,5,5,5, 4,3,1,5,5,5,5,5,
        5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5
    },
    {
        0,0,0,4,4,4,5,5, 0,0,4,4,4,4,5,5,
        0,3,2,2,2,1,5,5, 3,3,2,2,1,5,5,5,
        3,3,2,1,5,5,5,5, 3,3,1,5,5,5,5,5,
        5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5
    },
    {
        0,0,0,4,4,3,5,5, 0,0,4,4,3,2,5,5,
        0,4,4,3,2,1,5,5, 4,4,3,2,1,5,5,5,
        4,3,2,1,5,5,5,5, 3,2,1,5,5,5,5,5,
        5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5
    }
};

/* Adds a signed syntax delta with the normative 0..255 parameter check. */
static int add_parameter(uint8_t base, int8_t delta, uint8_t *value) {
    int result = (int)base + (int)delta;
    if (result < 0 || result > 255) return 0;
    *value = (uint8_t)result;
    return 1;
}

cavs_result cavs_broadcast_build_weight_matrix(
    uint8_t parameter_index, uint8_t model,
    const int8_t delta1[6], const int8_t delta2[6], uint8_t matrix[64]) {
    uint8_t parameters[6];
    unsigned index;
    if (delta1 == NULL || delta2 == NULL || matrix == NULL ||
        parameter_index > 2U || model > 2U)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (index = 0U; index < 6U; ++index) {
        int8_t delta = parameter_index == 1U ? delta1[index] :
            (parameter_index == 2U ? delta2[index] : 0);
        uint8_t base = parameter_index == 0U ? parameter_default[index] :
            (parameter_index == 1U ? parameter_base1[index] :
                                     parameter_base2[index]);
        if (!add_parameter(base, delta, &parameters[index]))
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    for (index = 0U; index < 64U; ++index)
        matrix[index] = parameters[model_selector[model][index]];
    return CAVS_OK;
}
