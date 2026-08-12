/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.4.6, 7.4.11 and 9.3. This module contains only the
 * stateless weighted-prediction arithmetic and syntax-index rules.
 */
#include "codec/broadcast_weighting.h"
#include <limits.h>

static uint8_t clip_sample(int32_t value) {
    if (value < 0) return 0U;
    if (value > 255) return 255U;
    return (uint8_t)value;
}

cavs_result cavs_broadcast_weight_sample(
    uint8_t sample, uint8_t scale, int8_t shift, uint8_t *weighted) {
    int32_t value;
    if (weighted == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    value = ((int32_t)sample * (int32_t)scale + 16) >> 5;
    value += (int32_t)shift;
    *weighted = clip_sample(value);
    return CAVS_OK;
}

cavs_result cavs_broadcast_weight_block(
    uint8_t *samples, size_t stride, size_t x, size_t y,
    size_t width, size_t height, uint8_t scale, int8_t shift) {
    size_t row;
    if (samples == NULL || stride == 0U || width == 0U || height == 0U ||
        x > SIZE_MAX - width || y > SIZE_MAX - height || x + width > stride)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (row = 0U; row < height; ++row) {
        size_t column;
        for (column = 0U; column < width; ++column) {
            uint8_t weighted;
            cavs_result result = cavs_broadcast_weight_sample(
                samples[(y + row) * stride + x + column], scale, shift,
                &weighted);
            if (result != CAVS_OK) return result;
            samples[(y + row) * stride + x + column] = weighted;
        }
    }
    return CAVS_OK;
}

cavs_result cavs_broadcast_weight_parameter_index(
    cavs_picture_type picture_type, cavs_prediction_direction direction,
    uint8_t reference_index, uint8_t *parameter_index) {
    unsigned index;
    if (parameter_index == NULL || picture_type > CAVS_PICTURE_B ||
        direction > CAVS_PRED_BACKWARD)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (picture_type == CAVS_PICTURE_B) {
        index = (unsigned)reference_index * 2U +
            (direction == CAVS_PRED_BACKWARD ? 1U : 0U);
    } else {
        index = reference_index;
    }
    if (index >= CAVS_BROADCAST_WEIGHT_COUNT)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *parameter_index = (uint8_t)index;
    return CAVS_OK;
}

cavs_result cavs_broadcast_should_weight(
    uint8_t slice_weighting_flag, uint8_t mb_weighting_flag,
    uint8_t weighting_prediction, uint8_t is_intra, uint8_t *enabled) {
    if (enabled == NULL || slice_weighting_flag > 1U ||
        mb_weighting_flag > 1U || weighting_prediction > 1U ||
        is_intra > 1U)
        return CAVS_ERR_INVALID_ARGUMENT;
    /* Part 16 9.3 applies weighted prediction only to non-I macroblocks. */
    *enabled = (uint8_t)(is_intra == 0U && slice_weighting_flag != 0U &&
        (mb_weighting_flag == 0U || weighting_prediction != 0U));
    return CAVS_OK;
}
