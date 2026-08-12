/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013 9.11 and GB/T 20090.16-2016 9.10 sample
 * reconstruction: combine one or two predictions with inverse-transform
 * residuals and clip the result to the 8-bit sample range.
 */
#include "dsp/reconstruction.h"

static uint8_t clip_sample(int value) {
    if (value < 0) return 0U;
    if (value > 255) return 255U;
    return (uint8_t)value;
}

cavs_result cavs_dsp_reconstruct_samples_8x8_c(
    const uint8_t forward[64], const uint8_t backward[64],
    const int16_t residual[64], uint8_t reconstructed[64]) {
    uint8_t parsed[64];
    unsigned index;
    if (forward == NULL || residual == NULL || reconstructed == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (index = 0U; index < 64U; ++index) {
        int prediction = forward[index];
        if (backward != NULL)
            prediction = (prediction + backward[index] + 1) >> 1;
        parsed[index] = clip_sample(prediction + residual[index]);
    }
    for (index = 0U; index < 64U; ++index)
        reconstructed[index] = parsed[index];
    return CAVS_OK;
}

void cavs_dsp_add_residual_8x8_c(
    const uint8_t prediction[64], const int16_t residual[64],
    uint8_t reconstructed[64]) {
    unsigned index;
    for (index = 0U; index < 64U; ++index)
        reconstructed[index] = clip_sample(prediction[index] + residual[index]);
}
