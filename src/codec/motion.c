/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 9.4.6.2-9.4.6.3, Figures 18 and 32.
 */
#include "codec/motion.h"
#include <limits.h>

static int motion_range(cavs_luma_motion_precision precision,
                        int32_t *minimum, int32_t *maximum) {
    if (precision == CAVS_LUMA_MOTION_QUARTER) {
        *minimum = -4096;
        *maximum = 4095;
        return 1;
    }
    if (precision == CAVS_LUMA_MOTION_EIGHTH) {
        *minimum = -8192;
        *maximum = 8191;
        return 1;
    }
    return 0;
}

static int vector_in_range(cavs_luma_motion_vector vector,
                           int32_t minimum, int32_t maximum) {
    return vector.x >= minimum && vector.x <= maximum &&
           vector.y >= minimum && vector.y <= maximum;
}

static int candidate_valid(const cavs_motion_candidate *candidate) {
    int active;
    if (candidate->available > 1U || candidate->intra > 1U ||
        candidate->same_direction > 1U || candidate->reference_index < -1 ||
        candidate->reference_index >= 4 || candidate->block_distance > 511U)
        return 0;
    active = candidate->available != 0U && candidate->intra == 0U &&
             candidate->same_direction != 0U;
    return !active ||
           (candidate->reference_index >= 0 && candidate->block_distance != 0U);
}

static cavs_motion_candidate normalized_candidate(
    cavs_motion_candidate candidate) {
    if (candidate.available == 0U || candidate.intra != 0U ||
        candidate.same_direction == 0U) {
        candidate.vector.x = 0;
        candidate.vector.y = 0;
        candidate.block_distance = 1U;
        candidate.reference_index = -1;
    }
    return candidate;
}

static int scale_component(int32_t component, uint16_t source_distance,
                           uint16_t target_distance, int32_t *scaled) {
    uint64_t magnitude;
    uint64_t value;
    if (source_distance == 0U || target_distance == 0U) return 0;
    magnitude = component < 0 ? (uint64_t)(-(int64_t)component)
                              : (uint64_t)component;
    value = (magnitude * target_distance * (512U / source_distance) + 256U) /
            512U;
    if (value > INT32_MAX) return 0;
    *scaled = component < 0 ? -(int32_t)value : (int32_t)value;
    return 1;
}

static int scale_vector(cavs_luma_motion_vector vector,
                        uint16_t source_distance, uint16_t target_distance,
                        cavs_luma_motion_vector *scaled) {
    return scale_component(vector.x, source_distance, target_distance,
                           &scaled->x) &&
           scale_component(vector.y, source_distance, target_distance,
                           &scaled->y);
}

static uint32_t vector_distance(cavs_luma_motion_vector first,
                                cavs_luma_motion_vector second) {
    int64_t dx = (int64_t)first.x - second.x;
    int64_t dy = (int64_t)first.y - second.y;
    uint64_t distance = (uint64_t)(dx < 0 ? -dx : dx) +
                        (uint64_t)(dy < 0 ? -dy : dy);
    return distance > UINT32_MAX ? UINT32_MAX : (uint32_t)distance;
}

static uint32_t median3(uint32_t first, uint32_t second, uint32_t third) {
    if (first > second) {
        uint32_t temporary = first;
        first = second;
        second = temporary;
    }
    if (second > third) second = third;
    return first > second ? first : second;
}

cavs_result cavs_predict_luma_motion(
    const cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT],
    int8_t current_reference_index, uint16_t current_block_distance,
    cavs_motion_partition_position partition,
    cavs_luma_motion_precision precision,
    cavs_luma_motion_vector *prediction) {
    cavs_motion_candidate neighbor[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector scaled[3];
    cavs_luma_motion_vector result;
    int32_t minimum;
    int32_t maximum;
    unsigned active = 0U;
    unsigned active_index = 0U;
    unsigned index;
    uint32_t distance_ab;
    uint32_t distance_bc;
    uint32_t distance_ca;
    uint32_t middle;
    if (candidates == NULL || prediction == NULL ||
        current_reference_index < 0 || current_reference_index >= 4 ||
        current_block_distance == 0U || current_block_distance > 511U ||
        partition < CAVS_MOTION_PARTITION_OTHER ||
        partition > CAVS_MOTION_PARTITION_16X8_BOTTOM ||
        !motion_range(precision, &minimum, &maximum))
        return CAVS_ERR_INVALID_ARGUMENT;
    for (index = 0U; index < CAVS_MOTION_NEIGHBOR_COUNT; ++index) {
        if (!candidate_valid(&candidates[index]))
            return CAVS_ERR_INVALID_ARGUMENT;
        neighbor[index] = normalized_candidate(candidates[index]);
        if (neighbor[index].reference_index >= 0 &&
            (!vector_in_range(neighbor[index].vector, minimum, maximum) ||
             neighbor[index].block_distance == 0U))
            return CAVS_ERR_INVALID_ARGUMENT;
    }
    if (candidates[CAVS_MOTION_NEIGHBOR_C].available == 0U)
        neighbor[CAVS_MOTION_NEIGHBOR_C] = neighbor[CAVS_MOTION_NEIGHBOR_D];

    for (index = 0U; index < 3U; ++index) {
        if (neighbor[index].reference_index >= 0) {
            ++active;
            active_index = index;
        }
    }
    if (active == 1U) {
        result = neighbor[active_index].vector;
    } else if (partition == CAVS_MOTION_PARTITION_8X16_LEFT &&
               neighbor[CAVS_MOTION_NEIGHBOR_A].reference_index ==
                   current_reference_index) {
        result = neighbor[CAVS_MOTION_NEIGHBOR_A].vector;
    } else if (partition == CAVS_MOTION_PARTITION_8X16_RIGHT &&
               neighbor[CAVS_MOTION_NEIGHBOR_C].reference_index ==
                   current_reference_index) {
        result = neighbor[CAVS_MOTION_NEIGHBOR_C].vector;
    } else if (partition == CAVS_MOTION_PARTITION_16X8_TOP &&
               neighbor[CAVS_MOTION_NEIGHBOR_B].reference_index ==
                   current_reference_index) {
        result = neighbor[CAVS_MOTION_NEIGHBOR_B].vector;
    } else if (partition == CAVS_MOTION_PARTITION_16X8_BOTTOM &&
               neighbor[CAVS_MOTION_NEIGHBOR_A].reference_index ==
                   current_reference_index) {
        result = neighbor[CAVS_MOTION_NEIGHBOR_A].vector;
    } else {
        for (index = 0U; index < 3U; ++index) {
            if (!scale_vector(neighbor[index].vector,
                              neighbor[index].block_distance,
                              current_block_distance, &scaled[index]))
                return CAVS_ERR_CORRUPT_BITSTREAM;
        }
        distance_ab = vector_distance(scaled[0], scaled[1]);
        distance_bc = vector_distance(scaled[1], scaled[2]);
        distance_ca = vector_distance(scaled[2], scaled[0]);
        middle = median3(distance_ab, distance_bc, distance_ca);
        if (middle == distance_ab)
            result = scaled[2];
        else if (middle == distance_bc)
            result = scaled[0];
        else
            result = scaled[1];
    }
    *prediction = result;
    return CAVS_OK;
}

cavs_result cavs_decode_luma_motion(
    const cavs_luma_motion_vector *prediction,
    const cavs_luma_motion_vector *difference,
    cavs_luma_motion_precision precision, cavs_luma_motion_vector *motion) {
    cavs_luma_motion_vector decoded;
    int32_t minimum;
    int32_t maximum;
    int64_t x;
    int64_t y;
    if (prediction == NULL || difference == NULL || motion == NULL ||
        !motion_range(precision, &minimum, &maximum) ||
        !vector_in_range(*difference, minimum, maximum))
        return CAVS_ERR_INVALID_ARGUMENT;
    x = (int64_t)prediction->x + difference->x;
    y = (int64_t)prediction->y + difference->y;
    if (x < minimum || x > maximum || y < minimum || y > maximum)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    decoded.x = (int32_t)x;
    decoded.y = (int32_t)y;
    *motion = decoded;
    return CAVS_OK;
}

cavs_result cavs_derive_p_skip_motion(
    const cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT],
    uint16_t default_block_distance, cavs_luma_motion_precision precision,
    cavs_luma_motion_vector *motion) {
    cavs_luma_motion_vector derived;
    cavs_motion_candidate neighbor_a;
    cavs_motion_candidate neighbor_b;
    int32_t minimum;
    int32_t maximum;
    unsigned index;
    if (candidates == NULL || motion == NULL ||
        default_block_distance == 0U || default_block_distance > 511U ||
        !motion_range(precision, &minimum, &maximum))
        return CAVS_ERR_INVALID_ARGUMENT;
    for (index = 0U; index < CAVS_MOTION_NEIGHBOR_COUNT; ++index) {
        if (!candidate_valid(&candidates[index]))
            return CAVS_ERR_INVALID_ARGUMENT;
    }
    neighbor_a = normalized_candidate(candidates[CAVS_MOTION_NEIGHBOR_A]);
    neighbor_b = normalized_candidate(candidates[CAVS_MOTION_NEIGHBOR_B]);
    if (candidates[CAVS_MOTION_NEIGHBOR_A].available == 0U ||
        candidates[CAVS_MOTION_NEIGHBOR_B].available == 0U ||
        (neighbor_a.reference_index == 0 && neighbor_a.vector.x == 0 &&
         neighbor_a.vector.y == 0) ||
        (neighbor_b.reference_index == 0 && neighbor_b.vector.x == 0 &&
         neighbor_b.vector.y == 0)) {
        derived.x = 0;
        derived.y = 0;
    } else {
        cavs_result result = cavs_predict_luma_motion(
            candidates, 0, default_block_distance,
            CAVS_MOTION_PARTITION_OTHER, precision, &derived);
        if (result != CAVS_OK) return result;
    }
    if (!vector_in_range(derived, minimum, maximum))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *motion = derived;
    return CAVS_OK;
}

static int64_t floor_divide_power_of_two(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static int derive_symmetric_component(int32_t forward,
                                      uint16_t forward_distance,
                                      uint16_t backward_distance,
                                      int32_t *backward) {
    int64_t factor;
    int64_t value;
    int64_t derived;
    if (forward_distance == 0U || backward_distance == 0U) return 0;
    factor = (int64_t)backward_distance * (512U / forward_distance);
    value = (int64_t)forward * factor + 256;
    derived = -floor_divide_power_of_two(value, 9U);
    if (derived < INT32_MIN || derived > INT32_MAX) return 0;
    *backward = (int32_t)derived;
    return 1;
}

cavs_result cavs_derive_symmetric_motion(
    const cavs_luma_motion_vector *forward, int8_t forward_reference_index,
    uint8_t picture_structure, uint16_t forward_block_distance,
    uint16_t backward_block_distance, cavs_luma_motion_precision precision,
    cavs_bidirectional_motion *motion) {
    cavs_bidirectional_motion derived;
    int32_t minimum;
    int32_t maximum;
    if (forward == NULL || motion == NULL || picture_structure > 1U ||
        forward_reference_index < 0 || forward_reference_index > 1 ||
        forward_block_distance == 0U || forward_block_distance > 511U ||
        backward_block_distance == 0U || backward_block_distance > 511U ||
        !motion_range(precision, &minimum, &maximum) ||
        !vector_in_range(*forward, minimum, maximum))
        return CAVS_ERR_INVALID_ARGUMENT;
    derived.forward = *forward;
    derived.forward_reference_index = forward_reference_index;
    derived.backward_reference_index = picture_structure != 0U ?
        forward_reference_index : (int8_t)(1 - forward_reference_index);
    if (!derive_symmetric_component(forward->x, forward_block_distance,
                                    backward_block_distance,
                                    &derived.backward.x) ||
        !derive_symmetric_component(forward->y, forward_block_distance,
                                    backward_block_distance,
                                    &derived.backward.y))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!vector_in_range(derived.backward, minimum, maximum))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *motion = derived;
    return CAVS_OK;
}

static int derive_direct_component(int32_t colocated, uint16_t distance,
                                   uint16_t denominator_distance,
                                   int opposite, int32_t *component) {
    uint64_t magnitude = colocated < 0 ?
        (uint64_t)(-(int64_t)colocated) : (uint64_t)colocated;
    uint64_t denominator;
    uint64_t numerator;
    uint64_t scaled;
    int negative;
    if (denominator_distance == 0U || distance == 0U) return 0;
    denominator = 16384U / denominator_distance;
    numerator = denominator * (1U + magnitude * distance) - 1U;
    scaled = numerator / 16384U;
    if (scaled > INT32_MAX) return 0;
    negative = (colocated < 0) != (opposite != 0);
    *component = negative ? -(int32_t)scaled : (int32_t)scaled;
    return 1;
}

cavs_result cavs_derive_direct_motion(
    const cavs_luma_motion_vector *colocated,
    int8_t forward_reference_index,
    int8_t backward_reference_index, uint8_t current_picture_structure,
    uint8_t colocated_picture_structure, uint16_t colocated_block_distance,
    uint16_t forward_block_distance, uint16_t backward_block_distance,
    cavs_luma_motion_precision precision, cavs_bidirectional_motion *motion) {
    cavs_bidirectional_motion derived;
    cavs_luma_motion_vector adjusted;
    int32_t minimum;
    int32_t maximum;
    if (colocated == NULL || motion == NULL ||
        forward_reference_index < 0 || forward_reference_index >= 4 ||
        backward_reference_index < 0 || backward_reference_index >= 4 ||
        current_picture_structure > 1U || colocated_picture_structure > 1U ||
        colocated_block_distance == 0U || colocated_block_distance > 511U ||
        forward_block_distance == 0U || forward_block_distance > 511U ||
        backward_block_distance == 0U || backward_block_distance > 511U ||
        !motion_range(precision, &minimum, &maximum) ||
        !vector_in_range(*colocated, minimum, maximum))
        return CAVS_ERR_INVALID_ARGUMENT;
    adjusted = *colocated;
    if (current_picture_structure != 0U &&
        colocated_picture_structure == 0U)
        adjusted.y *= 2;
    else if (current_picture_structure == 0U &&
             colocated_picture_structure != 0U)
        adjusted.y /= 2;
    derived.forward_reference_index = forward_reference_index;
    derived.backward_reference_index = backward_reference_index;
    if (!derive_direct_component(adjusted.x, forward_block_distance,
                                 colocated_block_distance, 0,
                                 &derived.forward.x) ||
        !derive_direct_component(adjusted.y, forward_block_distance,
                                 colocated_block_distance, 0,
                                 &derived.forward.y) ||
        !derive_direct_component(adjusted.x, backward_block_distance,
                                 colocated_block_distance, 1,
                                 &derived.backward.x) ||
        !derive_direct_component(adjusted.y, backward_block_distance,
                                 colocated_block_distance, 1,
                                 &derived.backward.y))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!vector_in_range(derived.forward, minimum, maximum) ||
        !vector_in_range(derived.backward, minimum, maximum))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *motion = derived;
    return CAVS_OK;
}

cavs_result cavs_derive_chroma_motion(
    cavs_pixel_format format, int32_t luma_x, int32_t luma_y,
    int32_t *chroma_x, int32_t *chroma_y) {
    int64_t derived_y;
    if (chroma_x == NULL || chroma_y == NULL ||
        (format != CAVS_YUV420P8 && format != CAVS_YUV422P8))
        return CAVS_ERR_INVALID_ARGUMENT;
    derived_y = format == CAVS_YUV422P8 ? 2 * (int64_t)luma_y : luma_y;
    if (derived_y < INT32_MIN || derived_y > INT32_MAX)
        return CAVS_ERR_INVALID_ARGUMENT;
    *chroma_x = luma_x;
    *chroma_y = (int32_t)derived_y;
    return CAVS_OK;
}
