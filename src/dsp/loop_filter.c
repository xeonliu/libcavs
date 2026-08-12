/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 9.11.4-9.11.5, Figures 32-34 and Tables 64-65,
 * and GB/T 20090.2-2013 9.12.4-9.12.5, Figures 44-46 and Tables 73-74.
 */
#include "dsp/loop_filter.h"

static int clip_int(int minimum, int maximum, int value) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static uint8_t clip_sample(int value) {
    return (uint8_t)clip_int(0, 255, value);
}

static int difference(int first, int second) {
    return first >= second ? first - second : second - first;
}

static int floor_shift(int value, unsigned shift) {
    int divisor = 1 << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

/* Implements the Bs=2 luma process in Part 16 9.11.4 / Part 2 9.12.4. */
static void filter_strong_luma(uint8_t *q0, ptrdiff_t step,
                               int alpha, int beta) {
    int p2 = q0[-3 * step];
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int q2 = q0[2 * step];
    int sum = p0 + q0_value + 2;
    int inner_alpha = (alpha >> 2) + 2;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    if (difference(p2, p0) < beta &&
        difference(p0, q0_value) < inner_alpha) {
        q0[-step] = (uint8_t)((p1 + p0 + sum) >> 2);
        q0[-2 * step] = (uint8_t)((2 * p1 + sum) >> 2);
    } else {
        q0[-step] = (uint8_t)((2 * p1 + sum) >> 2);
    }
    if (difference(q2, q0_value) < beta &&
        difference(q0_value, p0) < inner_alpha) {
        q0[0] = (uint8_t)((q1 + q0_value + sum) >> 2);
        q0[step] = (uint8_t)((2 * q1 + sum) >> 2);
    } else {
        q0[0] = (uint8_t)((2 * q1 + sum) >> 2);
    }
}

/* Implements the Bs=2 chroma process in Part 16 9.11.4 / Part 2 9.12.4. */
static void filter_strong_chroma(uint8_t *q0, ptrdiff_t step,
                                 int alpha, int beta) {
    int p2 = q0[-3 * step];
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int q2 = q0[2 * step];
    int sum = p0 + q0_value + 2;
    int inner_alpha = (alpha >> 2) + 2;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    if (difference(p2, p0) < beta &&
        difference(p0, q0_value) < inner_alpha)
        q0[-step] = (uint8_t)((p1 + p0 + sum) >> 2);
    else
        q0[-step] = (uint8_t)((2 * p1 + sum) >> 2);
    if (difference(q2, q0_value) < beta &&
        difference(q0_value, p0) < inner_alpha)
        q0[0] = (uint8_t)((q1 + q0_value + sum) >> 2);
    else
        q0[0] = (uint8_t)((2 * q1 + sum) >> 2);
}

/* Implements the Bs=1 luma process in Part 16 9.11.5 / Part 2 9.12.5. */
static void filter_normal_luma(uint8_t *q0, ptrdiff_t step,
                               int alpha, int beta, int clipping) {
    int p2 = q0[-3 * step];
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int q2 = q0[2 * step];
    int delta;
    int filtered_p0;
    int filtered_q0;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    delta = clip_int(-clipping, clipping,
                     floor_shift((q0_value - p0) * 3 + p1 - q1 + 4, 3U));
    filtered_p0 = clip_sample(p0 + delta);
    filtered_q0 = clip_sample(q0_value - delta);
    q0[-step] = (uint8_t)filtered_p0;
    q0[0] = (uint8_t)filtered_q0;
    if (difference(p2, p0) < beta) {
        delta = clip_int(
            -clipping, clipping,
            floor_shift((filtered_p0 - p1) * 3 + p2 - filtered_q0 + 4, 3U));
        q0[-2 * step] = clip_sample(p1 + delta);
    }
    if (difference(q2, q0_value) < beta) {
        delta = clip_int(
            -clipping, clipping,
            floor_shift((q1 - filtered_q0) * 3 + filtered_p0 - q2 + 4, 3U));
        q0[step] = clip_sample(q1 - delta);
    }
}

/* Implements the Bs=1 chroma process in Part 16 9.11.5 / Part 2 9.12.5. */
static void filter_normal_chroma(uint8_t *q0, ptrdiff_t step,
                                 int alpha, int beta, int clipping) {
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int delta;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    delta = clip_int(-clipping, clipping,
                     floor_shift((q0_value - p0) * 3 + p1 - q1 + 4, 3U));
    q0[-step] = clip_sample(p0 + delta);
    q0[0] = clip_sample(q0_value - delta);
}

/* Applies one normative kernel to a contiguous boundary segment. */
void cavs_dsp_loop_filter_segment_c(
    uint8_t *q0, ptrdiff_t normal_step, ptrdiff_t along_step,
    unsigned length, uint8_t strength, uint8_t alpha, uint8_t beta,
    uint8_t clipping, int chroma) {
    unsigned index;
    if (strength == 0U) return;
    for (index = 0U; index < length; ++index) {
        uint8_t *sample = q0 + (ptrdiff_t)index * along_step;
        if (strength == 2U) {
            if (chroma != 0)
                filter_strong_chroma(sample, normal_step, alpha, beta);
            else
                filter_strong_luma(sample, normal_step, alpha, beta);
        } else if (chroma != 0) {
            filter_normal_chroma(sample, normal_step, alpha, beta, clipping);
        } else {
            filter_normal_luma(sample, normal_step, alpha, beta, clipping);
        }
    }
}
