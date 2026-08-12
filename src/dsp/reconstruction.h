/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Scalar sample reconstruction from GB/T 20090.2-2013 9.11 and
 * GB/T 20090.16-2016 9.10.
 */
#ifndef CAVS_DSP_RECONSTRUCTION_H
#define CAVS_DSP_RECONSTRUCTION_H

#include <cavs/cavs.h>
#include <stdint.h>

/* Averages bidirectional prediction when backward is non-NULL, then clips. */
cavs_result cavs_dsp_reconstruct_samples_8x8_c(
    const uint8_t forward[64], const uint8_t backward[64],
    const int16_t residual[64], uint8_t reconstructed[64]);

/* Adds one inverse-transform residual block to an existing prediction. */
void cavs_dsp_add_residual_8x8_c(
    const uint8_t prediction[64], const int16_t residual[64],
    uint8_t reconstructed[64]);

#endif
