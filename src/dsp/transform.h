/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Scalar inverse-transform kernels from GB/T 20090.2-2013 9.8.2-9.8.3.
 */
#ifndef CAVS_RECONSTRUCTION_H
#define CAVS_RECONSTRUCTION_H

#include <cavs/cavs.h>
#include <stdint.h>

/* Implements the 8-bit form of the 9.8.2 separable inverse transform. */
cavs_result cavs_dsp_inverse_transform_8x8_c(
    const int32_t coefficients[64], int16_t residual[64]);

/* Implements GB/T 20090.2-2013 9.8.3 and Table 71. */
cavs_result cavs_dsp_inverse_transform_4x4_c(
    const int32_t coefficients[16], int16_t residual[16]);

#endif
