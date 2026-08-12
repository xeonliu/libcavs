/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016, 信息技术 高效多媒体编码 第16部分: 广播电视视频,
 * 9.4.6, 9.9.1, Tables 55-57, and Figures 24-29.
 */
#include "broadcast_motion.h"
#include "image.h"
#include <limits.h>
#include <string.h>

typedef struct cavs_resolved_reference {
    const cavs_broadcast_reference *entry;
    int8_t index;
} cavs_resolved_reference;

struct cavs_macroblock_prediction_420 {
    uint8_t luma[256];
    uint8_t chroma[2][64];
};

typedef struct cavs_reference_plane {
    const uint8_t *data;
    size_t width;
    size_t height;
    size_t stride;
} cavs_reference_plane;

typedef struct cavs_picture_motion_geometry {
    size_t field_rows;
    size_t field_index;
    int64_t field_parity;
    int64_t vertical_step;
} cavs_picture_motion_geometry;

/* Computes modulo-512 distance; this introduces no codec decision. */
static uint16_t distance_index(uint16_t later, uint16_t earlier) {
    return (uint16_t)(((unsigned)later + 512U - earlier) % 512U);
}

/* Tests a field identifier; this introduces no codec decision. */
static int is_field(uint8_t field) {
    return field == CAVS_BROADCAST_FIELD_TOP ||
           field == CAVS_BROADCAST_FIELD_BOTTOM;
}

/* Converts a unified vector to the value-only primitive representation. */
static cavs_luma_motion_vector luma_value(const cavs_motion_vector *motion) {
    cavs_luma_motion_vector value;
    value.x = motion->x;
    value.y = motion->y;
    return value;
}

/* Writes one value/reference pair to unified storage. */
static void store_motion(cavs_motion_vector *destination,
                         cavs_luma_motion_vector value,
                         int8_t reference_index) {
    destination->x = value.x;
    destination->y = value.y;
    destination->reference_index = reference_index;
    destination->valid = 1U;
}

/* Clears one direction without manufacturing a reference. */
static void clear_motion(cavs_motion_vector *destination) {
    memset(destination, 0, sizeof(*destination));
    destination->reference_index = -1;
}

/* Resolves and validates one reference-list entry. */
static cavs_result resolve_reference(
    const cavs_broadcast_motion_context *context, unsigned direction,
    int8_t index, cavs_resolved_reference *reference) {
    const cavs_broadcast_reference *entry;
    if (context == NULL || reference == NULL ||
        direction >= CAVS_MB_DIRECTIONS || index < 0 ||
        index >= (int8_t)CAVS_BROADCAST_REFERENCE_COUNT)
        return CAVS_ERR_INVALID_ARGUMENT;
    entry = &context->reference[direction][(unsigned)index];
    if (entry->valid > 1U || entry->valid == 0U)
        return CAVS_ERR_MISSING_REFERENCE;
    if (entry->distance_index >= 512U || entry->block_distance == 0U ||
        entry->block_distance >= 512U ||
        (context->picture_structure == 0U && !is_field(entry->field)))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    reference->entry = entry;
    reference->index = index;
    return CAVS_OK;
}

/* Maps normalized geometry to the 9.4.6.2 partition shortcut. */
static cavs_motion_partition_position partition_position(
    const cavs_broadcast_motion_partition *partition) {
    if (partition->width == 8U && partition->height == 16U)
        return partition->x == 0U ? CAVS_MOTION_PARTITION_8X16_LEFT :
                                    CAVS_MOTION_PARTITION_8X16_RIGHT;
    if (partition->width == 16U && partition->height == 8U)
        return partition->y == 0U ? CAVS_MOTION_PARTITION_16X8_TOP :
                                    CAVS_MOTION_PARTITION_16X8_BOTTOM;
    return CAVS_MOTION_PARTITION_OTHER;
}

/* Checks one exact rectangular partition; this introduces no codec decision. */
static int geometry_is(const cavs_broadcast_motion_partition *partition,
                       uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
    return partition->x == x && partition->y == y &&
           partition->width == width && partition->height == height;
}

/* Validates the Table 55 P template. */
static int valid_p_geometry(const cavs_broadcast_motion_syntax *syntax) {
    switch (syntax->type) {
        case CAVS_MB_P_SKIP:
        case CAVS_MB_P_16X16:
            return syntax->partition_count == 1U &&
                geometry_is(&syntax->partition[0], 0U, 0U, 16U, 16U);
        case CAVS_MB_P_16X8:
            return syntax->partition_count == 2U &&
                geometry_is(&syntax->partition[0], 0U, 0U, 16U, 8U) &&
                geometry_is(&syntax->partition[1], 0U, 8U, 16U, 8U);
        case CAVS_MB_P_8X16:
            return syntax->partition_count == 2U &&
                geometry_is(&syntax->partition[0], 0U, 0U, 8U, 16U) &&
                geometry_is(&syntax->partition[1], 8U, 0U, 8U, 16U);
        case CAVS_MB_P_8X8:
            return syntax->partition_count == 4U &&
                geometry_is(&syntax->partition[0], 0U, 0U, 8U, 8U) &&
                geometry_is(&syntax->partition[1], 8U, 0U, 8U, 8U) &&
                geometry_is(&syntax->partition[2], 0U, 8U, 8U, 8U) &&
                geometry_is(&syntax->partition[3], 8U, 8U, 8U, 8U);
        default:
            return 0;
    }
}

/* Validates that B partitions tile the four 8x8 luma quadrants once. */
static int valid_b_geometry(const cavs_broadcast_motion_syntax *syntax) {
    unsigned covered = 0U;
    unsigned partition_index;
    if (syntax->partition_count == 0U ||
        syntax->partition_count > CAVS_MAX_MB_PARTITIONS)
        return 0;
    for (partition_index = 0U;
         partition_index < syntax->partition_count; ++partition_index) {
        const cavs_broadcast_motion_partition *partition =
            &syntax->partition[partition_index];
        unsigned block;
        if ((partition->x != 0U && partition->x != 8U) ||
            (partition->y != 0U && partition->y != 8U) ||
            (partition->width != 8U && partition->width != 16U) ||
            (partition->height != 8U && partition->height != 16U) ||
            (unsigned)partition->x + partition->width > 16U ||
            (unsigned)partition->y + partition->height > 16U)
            return 0;
        for (block = 0U; block < CAVS_MB_MOTION_BLOCKS; ++block) {
            unsigned x = (block & 1U) * 8U + 4U;
            unsigned y = (block >> 1U) * 8U + 4U;
            if (x >= partition->x &&
                x < (unsigned)partition->x + partition->width &&
                y >= partition->y &&
                y < (unsigned)partition->y + partition->height) {
                unsigned mask = 1U << block;
                if ((covered & mask) != 0U) return 0;
                covered |= mask;
            }
        }
    }
    return covered == 15U;
}

/* Resolves an external or already-derived same-macroblock candidate. */
static cavs_result resolve_candidate(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_candidate *source,
    const cavs_macroblock *macroblock, unsigned current_partition,
    unsigned direction, cavs_motion_candidate *candidate) {
    int8_t source_partition;
    if (context == NULL || source == NULL || macroblock == NULL ||
        candidate == NULL || direction >= CAVS_MB_DIRECTIONS)
        return CAVS_ERR_INVALID_ARGUMENT;
    source_partition = source->source_partition;
    if (source_partition == CAVS_BROADCAST_NO_PARTITION) {
        *candidate = source->value;
        return CAVS_OK;
    }
    if (source_partition < 0 || (unsigned)source_partition >= current_partition)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    memset(candidate, 0, sizeof(*candidate));
    candidate->available = 1U;
    candidate->same_direction = 1U;
    if (macroblock->partition[(unsigned)source_partition]
            .motion[direction].valid == 0U) {
        candidate->reference_index = -1;
        candidate->block_distance = 1U;
        candidate->same_direction = 0U;
    } else {
        cavs_resolved_reference reference;
        const cavs_motion_vector *motion =
            &macroblock->partition[(unsigned)source_partition]
                 .motion[direction];
        cavs_result result = resolve_reference(
            context, direction, motion->reference_index, &reference);
        if (result != CAVS_OK) return result;
        candidate->vector = luma_value(motion);
        candidate->reference_index = motion->reference_index;
        candidate->block_distance = reference.entry->block_distance;
    }
    return CAVS_OK;
}

/* Resolves A/B/C/D in primitive order. */
static cavs_result resolve_candidates(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_partition *partition,
    const cavs_macroblock *macroblock, unsigned current_partition,
    unsigned direction,
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT]) {
    unsigned index;
    for (index = 0U; index < CAVS_MOTION_NEIGHBOR_COUNT; ++index) {
        cavs_result result = resolve_candidate(
            context, &partition->candidate[direction][index], macroblock,
            current_partition, direction, &candidates[index]);
        if (result != CAVS_OK) return result;
    }
    return CAVS_OK;
}

/* Predicts, adds the syntax difference, and stores one ordinary direction. */
static cavs_result derive_decoded_direction(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_partition *syntax_partition,
    cavs_macroblock *macroblock, unsigned partition_index,
    unsigned direction) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector prediction;
    cavs_luma_motion_vector decoded;
    cavs_resolved_reference reference;
    cavs_result result;
    result = resolve_reference(context, direction,
                               syntax_partition->reference_index[direction],
                               &reference);
    if (result != CAVS_OK) return result;
    result = resolve_candidates(context, syntax_partition, macroblock,
                                partition_index, direction, candidates);
    if (result != CAVS_OK) return result;
    result = cavs_predict_luma_motion(
        candidates, reference.index, reference.entry->block_distance,
        partition_position(syntax_partition), context->precision,
        &prediction);
    if (result != CAVS_OK) return result;
    result = cavs_decode_luma_motion(
        &prediction, &syntax_partition->difference[direction],
        context->precision, &decoded);
    if (result != CAVS_OK) return result;
    store_motion(&macroblock->partition[partition_index].motion[direction],
                 decoded, reference.index);
    return CAVS_OK;
}

/* Derives Table 55 P_Skip, including the broadcast field-enhanced reference. */
static cavs_result derive_p_skip(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_partition *syntax_partition,
    cavs_macroblock *macroblock) {
    cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
    cavs_luma_motion_vector motion;
    cavs_resolved_reference reference;
    int8_t reference_index =
        context->picture_type == CAVS_PICTURE_P &&
        context->picture_structure == 0U &&
        context->pb_field_enhanced != 0U ? 1 :
        (int8_t)context->default_reference_index[CAVS_PRED_FORWARD];
    cavs_result result = resolve_reference(
        context, CAVS_PRED_FORWARD, reference_index, &reference);
    if (result != CAVS_OK) return result;
    result = resolve_candidates(context, syntax_partition, macroblock, 0U,
                                CAVS_PRED_FORWARD, candidates);
    if (result != CAVS_OK) return result;
    result = cavs_derive_p_skip_motion(
        candidates, reference.entry->block_distance, context->precision,
        &motion);
    if (result != CAVS_OK) return result;
    store_motion(&macroblock->partition[0].motion[CAVS_PRED_FORWARD],
                 motion, reference.index);
    return CAVS_OK;
}

/* Converts a B assembly mode to the unified prediction direction. */
static cavs_prediction_direction unified_direction(
    cavs_broadcast_motion_mode mode) {
    switch (mode) {
        case CAVS_BROADCAST_MOTION_FORWARD: return CAVS_PRED_FORWARD;
        case CAVS_BROADCAST_MOTION_BACKWARD: return CAVS_PRED_BACKWARD;
        case CAVS_BROADCAST_MOTION_SYMMETRIC: return CAVS_PRED_SYMMETRIC;
        default: return CAVS_PRED_BIDIRECTIONAL;
    }
}

/* Derives a symmetric partition after its forward syntax vector. */
static cavs_result derive_symmetric(
    const cavs_broadcast_motion_context *context,
    cavs_macroblock *macroblock, unsigned partition_index) {
    cavs_motion_vector *forward =
        &macroblock->partition[partition_index].motion[CAVS_PRED_FORWARD];
    cavs_resolved_reference forward_reference;
    cavs_resolved_reference backward_reference;
    cavs_bidirectional_motion symmetric;
    int8_t backward_index = context->picture_structure != 0U ?
        forward->reference_index : (int8_t)(1 - forward->reference_index);
    cavs_luma_motion_vector value = luma_value(forward);
    cavs_result result = resolve_reference(
        context, CAVS_PRED_FORWARD, forward->reference_index,
        &forward_reference);
    if (result != CAVS_OK) return result;
    result = resolve_reference(context, CAVS_PRED_BACKWARD, backward_index,
                               &backward_reference);
    if (result != CAVS_OK) return result;
    result = cavs_derive_symmetric_motion(
        &value, forward->reference_index, context->picture_structure,
        forward_reference.entry->block_distance,
        backward_reference.entry->block_distance, context->precision,
        &symmetric);
    if (result != CAVS_OK) return result;
    store_motion(&macroblock->partition[partition_index]
                      .motion[CAVS_PRED_BACKWARD],
                 symmetric.backward, symmetric.backward_reference_index);
    return CAVS_OK;
}

/* Implements one signed component of Part 16 9.9.1 direct scaling. */
static cavs_result direct_component(int32_t colocated, uint16_t distance,
                                    uint16_t denominator_distance,
                                    int opposite, int32_t correction,
                                    int32_t *component) {
    uint64_t magnitude = colocated < 0 ?
        (uint64_t)(-(int64_t)colocated) : (uint64_t)colocated;
    uint64_t denominator;
    uint64_t numerator;
    uint64_t scaled;
    int64_t corrected;
    int negative;
    if (component == NULL || denominator_distance == 0U || distance == 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    denominator = 16384U / denominator_distance;
    numerator = denominator * (1U + magnitude * distance) - 1U;
    scaled = numerator / 16384U;
    if (scaled > INT32_MAX) return CAVS_ERR_CORRUPT_BITSTREAM;
    negative = (colocated < 0) != (opposite != 0);
    corrected = (negative ? -(int64_t)scaled : (int64_t)scaled) - correction;
    if (corrected < INT32_MIN || corrected > INT32_MAX)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *component = (int32_t)corrected;
    return CAVS_OK;
}

/* Returns +2/-2 when a vector crosses bottom/top field coordinates. */
static int32_t field_correction(uint8_t source, uint8_t reference) {
    if (source == CAVS_BROADCAST_FIELD_TOP &&
        reference == CAVS_BROADCAST_FIELD_BOTTOM)
        return 2;
    if (source == CAVS_BROADCAST_FIELD_BOTTOM &&
        reference == CAVS_BROADCAST_FIELD_TOP)
        return -2;
    return 0;
}

/* Selects Part 16 PB-field-enhanced direct references for one 8x8 block. */
static cavs_result select_direct_references(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_colocated_block *colocated,
    cavs_resolved_reference *forward,
    cavs_resolved_reference *backward) {
    int8_t forward_index;
    int8_t backward_index;
    if (context->picture_structure != 0U) {
        forward_index =
            (int8_t)context->default_reference_index[CAVS_PRED_FORWARD];
        backward_index =
            (int8_t)context->default_reference_index[CAVS_PRED_BACKWARD];
    } else {
        if (context->pb_field_enhanced != 0U &&
            context->second_field != 0U) {
            forward_index = 0;
        } else if (context->reference[CAVS_PRED_FORWARD][0].valid != 0U &&
                   colocated->reference_distance_index ==
                       context->reference[CAVS_PRED_FORWARD][0]
                           .distance_index) {
            forward_index = 0;
        } else {
            forward_index = 1;
        }
        backward_index = context->pb_field_enhanced != 0U ||
            context->second_field == 0U ? 0 : 1;
    }
    {
        cavs_result result = resolve_reference(
            context, CAVS_PRED_FORWARD, forward_index, forward);
        if (result != CAVS_OK) return result;
    }
    return resolve_reference(context, CAVS_PRED_BACKWARD, backward_index,
                             backward);
}

/* Derives the non-intra co-located direct path, including field corrections. */
static cavs_result derive_colocated_direct(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_colocated_block *colocated,
    cavs_motion_vector motion[CAVS_MB_DIRECTIONS]) {
    cavs_resolved_reference forward;
    cavs_resolved_reference backward;
    cavs_luma_motion_vector adjusted = colocated->motion;
    cavs_bidirectional_motion direct;
    uint16_t colocated_distance;
    cavs_result result = select_direct_references(
        context, colocated, &forward, &backward);
    if (result != CAVS_OK) return result;
    colocated_distance = colocated->block_distance != 0U ?
        colocated->block_distance :
        distance_index(colocated->distance_index,
                       colocated->reference_distance_index);
    if (colocated_distance == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    if (context->pb_field_enhanced == 0U ||
        context->picture_structure != 0U) {
        result = cavs_derive_direct_motion(
            &adjusted, forward.index, backward.index,
            context->picture_structure, colocated->picture_structure,
            colocated_distance, forward.entry->block_distance,
            backward.entry->block_distance, context->precision, &direct);
        if (result != CAVS_OK) return result;
    } else {
        int32_t delta_reference;
        int32_t delta_forward;
        int32_t delta_backward;
        int64_t adjusted_vertical;
        if (!is_field(context->current_field) ||
            !is_field(forward.entry->field) ||
            !is_field(backward.entry->field) ||
            !is_field(colocated->reference_field))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        if (colocated->picture_structure != 0U)
            adjusted.y /= 2;
        delta_reference = field_correction(backward.entry->field,
                                           colocated->reference_field);
        delta_forward = field_correction(context->current_field,
                                         forward.entry->field);
        delta_backward = field_correction(context->current_field,
                                          backward.entry->field);
        result = direct_component(
            adjusted.x, forward.entry->block_distance,
            colocated_distance, 0, 0, &direct.forward.x);
        if (result != CAVS_OK) return result;
        adjusted_vertical = (int64_t)adjusted.y + delta_reference;
        if (adjusted_vertical < INT32_MIN || adjusted_vertical > INT32_MAX)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        result = direct_component(
            (int32_t)adjusted_vertical, forward.entry->block_distance,
            colocated_distance, 0, delta_forward, &direct.forward.y);
        if (result != CAVS_OK) return result;
        result = direct_component(
            adjusted.x, backward.entry->block_distance,
            colocated_distance, 1, 0, &direct.backward.x);
        if (result != CAVS_OK) return result;
        result = direct_component(
            (int32_t)adjusted_vertical, backward.entry->block_distance,
            colocated_distance, 1, delta_backward, &direct.backward.y);
        if (result != CAVS_OK) return result;
        direct.forward_reference_index = forward.index;
        direct.backward_reference_index = backward.index;
    }
    store_motion(&motion[CAVS_PRED_FORWARD], direct.forward,
                 direct.forward_reference_index);
    store_motion(&motion[CAVS_PRED_BACKWARD], direct.backward,
                 direct.backward_reference_index);
    return CAVS_OK;
}

/* Derives the co-located-intra direct fallback from ordinary predictors. */
static cavs_result derive_intra_direct(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_partition *syntax_partition,
    cavs_macroblock *macroblock, unsigned partition_index) {
    unsigned direction;
    for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
        cavs_motion_candidate candidates[CAVS_MOTION_NEIGHBOR_COUNT];
        cavs_luma_motion_vector prediction;
        cavs_resolved_reference reference;
        int8_t reference_index =
            (int8_t)context->default_reference_index[direction];
        cavs_result result = resolve_reference(
            context, direction, reference_index, &reference);
        if (result != CAVS_OK) return result;
        result = resolve_candidates(context, syntax_partition, macroblock,
                                    partition_index, direction, candidates);
        if (result != CAVS_OK) return result;
        result = cavs_predict_luma_motion(
            candidates, reference.index, reference.entry->block_distance,
            CAVS_MOTION_PARTITION_OTHER, context->precision, &prediction);
        if (result != CAVS_OK) return result;
        store_motion(&macroblock->partition[partition_index].motion[direction],
                     prediction, reference.index);
    }
    return CAVS_OK;
}

/* Maps an 8x8 partition's top-left to its co-located raster block. */
static unsigned colocated_index(
    const cavs_broadcast_motion_partition *partition) {
    return (unsigned)(partition->x / 8U) +
           (unsigned)(partition->y / 8U) * 2U;
}

/* Derives one direct 8x8 partition. */
static cavs_result derive_direct(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_partition *syntax_partition,
    cavs_macroblock *macroblock, unsigned partition_index) {
    const cavs_broadcast_colocated_block *colocated =
        &context->colocated[colocated_index(syntax_partition)];
    if (colocated->available > 1U || colocated->intra > 1U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (colocated->available == 0U) return CAVS_ERR_MISSING_REFERENCE;
    if (colocated->intra != 0U)
        return derive_intra_direct(context, syntax_partition, macroblock,
                                   partition_index);
    if (colocated->distance_index >= 512U ||
        colocated->reference_distance_index >= 512U ||
        colocated->picture_structure > 1U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    return derive_colocated_direct(
        context, colocated, macroblock->partition[partition_index].motion);
}

/* Validates common context state before deriving any partition. */
static cavs_result validate_context(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_syntax *syntax) {
    int p_path;
    if (context == NULL || syntax == NULL ||
        context->picture_type > CAVS_PICTURE_B ||
        context->picture_structure > 1U || context->second_field > 1U ||
        context->pb_field_enhanced > 1U ||
        context->current_distance_index >= 512U ||
        (context->precision != CAVS_LUMA_MOTION_QUARTER &&
         context->precision != CAVS_LUMA_MOTION_EIGHTH) ||
        (context->picture_structure != 0U && context->second_field != 0U) ||
        (context->picture_structure == 0U &&
         !is_field(context->current_field)))
        return CAVS_ERR_INVALID_ARGUMENT;
    p_path = context->picture_type == CAVS_PICTURE_P ||
        (context->picture_type == CAVS_PICTURE_I &&
         context->picture_structure == 0U && context->second_field != 0U);
    if (p_path != 0) {
        if (!valid_p_geometry(syntax)) return CAVS_ERR_CORRUPT_BITSTREAM;
    } else if (context->picture_type == CAVS_PICTURE_B) {
        if (syntax->type != CAVS_MB_B_SKIP &&
            syntax->type != CAVS_MB_B_DIRECT &&
            syntax->type != CAVS_MB_B_INTER)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        if (!valid_b_geometry(syntax)) return CAVS_ERR_CORRUPT_BITSTREAM;
    } else {
        return CAVS_ERR_INVALID_STATE;
    }
    return CAVS_OK;
}

/* Initializes normalized partitions while preserving non-motion MB metadata. */
static void initialize_macroblock(
    const cavs_broadcast_motion_syntax *syntax,
    cavs_macroblock *macroblock) {
    unsigned index;
    macroblock->type = syntax->type;
    macroblock->is_intra = 0U;
    macroblock->is_skipped =
        syntax->type == CAVS_MB_P_SKIP || syntax->type == CAVS_MB_B_SKIP;
    macroblock->partition_count = syntax->partition_count;
    for (index = 0U; index < CAVS_MAX_MB_PARTITIONS; ++index) {
        cavs_mb_partition *destination = &macroblock->partition[index];
        memset(destination, 0, sizeof(*destination));
        destination->motion[0].reference_index = -1;
        destination->motion[1].reference_index = -1;
        if (index < syntax->partition_count) {
            const cavs_broadcast_motion_partition *source =
                &syntax->partition[index];
            destination->x = source->x;
            destination->y = source->y;
            destination->width = source->width;
            destination->height = source->height;
            destination->direction = unified_direction(source->mode);
        }
    }
}

cavs_result cavs_assemble_broadcast_macroblock_motion(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_syntax *syntax,
    cavs_macroblock *macroblock) {
    cavs_macroblock assembled;
    cavs_result result;
    unsigned index;
    if (macroblock == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    result = validate_context(context, syntax);
    if (result != CAVS_OK) return result;
    assembled = *macroblock;
    initialize_macroblock(syntax, &assembled);
    if (syntax->type == CAVS_MB_P_SKIP) {
        if (syntax->partition[0].mode != CAVS_BROADCAST_MOTION_FORWARD)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        result = derive_p_skip(context, &syntax->partition[0], &assembled);
        if (result != CAVS_OK) return result;
    } else {
        for (index = 0U; index < syntax->partition_count; ++index) {
            const cavs_broadcast_motion_partition *partition =
                &syntax->partition[index];
            if (context->picture_type != CAVS_PICTURE_B ||
                syntax->type < CAVS_MB_B_SKIP) {
                if (partition->mode != CAVS_BROADCAST_MOTION_FORWARD)
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                result = derive_decoded_direction(
                    context, partition, &assembled, index,
                    CAVS_PRED_FORWARD);
            } else if (partition->mode == CAVS_BROADCAST_MOTION_DIRECT) {
                if (!geometry_is(partition, partition->x, partition->y,
                                 8U, 8U))
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                result = derive_direct(context, partition, &assembled, index);
            } else if (partition->mode == CAVS_BROADCAST_MOTION_FORWARD) {
                result = derive_decoded_direction(
                    context, partition, &assembled, index,
                    CAVS_PRED_FORWARD);
            } else if (partition->mode == CAVS_BROADCAST_MOTION_BACKWARD) {
                result = derive_decoded_direction(
                    context, partition, &assembled, index,
                    CAVS_PRED_BACKWARD);
            } else if (partition->mode == CAVS_BROADCAST_MOTION_SYMMETRIC) {
                result = derive_decoded_direction(
                    context, partition, &assembled, index,
                    CAVS_PRED_FORWARD);
                if (result == CAVS_OK)
                    result = derive_symmetric(context, &assembled, index);
            } else if (partition->mode ==
                       CAVS_BROADCAST_MOTION_BIDIRECTIONAL) {
                result = derive_decoded_direction(
                    context, partition, &assembled, index,
                    CAVS_PRED_FORWARD);
                if (result == CAVS_OK)
                    result = derive_decoded_direction(
                        context, partition, &assembled, index,
                        CAVS_PRED_BACKWARD);
            } else {
                return CAVS_ERR_CORRUPT_BITSTREAM;
            }
            if (result != CAVS_OK) return result;
        }
    }
    for (index = 0U; index < assembled.partition_count; ++index) {
        if (assembled.partition[index].motion[CAVS_PRED_FORWARD].valid == 0U)
            clear_motion(&assembled.partition[index]
                              .motion[CAVS_PRED_FORWARD]);
        if (assembled.partition[index].motion[CAVS_PRED_BACKWARD].valid == 0U)
            clear_motion(&assembled.partition[index]
                              .motion[CAVS_PRED_BACKWARD]);
    }
    *macroblock = assembled;
    return CAVS_OK;
}

/* Initializes the normalized unavailable-candidate representation. */
static void unavailable_picture_candidate(
    cavs_broadcast_motion_candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->source_partition = CAVS_BROADCAST_NO_PARTITION;
    candidate->value.reference_index = -1;
    candidate->value.block_distance = 1U;
}

/* Validates picture indexing and the current frame/field metadata half. */
static cavs_result picture_motion_geometry(
    const cavs_broadcast_motion_context *context,
    const cavs_picture *picture, const cavs_macroblock *macroblock,
    cavs_picture_motion_geometry *geometry) {
    size_t required;
    size_t address;
    uint8_t expected_field;
    if (context == NULL || picture == NULL || macroblock == NULL ||
        geometry == NULL || picture->format != CAVS_YUV420P8 ||
        picture->picture_type != context->picture_type ||
        picture->macroblock_width == 0U || picture->macroblock_height == 0U ||
        picture->macroblocks == NULL || picture->field_picture > 1U ||
        picture->top_field_first > 1U ||
        picture->coded_width != (uint32_t)picture->macroblock_width * 16U ||
        picture->coded_height != (uint32_t)picture->macroblock_height * 16U)
        return CAVS_ERR_INVALID_ARGUMENT;
    required = (size_t)picture->macroblock_width *
               picture->macroblock_height;
    if (picture->macroblock_count < required ||
        macroblock->row >= picture->macroblock_height ||
        macroblock->column >= picture->macroblock_width)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    address = (size_t)macroblock->row * picture->macroblock_width +
              macroblock->column;
    if (address > UINT32_MAX || macroblock->address != (uint32_t)address)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    memset(geometry, 0, sizeof(*geometry));
    geometry->vertical_step = 1;
    if (context->picture_structure != 0U) {
        if (picture->field_picture != 0U)
            return CAVS_ERR_INVALID_STATE;
        geometry->field_rows = picture->macroblock_height;
        return CAVS_OK;
    }
    if (picture->field_picture == 0U ||
        (picture->macroblock_height & 1U) != 0U ||
        (picture->coded_height & 31U) != 0U)
        return CAVS_ERR_INVALID_STATE;
    geometry->field_rows = picture->macroblock_height / 2U;
    if (geometry->field_rows == 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    geometry->field_index = macroblock->row / geometry->field_rows;
    if (geometry->field_index != context->second_field)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    expected_field = geometry->field_index == 0U ?
        (picture->top_field_first != 0U ? CAVS_BROADCAST_FIELD_TOP :
                                         CAVS_BROADCAST_FIELD_BOTTOM) :
        (picture->top_field_first != 0U ? CAVS_BROADCAST_FIELD_BOTTOM :
                                         CAVS_BROADCAST_FIELD_TOP);
    if (context->current_field != expected_field)
        return CAVS_ERR_INVALID_STATE;
    geometry->field_parity = expected_field == CAVS_BROADCAST_FIELD_BOTTOM ?
        1 : 0;
    geometry->vertical_step = 2;
    return CAVS_OK;
}

/* Locates the partition containing one macroblock-local luma sample. */
static int syntax_partition_at(
    const cavs_broadcast_motion_syntax *syntax, uint8_t x, uint8_t y,
    unsigned *partition_index) {
    unsigned index;
    for (index = 0U; index < syntax->partition_count; ++index) {
        const cavs_broadcast_motion_partition *partition =
            &syntax->partition[index];
        if (x >= partition->x &&
            (unsigned)x < (unsigned)partition->x + partition->width &&
            y >= partition->y &&
            (unsigned)y < (unsigned)partition->y + partition->height) {
            *partition_index = index;
            return 1;
        }
    }
    return 0;
}

/* Validates committed inter geometry and locates one sample's partition. */
static cavs_result picture_partition_at(
    const cavs_macroblock *macroblock, uint8_t x, uint8_t y,
    const cavs_mb_partition **selected) {
    unsigned covered = 0U;
    unsigned index;
    *selected = NULL;
    if (macroblock->partition_count == 0U ||
        macroblock->partition_count > CAVS_MAX_MB_PARTITIONS)
        return CAVS_ERR_INVALID_STATE;
    for (index = 0U; index < macroblock->partition_count; ++index) {
        const cavs_mb_partition *partition = &macroblock->partition[index];
        int forward = partition->motion[CAVS_PRED_FORWARD].valid != 0U;
        int backward = partition->motion[CAVS_PRED_BACKWARD].valid != 0U;
        unsigned block;
        if ((partition->x != 0U && partition->x != 8U) ||
            (partition->y != 0U && partition->y != 8U) ||
            (partition->width != 8U && partition->width != 16U) ||
            (partition->height != 8U && partition->height != 16U) ||
            (unsigned)partition->x + partition->width > 16U ||
            (unsigned)partition->y + partition->height > 16U ||
            partition->direction > CAVS_PRED_BIDIRECTIONAL ||
            partition->motion[CAVS_PRED_FORWARD].valid > 1U ||
            partition->motion[CAVS_PRED_BACKWARD].valid > 1U ||
            (partition->direction == CAVS_PRED_FORWARD &&
             (!forward || backward)) ||
            (partition->direction == CAVS_PRED_BACKWARD &&
             (forward || !backward)) ||
            ((partition->direction == CAVS_PRED_SYMMETRIC ||
              partition->direction == CAVS_PRED_BIDIRECTIONAL) &&
             (!forward || !backward)))
            return CAVS_ERR_INVALID_STATE;
        for (block = 0U; block < CAVS_MB_MOTION_BLOCKS; ++block) {
            unsigned block_x = (block & 1U) * 8U + 4U;
            unsigned block_y = (block >> 1U) * 8U + 4U;
            if (block_x >= partition->x &&
                block_x < (unsigned)partition->x + partition->width &&
                block_y >= partition->y &&
                block_y < (unsigned)partition->y + partition->height) {
                unsigned mask = 1U << block;
                if ((covered & mask) != 0U)
                    return CAVS_ERR_INVALID_STATE;
                covered |= mask;
            }
        }
        if (x >= partition->x &&
            (unsigned)x < (unsigned)partition->x + partition->width &&
            y >= partition->y &&
            (unsigned)y < (unsigned)partition->y + partition->height)
            *selected = partition;
    }
    return covered == 15U && *selected != NULL ? CAVS_OK :
                                                 CAVS_ERR_INVALID_STATE;
}

/* Converts one committed external block to a prediction candidate. */
static cavs_result external_picture_candidate(
    const cavs_broadcast_motion_context *context,
    const cavs_macroblock *neighbor, uint8_t x, uint8_t y,
    unsigned direction, cavs_broadcast_motion_candidate *candidate) {
    const cavs_mb_partition *partition;
    const cavs_motion_vector *motion;
    cavs_resolved_reference reference;
    cavs_result result;
    candidate->value.available = 1U;
    candidate->value.intra = neighbor->is_intra;
    if (neighbor->is_intra != 0U) return CAVS_OK;
    result = picture_partition_at(neighbor, x, y, &partition);
    if (result != CAVS_OK) return result;
    motion = &partition->motion[direction];
    if (motion->valid == 0U) {
        candidate->value.reference_index = -1;
        candidate->value.block_distance = 1U;
        return CAVS_OK;
    }
    result = resolve_reference(context, direction, motion->reference_index,
                               &reference);
    if (result != CAVS_OK) return result;
    candidate->value.same_direction = 1U;
    candidate->value.vector = luma_value(motion);
    candidate->value.reference_index = motion->reference_index;
    candidate->value.block_distance = reference.entry->block_distance;
    return CAVS_OK;
}

/*
 * Implements Part 16 Table 58 lookup in physical sample coordinates. Field
 * rows use step two, then map back to their decoder-order metadata half.
 */
static cavs_result picture_candidate_at(
    const cavs_broadcast_motion_context *context,
    const cavs_picture *picture, const cavs_macroblock *current,
    const cavs_broadcast_motion_syntax *syntax,
    const cavs_picture_motion_geometry *geometry,
    unsigned current_partition, unsigned direction,
    int64_t sample_x, int64_t sample_y,
    cavs_broadcast_motion_candidate *candidate) {
    size_t metadata_row;
    size_t column;
    size_t address;
    int64_t field_y;
    uint8_t local_x;
    uint8_t local_y;
    unsigned source_partition;
    const cavs_macroblock *neighbor;
    unavailable_picture_candidate(candidate);
    if (sample_x < 0 || sample_y < 0 ||
        (uint64_t)sample_x >= picture->coded_width ||
        (uint64_t)sample_y >= picture->coded_height)
        return CAVS_OK;
    column = (size_t)sample_x / 16U;
    local_x = (uint8_t)((uint64_t)sample_x % 16U);
    if (context->picture_structure == 0U) {
        if ((sample_y & 1) != geometry->field_parity)
            return CAVS_OK;
        field_y = (sample_y - geometry->field_parity) / 2;
        metadata_row = geometry->field_index * geometry->field_rows +
                       (size_t)field_y / 16U;
        local_y = (uint8_t)((uint64_t)field_y % 16U);
    } else {
        metadata_row = (size_t)sample_y / 16U;
        local_y = (uint8_t)((uint64_t)sample_y % 16U);
    }
    if (metadata_row >= picture->macroblock_height ||
        column >= picture->macroblock_width)
        return CAVS_OK;
    if (metadata_row == current->row && column == current->column) {
        if (syntax_partition_at(syntax, local_x, local_y,
                                &source_partition) &&
            source_partition < current_partition) {
            candidate->source_partition = (int8_t)source_partition;
        }
        return CAVS_OK;
    }
    address = metadata_row * picture->macroblock_width + column;
    if (address >= picture->macroblock_count) return CAVS_OK;
    neighbor = &picture->macroblocks[address];
    if (address >= current->address || neighbor->end_bit_offset == 0U ||
        neighbor->slice_id != current->slice_id)
        return CAVS_OK;
    if (neighbor->address != address || neighbor->row != metadata_row ||
        neighbor->column != column || neighbor->is_intra > 1U)
        return CAVS_ERR_INVALID_STATE;
    return external_picture_candidate(context, neighbor, local_x, local_y,
                                      direction, candidate);
}

/* Maps one entropy direction and its stored MVD/reference pair. */
static cavs_result picture_partition_syntax(
    const cavs_macroblock *macroblock, unsigned index,
    cavs_broadcast_motion_partition *syntax_partition) {
    const cavs_mb_partition *partition = &macroblock->partition[index];
    const cavs_motion_vector *forward =
        &partition->motion[CAVS_PRED_FORWARD];
    const cavs_motion_vector *backward =
        &partition->motion[CAVS_PRED_BACKWARD];
    unsigned direction;
    if (forward->valid > 1U || backward->valid > 1U ||
        partition->direction > CAVS_PRED_BIDIRECTIONAL)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    syntax_partition->x = partition->x;
    syntax_partition->y = partition->y;
    syntax_partition->width = partition->width;
    syntax_partition->height = partition->height;
    for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
        const cavs_motion_vector *motion = &partition->motion[direction];
        syntax_partition->reference_index[direction] = motion->reference_index;
        syntax_partition->difference[direction] = luma_value(motion);
    }
    if (macroblock->type < CAVS_MB_B_SKIP) {
        if (partition->direction != CAVS_PRED_FORWARD ||
            forward->valid == 0U || backward->valid != 0U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        syntax_partition->mode = CAVS_BROADCAST_MOTION_FORWARD;
    } else if (partition->direction == CAVS_PRED_FORWARD &&
               forward->valid != 0U && backward->valid == 0U) {
        syntax_partition->mode = CAVS_BROADCAST_MOTION_FORWARD;
    } else if (partition->direction == CAVS_PRED_BACKWARD &&
               forward->valid == 0U && backward->valid != 0U) {
        syntax_partition->mode = CAVS_BROADCAST_MOTION_BACKWARD;
    } else if (partition->direction == CAVS_PRED_SYMMETRIC &&
               forward->valid != 0U && backward->valid == 0U) {
        syntax_partition->mode = CAVS_BROADCAST_MOTION_SYMMETRIC;
    } else if (partition->direction == CAVS_PRED_BIDIRECTIONAL &&
               forward->valid != 0U && backward->valid != 0U) {
        syntax_partition->mode = CAVS_BROADCAST_MOTION_BIDIRECTIONAL;
    } else if (macroblock->type == CAVS_MB_B_INTER &&
               partition->direction == CAVS_PRED_BIDIRECTIONAL &&
               forward->valid == 0U && backward->valid == 0U &&
               partition->width == 8U && partition->height == 8U) {
        syntax_partition->mode = CAVS_BROADCAST_MOTION_DIRECT;
    } else {
        return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    return CAVS_OK;
}

/* Normalizes skip/direct geometry emitted without coded motion fields. */
static cavs_result picture_motion_syntax(
    const cavs_macroblock *macroblock,
    cavs_broadcast_motion_syntax *syntax) {
    unsigned index;
    memset(syntax, 0, sizeof(*syntax));
    syntax->type = macroblock->type;
    if (macroblock->is_intra != 0U || macroblock->type == CAVS_MB_I_8X8 ||
        macroblock->type >= CAVS_MB_INVALID)
        return CAVS_ERR_INVALID_STATE;
    if (macroblock->type == CAVS_MB_P_SKIP) {
        if (macroblock->partition_count > 1U ||
            (macroblock->partition_count == 1U &&
             (macroblock->partition[0].x != 0U ||
              macroblock->partition[0].y != 0U ||
              macroblock->partition[0].width != 16U ||
              macroblock->partition[0].height != 16U)))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        syntax->partition_count = 1U;
        syntax->partition[0].width = 16U;
        syntax->partition[0].height = 16U;
        syntax->partition[0].mode = CAVS_BROADCAST_MOTION_FORWARD;
        return CAVS_OK;
    }
    if (macroblock->type == CAVS_MB_B_SKIP ||
        macroblock->type == CAVS_MB_B_DIRECT) {
        if (macroblock->partition_count != 0U &&
            macroblock->partition_count != 1U &&
            macroblock->partition_count != 4U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        if (macroblock->partition_count == 1U &&
            (macroblock->partition[0].x != 0U ||
             macroblock->partition[0].y != 0U ||
             macroblock->partition[0].width != 16U ||
             macroblock->partition[0].height != 16U))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        if (macroblock->partition_count == 4U) {
            for (index = 0U; index < 4U; ++index) {
                const cavs_mb_partition *partition =
                    &macroblock->partition[index];
                if (partition->x != (uint8_t)((index & 1U) * 8U) ||
                    partition->y != (uint8_t)((index >> 1U) * 8U) ||
                    partition->width != 8U || partition->height != 8U)
                    return CAVS_ERR_CORRUPT_BITSTREAM;
            }
        }
        syntax->partition_count = 4U;
        for (index = 0U; index < 4U; ++index) {
            syntax->partition[index].x = (uint8_t)((index & 1U) * 8U);
            syntax->partition[index].y = (uint8_t)((index >> 1U) * 8U);
            syntax->partition[index].width = 8U;
            syntax->partition[index].height = 8U;
            syntax->partition[index].mode = CAVS_BROADCAST_MOTION_DIRECT;
        }
        return CAVS_OK;
    }
    if (macroblock->partition_count == 0U ||
        macroblock->partition_count > CAVS_MAX_MB_PARTITIONS)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    syntax->partition_count = macroblock->partition_count;
    for (index = 0U; index < syntax->partition_count; ++index) {
        cavs_result result = picture_partition_syntax(
            macroblock, index, &syntax->partition[index]);
        if (result != CAVS_OK) return result;
    }
    return CAVS_OK;
}

cavs_result cavs_assemble_broadcast_picture_macroblock_motion(
    const cavs_broadcast_motion_context *context,
    const cavs_picture *picture, const cavs_macroblock *entropy_macroblock,
    cavs_macroblock *macroblock) {
    cavs_picture_motion_geometry geometry;
    cavs_broadcast_motion_syntax syntax;
    cavs_macroblock assembled;
    unsigned partition_index;
    cavs_result result;
    if (context == NULL || picture == NULL || entropy_macroblock == NULL ||
        macroblock == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    result = picture_motion_geometry(context, picture, entropy_macroblock,
                                     &geometry);
    if (result != CAVS_OK) return result;
    result = picture_motion_syntax(entropy_macroblock, &syntax);
    if (result != CAVS_OK) return result;
    result = validate_context(context, &syntax);
    if (result != CAVS_OK) return result;
    for (partition_index = 0U;
         partition_index < syntax.partition_count; ++partition_index) {
        cavs_broadcast_motion_partition *partition =
            &syntax.partition[partition_index];
        /*
         * GB/T 20090.16-2016 9.9.1 b) 1) defines E as the macroblock
         * containing the current 8x8 block when an intra co-located block
         * selects the Direct fallback. Consequently all four Direct
         * partitions acquire A/B/C/D from the 16x16 macroblock geometry;
         * their individual 8x8 offsets must not move C inside that macroblock.
         * Non-intra Direct ignores these candidates, so the same geometry is
         * used for the complete Direct syntax path without another decision.
         */
        int direct = partition->mode == CAVS_BROADCAST_MOTION_DIRECT;
        int64_t x0 = (int64_t)entropy_macroblock->column * 16 +
                     (direct ? 0 : partition->x);
        int64_t local_row = context->picture_structure == 0U ?
            (int64_t)(entropy_macroblock->row % geometry.field_rows) :
            entropy_macroblock->row;
        int64_t y0 = local_row * 16 * geometry.vertical_step +
                     geometry.field_parity +
                     (direct ? 0 :
                      (int64_t)partition->y * geometry.vertical_step);
        int64_t x1 = x0 + (direct ? 16 : partition->width) - 1;
        int64_t sample_x[CAVS_MOTION_NEIGHBOR_COUNT];
        int64_t sample_y[CAVS_MOTION_NEIGHBOR_COUNT];
        unsigned direction;
        unsigned neighbor;
        sample_x[CAVS_MOTION_NEIGHBOR_A] = x0 - 1;
        sample_y[CAVS_MOTION_NEIGHBOR_A] = y0;
        sample_x[CAVS_MOTION_NEIGHBOR_B] = x0;
        sample_y[CAVS_MOTION_NEIGHBOR_B] =
            y0 - geometry.vertical_step;
        sample_x[CAVS_MOTION_NEIGHBOR_C] = x1 + 1;
        sample_y[CAVS_MOTION_NEIGHBOR_C] =
            y0 - geometry.vertical_step;
        sample_x[CAVS_MOTION_NEIGHBOR_D] = x0 - 1;
        sample_y[CAVS_MOTION_NEIGHBOR_D] =
            y0 - geometry.vertical_step;
        for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
            for (neighbor = 0U; neighbor < CAVS_MOTION_NEIGHBOR_COUNT;
                 ++neighbor) {
                result = picture_candidate_at(
                    context, picture, entropy_macroblock, &syntax, &geometry,
                    partition_index, direction, sample_x[neighbor],
                    sample_y[neighbor],
                    &partition->candidate[direction][neighbor]);
                if (result != CAVS_OK) return result;
            }
        }
    }
    assembled = *entropy_macroblock;
    result = cavs_assemble_broadcast_macroblock_motion(
        context, &syntax, &assembled);
    if (result != CAVS_OK) return result;
    *macroblock = assembled;
    return CAVS_OK;
}

/* Constructs a read-only full-frame or alternating-field plane view. */
static cavs_result reference_plane(
    const cavs_broadcast_reference *reference, unsigned plane,
    cavs_reference_plane *view) {
    const cavs_picture *picture;
    size_t width;
    size_t height;
    size_t stride;
    if (reference == NULL || view == NULL || plane >= 3U ||
        reference->picture == NULL)
        return CAVS_ERR_MISSING_REFERENCE;
    picture = reference->picture;
    if (picture->format != CAVS_YUV420P8 || picture->plane[plane] == NULL ||
        picture->coded_width == 0U || picture->coded_height == 0U ||
        picture->stride[plane] <= 0)
        return CAVS_ERR_INVALID_ARGUMENT;
    width = plane == 0U ? picture->coded_width : picture->coded_width / 2U;
    height = plane == 0U ? picture->coded_height : picture->coded_height / 2U;
    stride = (size_t)picture->stride[plane];
    if (stride < width || height == 0U ||
        height - 1U > (SIZE_MAX - (width - 1U)) / stride)
        return CAVS_ERR_INVALID_ARGUMENT;
    view->data = picture->plane[plane];
    view->width = width;
    view->height = height;
    view->stride = stride;
    if (reference->field == CAVS_BROADCAST_FIELD_FRAME)
        return CAVS_OK;
    if (!is_field(reference->field) || (height & 1U) != 0U ||
        stride > SIZE_MAX / 2U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (reference->field == CAVS_BROADCAST_FIELD_BOTTOM)
        view->data += stride;
    view->height /= 2U;
    view->stride *= 2U;
    return CAVS_OK;
}

/* Derives the current macroblock row in the selected reference coordinate view. */
static cavs_result reference_macroblock_row(
    const cavs_broadcast_motion_context *context,
    const cavs_macroblock *macroblock, const cavs_picture *reference,
    size_t *row) {
    size_t field_rows;
    if (context == NULL || macroblock == NULL || reference == NULL ||
        row == NULL || reference->coded_height % 16U != 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (context->picture_structure != 0U) {
        *row = macroblock->row;
        return CAVS_OK;
    }
    if (reference->coded_height % 32U != 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    field_rows = reference->coded_height / 32U;
    if (field_rows == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    *row = macroblock->row % field_rows;
    return CAVS_OK;
}

/* Interpolates one direction of one luma partition into a 16x16 staging plane. */
static cavs_result predict_luma_partition(
    const cavs_broadcast_motion_context *context,
    const cavs_macroblock *macroblock, const cavs_mb_partition *partition,
    unsigned direction, uint8_t prediction[256]) {
    const cavs_motion_vector *motion = &partition->motion[direction];
    cavs_resolved_reference reference;
    cavs_reference_plane plane;
    size_t macroblock_row;
    size_t x;
    size_t y;
    cavs_result result;
    if (motion->valid == 0U) return CAVS_ERR_MISSING_REFERENCE;
    result = resolve_reference(context, direction, motion->reference_index,
                               &reference);
    if (result != CAVS_OK) return result;
    result = reference_plane(reference.entry, 0U, &plane);
    if (result != CAVS_OK) return result;
    result = reference_macroblock_row(
        context, macroblock, reference.entry->picture, &macroblock_row);
    if (result != CAVS_OK) return result;
    x = (size_t)macroblock->column * 16U + partition->x;
    y = macroblock_row * 16U + partition->y;
    if (context->precision == CAVS_LUMA_MOTION_EIGHTH)
        return cavs_interpolate_luma_block_eighth(
            plane.data, plane.width, plane.height, plane.stride, x, y,
            partition->width, partition->height, motion->x, motion->y,
            prediction + (size_t)partition->y * 16U + partition->x, 16U);
    return cavs_interpolate_luma_block_quarter(
        plane.data, plane.width, plane.height, plane.stride, x, y,
        partition->width, partition->height, motion->x, motion->y,
        prediction + (size_t)partition->y * 16U + partition->x, 16U);
}

/* Interpolates one direction of one chroma partition into an 8x8 staging plane. */
static cavs_result predict_chroma_partition(
    const cavs_broadcast_motion_context *context,
    const cavs_macroblock *macroblock, const cavs_mb_partition *partition,
    unsigned direction, unsigned component, uint8_t prediction[64]) {
    const cavs_motion_vector *motion = &partition->motion[direction];
    cavs_resolved_reference reference;
    cavs_reference_plane plane;
    cavs_chroma_motion_precision precision;
    size_t macroblock_row;
    size_t x;
    size_t y;
    int32_t motion_x;
    int32_t motion_y;
    cavs_result result;
    if (motion->valid == 0U || component >= 2U)
        return CAVS_ERR_MISSING_REFERENCE;
    result = resolve_reference(context, direction, motion->reference_index,
                               &reference);
    if (result != CAVS_OK) return result;
    result = reference_plane(reference.entry, component + 1U, &plane);
    if (result != CAVS_OK) return result;
    result = reference_macroblock_row(
        context, macroblock, reference.entry->picture, &macroblock_row);
    if (result != CAVS_OK) return result;
    result = cavs_derive_chroma_motion(
        CAVS_YUV420P8, motion->x, motion->y, &motion_x, &motion_y);
    if (result != CAVS_OK) return result;
    precision = context->precision == CAVS_LUMA_MOTION_EIGHTH ?
        CAVS_CHROMA_MOTION_SIXTEENTH : CAVS_CHROMA_MOTION_EIGHTH;
    x = (size_t)macroblock->column * 8U + partition->x / 2U;
    y = macroblock_row * 8U + partition->y / 2U;
    return cavs_interpolate_chroma_block(
        plane.data, plane.width, plane.height, plane.stride, x, y,
        partition->width / 2U, partition->height / 2U,
        motion_x, motion_y, precision,
        prediction + (size_t)(partition->y / 2U) * 8U + partition->x / 2U,
        8U);
}

/* Averages one rectangular region; this introduces no codec decision. */
static void average_partition(uint8_t *destination, const uint8_t *forward,
                              const uint8_t *backward, size_t stride,
                              size_t x, size_t y, size_t width,
                              size_t height) {
    size_t row;
    for (row = 0U; row < height; ++row) {
        size_t column;
        for (column = 0U; column < width; ++column) {
            size_t offset = (y + row) * stride + x + column;
            destination[offset] = (uint8_t)(
                ((unsigned)forward[offset] + backward[offset] + 1U) >> 1U);
        }
    }
}

/* Copies one rectangular staging region; this introduces no codec decision. */
static void copy_partition(uint8_t *destination, const uint8_t *source,
                           size_t stride, size_t x, size_t y,
                           size_t width, size_t height) {
    size_t row;
    for (row = 0U; row < height; ++row)
        memcpy(destination + (y + row) * stride + x,
               source + (y + row) * stride + x, width);
}

/* Validates that normalized partitions cover the macroblock exactly once. */
static cavs_result validate_prediction_partitions(
    const cavs_macroblock *macroblock) {
    uint8_t covered[256];
    unsigned partition_index;
    if (macroblock == NULL || macroblock->is_intra != 0U ||
        macroblock->partition_count == 0U ||
        macroblock->partition_count > CAVS_MAX_MB_PARTITIONS)
        return CAVS_ERR_INVALID_ARGUMENT;
    memset(covered, 0, sizeof(covered));
    for (partition_index = 0U;
         partition_index < macroblock->partition_count; ++partition_index) {
        const cavs_mb_partition *partition =
            &macroblock->partition[partition_index];
        int forward = partition->motion[CAVS_PRED_FORWARD].valid != 0U;
        int backward = partition->motion[CAVS_PRED_BACKWARD].valid != 0U;
        unsigned row;
        if ((partition->x & 7U) != 0U || (partition->y & 7U) != 0U ||
            (partition->width != 8U && partition->width != 16U) ||
            (partition->height != 8U && partition->height != 16U) ||
            (unsigned)partition->x + partition->width > 16U ||
            (unsigned)partition->y + partition->height > 16U ||
            partition->direction > CAVS_PRED_BIDIRECTIONAL ||
            (partition->direction == CAVS_PRED_FORWARD &&
             (!forward || backward)) ||
            (partition->direction == CAVS_PRED_BACKWARD &&
             (forward || !backward)) ||
            ((partition->direction == CAVS_PRED_SYMMETRIC ||
              partition->direction == CAVS_PRED_BIDIRECTIONAL) &&
             (!forward || !backward)))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (row = 0U; row < partition->height; ++row) {
            unsigned column;
            for (column = 0U; column < partition->width; ++column) {
                size_t offset = ((size_t)partition->y + row) * 16U +
                                partition->x + column;
                if (covered[offset] != 0U)
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                covered[offset] = 1U;
            }
        }
    }
    for (partition_index = 0U; partition_index < 256U; ++partition_index)
        if (covered[partition_index] == 0U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
    return CAVS_OK;
}

cavs_result cavs_predict_broadcast_macroblock_420(
    const cavs_broadcast_motion_context *context,
    const cavs_macroblock *macroblock,
    struct cavs_macroblock_prediction_420 *prediction) {
    struct cavs_macroblock_prediction_420 assembled;
    unsigned partition_index;
    cavs_result result;
    if (context == NULL || macroblock == NULL || prediction == NULL ||
        context->picture_structure > 1U ||
        (context->precision != CAVS_LUMA_MOTION_QUARTER &&
         context->precision != CAVS_LUMA_MOTION_EIGHTH))
        return CAVS_ERR_INVALID_ARGUMENT;
    result = validate_prediction_partitions(macroblock);
    if (result != CAVS_OK) return result;
    memset(&assembled, 0, sizeof(assembled));
    for (partition_index = 0U;
         partition_index < macroblock->partition_count; ++partition_index) {
        const cavs_mb_partition *partition =
            &macroblock->partition[partition_index];
        uint8_t forward_luma[256];
        uint8_t backward_luma[256];
        unsigned direction;
        int forward = partition->motion[CAVS_PRED_FORWARD].valid != 0U;
        int backward = partition->motion[CAVS_PRED_BACKWARD].valid != 0U;
        if (!forward && !backward) return CAVS_ERR_MISSING_REFERENCE;
        memset(forward_luma, 0, sizeof(forward_luma));
        memset(backward_luma, 0, sizeof(backward_luma));
        if (forward) {
            result = predict_luma_partition(
                context, macroblock, partition, CAVS_PRED_FORWARD,
                forward_luma);
            if (result != CAVS_OK) return result;
        }
        if (backward) {
            result = predict_luma_partition(
                context, macroblock, partition, CAVS_PRED_BACKWARD,
                backward_luma);
            if (result != CAVS_OK) return result;
        }
        if (forward && backward)
            average_partition(assembled.luma, forward_luma, backward_luma,
                              16U, partition->x, partition->y,
                              partition->width, partition->height);
        else
            copy_partition(assembled.luma,
                           forward ? forward_luma : backward_luma, 16U,
                           partition->x, partition->y,
                           partition->width, partition->height);
        for (direction = 0U; direction < 2U; ++direction) {
            uint8_t forward_chroma[64];
            uint8_t backward_chroma[64];
            memset(forward_chroma, 0, sizeof(forward_chroma));
            memset(backward_chroma, 0, sizeof(backward_chroma));
            if (forward) {
                result = predict_chroma_partition(
                    context, macroblock, partition, CAVS_PRED_FORWARD,
                    direction, forward_chroma);
                if (result != CAVS_OK) return result;
            }
            if (backward) {
                result = predict_chroma_partition(
                    context, macroblock, partition, CAVS_PRED_BACKWARD,
                    direction, backward_chroma);
                if (result != CAVS_OK) return result;
            }
            if (forward && backward)
                average_partition(
                    assembled.chroma[direction], forward_chroma,
                    backward_chroma, 8U, partition->x / 2U,
                    partition->y / 2U, partition->width / 2U,
                    partition->height / 2U);
            else
                copy_partition(
                    assembled.chroma[direction],
                    forward ? forward_chroma : backward_chroma, 8U,
                    partition->x / 2U, partition->y / 2U,
                    partition->width / 2U, partition->height / 2U);
        }
    }
    *prediction = assembled;
    return CAVS_OK;
}
