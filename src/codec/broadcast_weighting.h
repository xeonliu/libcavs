/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.4.6, 7.4.11 and 9.3 weighted prediction.
 */
#ifndef CAVS_BROADCAST_WEIGHTING_H
#define CAVS_BROADCAST_WEIGHTING_H

#include "codec/macroblock.h"
#include "codec/syntax.h"
#include <stddef.h>
#include <stdint.h>

/* Part 16 9.3 uses two parameter slots for each B reference index. */
#define CAVS_BROADCAST_WEIGHT_COUNT CAVS_MAX_WEIGHT_PARAMETERS

/* Applies Clip1(((sample * scale + 16) >> 5) + shift) from Part 16 9.3. */
cavs_result cavs_broadcast_weight_sample(
    uint8_t sample, uint8_t scale, int8_t shift, uint8_t *weighted);

/* Applies the same formula to a rectangular prediction block in place. */
cavs_result cavs_broadcast_weight_block(
    uint8_t *samples, size_t stride, size_t x, size_t y,
    size_t width, size_t height, uint8_t scale, int8_t shift);

/* Maps the syntax reference index to the Part 16 9.3 parameter index. */
cavs_result cavs_broadcast_weight_parameter_index(
    cavs_picture_type picture_type, cavs_prediction_direction direction,
    uint8_t reference_index, uint8_t *parameter_index);

/* Resolves Table 25's slice/MB flags to WeightingPrediction. */
cavs_result cavs_broadcast_should_weight(
    uint8_t slice_weighting_flag, uint8_t mb_weighting_flag,
    uint8_t weighting_prediction, uint8_t is_intra, uint8_t *enabled);

#endif
