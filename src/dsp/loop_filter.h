/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Scalar in-loop deblocking pixel kernels from GB/T 20090.16-2016
 * 9.11.4-9.11.5 and GB/T 20090.2-2013 9.12.4-9.12.5.
 */
#ifndef CAVS_DSP_LOOP_FILTER_H
#define CAVS_DSP_LOOP_FILTER_H

#include <stddef.h>
#include <stdint.h>

/* alpha, beta, clipping, and boundary strength are derived by codec code. */
void cavs_dsp_loop_filter_segment_c(
    uint8_t *q0, ptrdiff_t normal_step, ptrdiff_t along_step,
    unsigned length, uint8_t strength, uint8_t alpha, uint8_t beta,
    uint8_t clipping, int chroma);

#endif
