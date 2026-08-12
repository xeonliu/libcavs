/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 9.4.6.2-9.4.6.3 and 9.10.1-9.10.2, Figures 18, 32, and 36-43.
 */
#include "motion.h"
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

static void split_motion(int32_t motion, int32_t denominator,
                         int32_t *integer, uint32_t *fraction) {
    int32_t quotient = motion / denominator;
    int32_t remainder = motion % denominator;
    if (remainder < 0) {
        --quotient;
        remainder += denominator;
    }
    *integer = quotient;
    *fraction = (uint32_t)remainder;
}

static size_t clamped_coordinate(size_t base, int32_t displacement,
                                 size_t limit) {
    if (displacement < 0) {
        uint32_t magnitude = (uint32_t)(-(int64_t)displacement);
        return (size_t)magnitude > base ? 0U : base - magnitude;
    }
    return (size_t)displacement > limit - base
               ? limit
               : base + (size_t)displacement;
}

static int block_arguments_valid(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    const uint8_t *prediction, size_t prediction_stride) {
    if (plane == NULL || prediction == NULL || width == 0U || height == 0U ||
        block_width == 0U || block_height == 0U)
        return 0;
    if (stride < width || prediction_stride < block_width ||
        block_width > CAVS_MAX_MOTION_BLOCK_DIMENSION ||
        block_height > CAVS_MAX_MOTION_BLOCK_DIMENSION ||
        x0 >= width || y0 >= height || width - x0 < block_width ||
        height - y0 < block_height)
        return 0;
    if (height - 1U > (SIZE_MAX - (width - 1U)) / stride ||
        block_height - 1U >
            (SIZE_MAX - (block_width - 1U)) / prediction_stride)
        return 0;
    return 1;
}

cavs_result cavs_interpolate_chroma_block(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    cavs_chroma_motion_precision precision,
    uint8_t *prediction, size_t prediction_stride) {
    uint8_t parsed[CAVS_MAX_MOTION_BLOCK_DIMENSION *
                   CAVS_MAX_MOTION_BLOCK_DIMENSION];
    int32_t integer_x;
    int32_t integer_y;
    uint32_t fraction_x;
    uint32_t fraction_y;
    uint32_t denominator;
    uint32_t scale;
    size_t x;
    size_t y;
    if (!block_arguments_valid(
            plane, width, height, stride, x0, y0, block_width, block_height,
            prediction, prediction_stride) ||
        (precision != CAVS_CHROMA_MOTION_EIGHTH &&
         precision != CAVS_CHROMA_MOTION_SIXTEENTH))
        return CAVS_ERR_INVALID_ARGUMENT;

    denominator = UINT32_C(1) << (unsigned)precision;
    scale = denominator * denominator;
    split_motion(motion_x, (int32_t)denominator, &integer_x, &fraction_x);
    split_motion(motion_y, (int32_t)denominator, &integer_y, &fraction_y);
    for (y = 0U; y < block_height; ++y) {
        size_t sample_y = clamped_coordinate(y0 + y, integer_y, height - 1U);
        size_t next_y = clamped_coordinate(y0 + y, integer_y + 1,
                                           height - 1U);
        for (x = 0U; x < block_width; ++x) {
            size_t sample_x = clamped_coordinate(x0 + x, integer_x, width - 1U);
            size_t next_x = clamped_coordinate(x0 + x, integer_x + 1,
                                               width - 1U);
            uint32_t a = plane[sample_y * stride + sample_x];
            uint32_t b = plane[sample_y * stride + next_x];
            uint32_t c = plane[next_y * stride + sample_x];
            uint32_t d = plane[next_y * stride + next_x];
            uint32_t sum = (denominator - fraction_x) *
                               (denominator - fraction_y) * a +
                           fraction_x * (denominator - fraction_y) * b +
                           (denominator - fraction_x) * fraction_y * c +
                           fraction_x * fraction_y * d;
            parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x] =
                (uint8_t)((sum + scale / 2U) / scale);
        }
    }
    for (y = 0U; y < block_height; ++y)
        for (x = 0U; x < block_width; ++x)
            prediction[y * prediction_stride + x] =
                parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x];
    return CAVS_OK;
}

typedef struct cavs_luma_source {
    const uint8_t *plane;
    size_t width;
    size_t height;
    size_t stride;
    size_t base_x;
    size_t base_y;
    int32_t integer_x;
    int32_t integer_y;
} cavs_luma_source;

static int32_t luma_integer(const cavs_luma_source *source,
                            int32_t offset_x, int32_t offset_y) {
    size_t x = clamped_coordinate(source->base_x,
                                  source->integer_x + offset_x,
                                  source->width - 1U);
    size_t y = clamped_coordinate(source->base_y,
                                  source->integer_y + offset_y,
                                  source->height - 1U);
    return source->plane[y * source->stride + x];
}

static int32_t luma_horizontal_half(const cavs_luma_source *source,
                                    int32_t anchor_x, int32_t row) {
    return -luma_integer(source, anchor_x - 1, row) +
           5 * luma_integer(source, anchor_x, row) +
           5 * luma_integer(source, anchor_x + 1, row) -
           luma_integer(source, anchor_x + 2, row);
}

static int32_t luma_vertical_half(const cavs_luma_source *source,
                                  int32_t column, int32_t anchor_y) {
    return -luma_integer(source, column, anchor_y - 1) +
           5 * luma_integer(source, column, anchor_y) +
           5 * luma_integer(source, column, anchor_y + 1) -
           luma_integer(source, column, anchor_y + 2);
}

static int32_t luma_half_half(const cavs_luma_source *source,
                              int32_t anchor_x, int32_t anchor_y) {
    return -luma_horizontal_half(source, anchor_x, anchor_y - 1) +
           5 * luma_horizontal_half(source, anchor_x, anchor_y) +
           5 * luma_horizontal_half(source, anchor_x, anchor_y + 1) -
           luma_horizontal_half(source, anchor_x, anchor_y + 2);
}

static int64_t floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static uint8_t rounded_luma(int32_t value, int32_t bias, unsigned shift) {
    int64_t rounded = floor_shift((int64_t)value + bias, shift);
    if (rounded < 0) return 0U;
    if (rounded > 255) return 255U;
    return (uint8_t)rounded;
}

static uint8_t luma_quarter_sample(const cavs_luma_source *source,
                                   uint32_t fraction_x,
                                   uint32_t fraction_y) {
    int32_t center;
    if (fraction_x == 0U && fraction_y == 0U)
        return (uint8_t)luma_integer(source, 0, 0);
    /* Axis-aligned a/b/c and d/h/n positions from Figure 42. */
    if (fraction_y == 0U) {
        if (fraction_x == 2U)
            return rounded_luma(luma_horizontal_half(source, 0, 0), 4, 3U);
        if (fraction_x == 1U)
            center = luma_horizontal_half(source, -1, 0) +
                     56 * luma_integer(source, 0, 0) +
                     7 * luma_horizontal_half(source, 0, 0) +
                     8 * luma_integer(source, 1, 0);
        else
            center = 8 * luma_integer(source, 0, 0) +
                     7 * luma_horizontal_half(source, 0, 0) +
                     56 * luma_integer(source, 1, 0) +
                     luma_horizontal_half(source, 1, 0);
        return rounded_luma(center, 64, 7U);
    }
    if (fraction_x == 0U) {
        if (fraction_y == 2U)
            return rounded_luma(luma_vertical_half(source, 0, 0), 4, 3U);
        if (fraction_y == 1U)
            center = luma_vertical_half(source, 0, -1) +
                     56 * luma_integer(source, 0, 0) +
                     7 * luma_vertical_half(source, 0, 0) +
                     8 * luma_integer(source, 0, 1);
        else
            center = 8 * luma_integer(source, 0, 0) +
                     7 * luma_vertical_half(source, 0, 0) +
                     56 * luma_integer(source, 0, 1) +
                     luma_vertical_half(source, 0, 1);
        return rounded_luma(center, 64, 7U);
    }

    center = luma_half_half(source, 0, 0);
    if (fraction_x == 2U && fraction_y == 2U)
        return rounded_luma(center, 32, 6U);
    /* One half-sample axis: i/k horizontally and f/q vertically. */
    if (fraction_y == 2U) {
        if (fraction_x == 1U)
            center = luma_half_half(source, -1, 0) +
                     56 * luma_vertical_half(source, 0, 0) + 7 * center +
                     8 * luma_vertical_half(source, 1, 0);
        else
            center = 8 * luma_vertical_half(source, 0, 0) + 7 * center +
                     56 * luma_vertical_half(source, 1, 0) +
                     luma_half_half(source, 1, 0);
        return rounded_luma(center, 512, 10U);
    }
    if (fraction_x == 2U) {
        if (fraction_y == 1U)
            center = luma_half_half(source, 0, -1) +
                     56 * luma_horizontal_half(source, 0, 0) + 7 * center +
                     8 * luma_horizontal_half(source, 0, 1);
        else
            center = 8 * luma_horizontal_half(source, 0, 0) + 7 * center +
                     56 * luma_horizontal_half(source, 0, 1) +
                     luma_half_half(source, 0, 1);
        return rounded_luma(center, 512, 10U);
    }

    /* Diagonal quarter positions e/g/p/r average j' with the nearest corner. */
    return rounded_luma(
        64 * luma_integer(source, fraction_x == 3U ? 1 : 0,
                          fraction_y == 3U ? 1 : 0) + center,
        64, 7U);
}

static int32_t half_grid_anchor(int32_t index) {
    int32_t anchor = index / 2;
    if (index < 0 && index % 2 != 0) --anchor;
    return anchor;
}

/* Returns an unrounded half-sample-grid value scaled by 64. */
static int32_t luma_half_grid64(const cavs_luma_source *source,
                                int32_t grid_x, int32_t grid_y) {
    int odd_x = grid_x % 2 != 0;
    int odd_y = grid_y % 2 != 0;
    int32_t anchor_x = half_grid_anchor(grid_x);
    int32_t anchor_y = half_grid_anchor(grid_y);
    if (!odd_x && !odd_y)
        return 64 * luma_integer(source, anchor_x, anchor_y);
    if (odd_x && !odd_y)
        return 8 * luma_horizontal_half(source, anchor_x, anchor_y);
    if (!odd_x && odd_y)
        return 8 * luma_vertical_half(source, anchor_x, anchor_y);
    return luma_half_half(source, anchor_x, anchor_y);
}

static uint8_t luma_eighth_axis_sample(const cavs_luma_source *source,
                                        uint32_t fraction,
                                        uint32_t fixed_fraction,
                                        int horizontal) {
    static const int8_t filters[2][4] = {
        {-6, 56, 15, -1},
        {-1, 15, 56, -6}
    };
    const int8_t *filter = filters[fraction % 4U == 1U ? 0 : 1];
    int32_t segment = (int32_t)(fraction / 4U);
    int32_t fixed = (int32_t)(fixed_fraction / 4U);
    int32_t sum = 0;
    unsigned index;
    for (index = 0U; index < 4U; ++index) {
        int32_t varying = segment + (int32_t)index - 1;
        int32_t value = horizontal
                            ? luma_half_grid64(source, varying, fixed)
                            : luma_half_grid64(source, fixed, varying);
        sum += filter[index] * value;
    }
    return rounded_luma(sum, 2048, 12U);
}

static uint8_t luma_eighth_interior_sample(const cavs_luma_source *source,
                                            uint32_t fraction_x,
                                            uint32_t fraction_y) {
    uint32_t cell_x = fraction_x / 4U;
    uint32_t cell_y = fraction_y / 4U;
    uint32_t local_x = fraction_x % 4U;
    uint32_t local_y = fraction_y % 4U;
    int32_t top_left = luma_half_grid64(
        source, (int32_t)cell_x, (int32_t)cell_y);
    int32_t top_right = luma_half_grid64(
        source, (int32_t)cell_x + 1, (int32_t)cell_y);
    int32_t bottom_left = luma_half_grid64(
        source, (int32_t)cell_x, (int32_t)cell_y + 1);
    int32_t bottom_right = luma_half_grid64(
        source, (int32_t)cell_x + 1, (int32_t)cell_y + 1);
    int32_t sum = (int32_t)((4U - local_x) * (4U - local_y)) * top_left +
                  (int32_t)(local_x * (4U - local_y)) * top_right +
                  (int32_t)((4U - local_x) * local_y) * bottom_left +
                  (int32_t)(local_x * local_y) * bottom_right;
    return rounded_luma(sum, 512, 10U);
}

static uint8_t luma_eighth_sample(const cavs_luma_source *source,
                                  uint32_t fraction_x,
                                  uint32_t fraction_y) {
    if (fraction_x % 2U == 0U && fraction_y % 2U == 0U)
        return luma_quarter_sample(source, fraction_x / 2U, fraction_y / 2U);
    if (fraction_x % 2U != 0U && fraction_y % 4U == 0U)
        return luma_eighth_axis_sample(
            source, fraction_x, fraction_y, 1);
    if (fraction_y % 2U != 0U && fraction_x % 4U == 0U)
        return luma_eighth_axis_sample(
            source, fraction_y, fraction_x, 0);
    return luma_eighth_interior_sample(source, fraction_x, fraction_y);
}

cavs_result cavs_interpolate_luma_block_quarter(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride) {
    uint8_t parsed[CAVS_MAX_MOTION_BLOCK_DIMENSION *
                   CAVS_MAX_MOTION_BLOCK_DIMENSION];
    cavs_luma_source source;
    uint32_t fraction_x;
    uint32_t fraction_y;
    size_t x;
    size_t y;
    if (!block_arguments_valid(
            plane, width, height, stride, x0, y0, block_width, block_height,
            prediction, prediction_stride))
        return CAVS_ERR_INVALID_ARGUMENT;
    source.plane = plane;
    source.width = width;
    source.height = height;
    source.stride = stride;
    split_motion(motion_x, 4, &source.integer_x, &fraction_x);
    split_motion(motion_y, 4, &source.integer_y, &fraction_y);
    for (y = 0U; y < block_height; ++y) {
        source.base_y = y0 + y;
        for (x = 0U; x < block_width; ++x) {
            source.base_x = x0 + x;
            parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x] =
                luma_quarter_sample(&source, fraction_x, fraction_y);
        }
    }
    for (y = 0U; y < block_height; ++y)
        for (x = 0U; x < block_width; ++x)
            prediction[y * prediction_stride + x] =
                parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x];
    return CAVS_OK;
}

cavs_result cavs_interpolate_luma_block_eighth(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, size_t block_width, size_t block_height,
    int32_t motion_x, int32_t motion_y,
    uint8_t *prediction, size_t prediction_stride) {
    uint8_t parsed[CAVS_MAX_MOTION_BLOCK_DIMENSION *
                   CAVS_MAX_MOTION_BLOCK_DIMENSION];
    cavs_luma_source source;
    uint32_t fraction_x;
    uint32_t fraction_y;
    size_t x;
    size_t y;
    if (!block_arguments_valid(
            plane, width, height, stride, x0, y0, block_width, block_height,
            prediction, prediction_stride))
        return CAVS_ERR_INVALID_ARGUMENT;
    source.plane = plane;
    source.width = width;
    source.height = height;
    source.stride = stride;
    split_motion(motion_x, 8, &source.integer_x, &fraction_x);
    split_motion(motion_y, 8, &source.integer_y, &fraction_y);
    for (y = 0U; y < block_height; ++y) {
        source.base_y = y0 + y;
        for (x = 0U; x < block_width; ++x) {
            source.base_x = x0 + x;
            parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x] =
                luma_eighth_sample(&source, fraction_x, fraction_y);
        }
    }
    for (y = 0U; y < block_height; ++y)
        for (x = 0U; x < block_width; ++x)
            prediction[y * prediction_stride + x] =
                parsed[y * CAVS_MAX_MOTION_BLOCK_DIMENSION + x];
    return CAVS_OK;
}
