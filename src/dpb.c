/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 6.4-6.5 and 9.4.5-9.4.6.1, Figures 14-20.
 */
#include "dpb.h"
#include <string.h>

/* Lifetime dispatch only; it introduces no codec decision. */
static void retain_picture(cavs_dpb *dpb, cavs_picture *picture) {
    dpb->lifetime.retain(dpb->lifetime.opaque, picture);
}

/* Lifetime dispatch only; it introduces no codec decision. */
static void release_picture(cavs_dpb *dpb, cavs_picture *picture) {
    dpb->lifetime.release(dpb->lifetime.opaque, picture);
}

/* Checks whether explicit lifetime management has been installed. */
static int lifetime_ready(const cavs_dpb *dpb) {
    return dpb != NULL && dpb->lifetime.retain != NULL &&
           dpb->lifetime.release != NULL;
}

/* Returns the physical field displayed first; no codec decision. */
static uint8_t first_display_field(const cavs_picture *picture) {
    return picture->top_field_first != 0U ? CAVS_FIELD_TOP :
                                            CAVS_FIELD_BOTTOM;
}

/* Returns whether field denotes a supported physical field or frame. */
static int field_valid(uint8_t field) {
    return field == CAVS_FIELD_TOP || field == CAVS_FIELD_BOTTOM ||
           field == CAVS_FIELD_BOTH;
}

/* Validates picture metadata consumed by DPB codec decisions. */
static int picture_metadata_valid(const cavs_picture *picture) {
    return picture != NULL && picture->picture_type <= CAVS_PICTURE_B &&
           picture->picture_distance <= 255U &&
           picture->top_field_first <= 1U && picture->field_picture <= 1U &&
           picture->filtered <= 1U && picture->is_reference <= 1U &&
           (picture->completed_fields & (uint8_t)~CAVS_FIELD_BOTH) == 0U &&
           !(picture->picture_type == CAVS_PICTURE_B &&
             picture->is_reference != 0U);
}

/* Validates the reconstruction state required before DPB insertion. */
static int picture_complete(const cavs_picture *picture) {
    size_t required;
    if (picture == NULL || picture->macroblock_width == 0U ||
        picture->macroblock_height == 0U)
        return 0;
    required = (size_t)picture->macroblock_width * picture->macroblock_height;
    return picture->filtered != 0U &&
           picture->completed_fields == CAVS_FIELD_BOTH &&
           picture->macroblocks != NULL &&
           picture->macroblock_count >= required;
}

/* Releases all persistent reference entries; no codec decision. */
static void clear_references(cavs_dpb *dpb) {
    size_t index;
    for (index = 0U; index < dpb->reference_count; ++index) {
        release_picture(dpb, dpb->reference[index].picture);
        memset(&dpb->reference[index], 0, sizeof(dpb->reference[index]));
    }
    dpb->reference_count = 0U;
}

cavs_result cavs_dpb_init(cavs_dpb *dpb) {
    if (dpb == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    memset(dpb, 0, sizeof(*dpb));
    return CAVS_OK;
}

cavs_result cavs_dpb_set_lifetime(cavs_dpb *dpb,
                                  const cavs_dpb_lifetime *lifetime) {
    if (dpb == NULL || lifetime == NULL || lifetime->retain == NULL ||
        lifetime->release == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (dpb->reference_count != 0U || dpb->output_count != 0U ||
        dpb->delayed_output != NULL || dpb->current_picture != NULL)
        return CAVS_ERR_INVALID_STATE;
    dpb->lifetime = *lifetime;
    return CAVS_OK;
}

cavs_result cavs_dpb_picture_distance(const cavs_picture *picture,
                                      uint8_t field,
                                      uint16_t *distance_index) {
    uint16_t distance;
    if (picture == NULL || distance_index == NULL || !field_valid(field) ||
        picture->picture_distance > 255U || picture->top_field_first > 1U)
        return CAVS_ERR_INVALID_ARGUMENT;
    distance = (uint16_t)(picture->picture_distance * 2U);
    if (field != CAVS_FIELD_BOTH &&
        field != first_display_field(picture))
        distance = (uint16_t)(distance + 1U);
    *distance_index = distance;
    return CAVS_OK;
}

cavs_result cavs_dpb_begin_picture(cavs_dpb *dpb, cavs_picture *picture,
                                   uint8_t field) {
    uint16_t distance;
    uint8_t first;
    cavs_result result;
    if (dpb == NULL || picture == NULL || !field_valid(field))
        return CAVS_ERR_INVALID_ARGUMENT;
    if (!lifetime_ready(dpb) || dpb->draining != 0U)
        return CAVS_ERR_INVALID_STATE;
    if (!picture_metadata_valid(picture))
        return CAVS_ERR_INVALID_ARGUMENT;
    if ((picture->field_picture == 0U && field != CAVS_FIELD_BOTH) ||
        (picture->field_picture != 0U && field == CAVS_FIELD_BOTH))
        return CAVS_ERR_INVALID_ARGUMENT;
    result = cavs_dpb_picture_distance(picture, field, &distance);
    if (result != CAVS_OK) return result;
    if (dpb->current_picture == NULL) {
        if (picture->completed_fields != 0U || picture->filtered != 0U)
            return CAVS_ERR_INVALID_STATE;
        if (picture->field_picture != 0U &&
            field != first_display_field(picture))
            return CAVS_ERR_INVALID_STATE;
        retain_picture(dpb, picture);
        dpb->current_picture = picture;
    } else if (dpb->current_picture != picture) {
        return CAVS_ERR_INVALID_STATE;
    } else {
        first = first_display_field(picture);
        if (dpb->current_field != first || field == first ||
            picture->completed_fields != first || picture->filtered != 0U)
            return CAVS_ERR_INVALID_STATE;
    }
    dpb->current_field = field;
    dpb->current_distance = distance;
    return CAVS_OK;
}

cavs_result cavs_dpb_end_picture(cavs_dpb *dpb, cavs_picture *picture) {
    if (dpb == NULL || picture == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (!lifetime_ready(dpb)) return CAVS_ERR_INVALID_STATE;
    if (dpb->current_picture != picture)
        return CAVS_ERR_INVALID_STATE;
    release_picture(dpb, dpb->current_picture);
    dpb->current_picture = NULL;
    dpb->current_distance = 0U;
    dpb->current_field = 0U;
    return CAVS_OK;
}

/* Computes one directed modular display distance from a DistanceIndex. */
static uint16_t directed_distance(uint16_t current, uint16_t reference,
                                  uint8_t direction) {
    unsigned distance;
    if (direction == CAVS_DPB_FORWARD)
        distance = (unsigned)current + CAVS_DPB_DISTANCE_MODULUS - reference;
    else
        distance = (unsigned)reference + CAVS_DPB_DISTANCE_MODULUS - current;
    return (uint16_t)(distance % CAVS_DPB_DISTANCE_MODULUS);
}

/* Appends a unique candidate; queue mechanics introduce no codec decision. */
static void append_candidate(cavs_dpb_reference candidate,
                             cavs_dpb_reference candidates[5],
                             size_t *candidate_count) {
    size_t index;
    for (index = 0U; index < *candidate_count; ++index) {
        if (candidates[index].picture == candidate.picture &&
            candidates[index].field == candidate.field)
            return;
    }
    if (*candidate_count < 5U) {
        candidates[*candidate_count] = candidate;
        ++*candidate_count;
    }
}

/* Adds a reference only when it lies in the requested display direction. */
static void append_directed_candidate(
    const cavs_dpb *dpb, cavs_dpb_reference candidate, uint8_t direction,
    cavs_dpb_reference candidates[5], size_t *candidate_count) {
    candidate.block_distance = directed_distance(
        dpb->current_distance, candidate.distance, direction);
    if (candidate.block_distance != 0U &&
        candidate.block_distance < CAVS_DPB_DISTANCE_MODULUS / 2U)
        append_candidate(candidate, candidates, candidate_count);
}

/* Stable insertion sort over at most five entries; no codec decision. */
static void sort_candidates(cavs_dpb_reference candidates[5], size_t count) {
    size_t index;
    for (index = 1U; index < count; ++index) {
        cavs_dpb_reference value = candidates[index];
        size_t position = index;
        while (position != 0U &&
               candidates[position - 1U].block_distance >
                   value.block_distance) {
            candidates[position] = candidates[position - 1U];
            --position;
        }
        candidates[position] = value;
    }
}

/* Returns NumberOfReference per GB/T 20090.16 Figures 14-20. */
static size_t candidate_limit(const cavs_dpb *dpb, uint8_t direction) {
    const cavs_picture *picture = dpb->current_picture;
    if (picture->picture_type == CAVS_PICTURE_I) {
        if (picture->field_picture != 0U &&
            dpb->current_field != first_display_field(picture) &&
            direction == CAVS_DPB_FORWARD)
            return 1U;
        return 0U;
    }
    if (picture->picture_type == CAVS_PICTURE_P) {
        if (direction != CAVS_DPB_FORWARD) return 0U;
        return picture->field_picture != 0U ? 4U : 2U;
    }
    return picture->field_picture != 0U ? 2U : 1U;
}

/* Converts one stored entry to the current picture's reference granularity. */
static void append_stored_reference(
    const cavs_dpb *dpb, const cavs_dpb_reference *stored, uint8_t direction,
    cavs_dpb_reference candidates[5], size_t *candidate_count) {
    cavs_dpb_reference candidate = *stored;
    if (dpb->current_picture->field_picture == 0U) {
        candidate.field = CAVS_FIELD_BOTH;
        if (cavs_dpb_picture_distance(candidate.picture, candidate.field,
                                      &candidate.distance) == CAVS_OK)
            append_directed_candidate(dpb, candidate, direction, candidates,
                                      candidate_count);
        return;
    }
    if (candidate.field != CAVS_FIELD_BOTH) {
        append_directed_candidate(dpb, candidate, direction, candidates,
                                  candidate_count);
        return;
    }
    candidate.field = first_display_field(candidate.picture);
    if (cavs_dpb_picture_distance(candidate.picture, candidate.field,
                                  &candidate.distance) == CAVS_OK)
        append_directed_candidate(dpb, candidate, direction, candidates,
                                  candidate_count);
    candidate.field = candidate.field == CAVS_FIELD_TOP ? CAVS_FIELD_BOTTOM :
                                                          CAVS_FIELD_TOP;
    if (cavs_dpb_picture_distance(candidate.picture, candidate.field,
                                  &candidate.distance) == CAVS_OK)
        append_directed_candidate(dpb, candidate, direction, candidates,
                                  candidate_count);
}

/* Builds the direction-specific reference list defined by Figures 14-20. */
static size_t collect_candidates(const cavs_dpb *dpb, uint8_t direction,
                                 cavs_dpb_reference candidates[5]) {
    size_t count = 0U;
    size_t limit = candidate_limit(dpb, direction);
    size_t index;
    if (limit == 0U) return 0U;
    if (dpb->current_picture->field_picture != 0U &&
        dpb->current_field != first_display_field(dpb->current_picture) &&
        dpb->current_picture->picture_type != CAVS_PICTURE_B &&
        direction == CAVS_DPB_FORWARD) {
        cavs_dpb_reference self;
        memset(&self, 0, sizeof(self));
        self.picture = dpb->current_picture;
        self.field = first_display_field(dpb->current_picture);
        if (cavs_dpb_picture_distance(self.picture, self.field,
                                      &self.distance) == CAVS_OK) {
            append_directed_candidate(dpb, self, direction, candidates,
                                      &count);
        }
    }
    for (index = 0U; index < dpb->reference_count; ++index)
        append_stored_reference(dpb, &dpb->reference[index], direction,
                                candidates, &count);
    sort_candidates(candidates, count);
    if (count > limit) count = limit;
    return count;
}

cavs_result cavs_dpb_select_reference_entry(
    const cavs_dpb *dpb, uint8_t direction, uint8_t reference_index,
    uint8_t field, cavs_dpb_reference *reference) {
    cavs_dpb_reference candidates[5];
    size_t count;
    if (dpb == NULL || reference == NULL ||
        (direction != CAVS_DPB_FORWARD && direction != CAVS_DPB_BACKWARD) ||
        !field_valid(field))
        return CAVS_ERR_INVALID_ARGUMENT;
    if (!lifetime_ready(dpb)) return CAVS_ERR_INVALID_STATE;
    if (dpb->current_picture == NULL || field != dpb->current_field)
        return CAVS_ERR_INVALID_STATE;
    count = collect_candidates(dpb, direction, candidates);
    if ((size_t)reference_index >= count)
        return CAVS_ERR_MISSING_REFERENCE;
    *reference = candidates[reference_index];
    return CAVS_OK;
}

cavs_result cavs_dpb_select_reference(
    const cavs_dpb *dpb, uint8_t direction, uint8_t reference_index,
    uint8_t field, cavs_picture **picture) {
    cavs_dpb_reference reference;
    cavs_result result;
    if (picture == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    result = cavs_dpb_select_reference_entry(
        dpb, direction, reference_index, field, &reference);
    if (result != CAVS_OK) return result;
    *picture = reference.picture;
    return CAVS_OK;
}

cavs_result cavs_dpb_colocated_macroblock(
    const cavs_dpb *dpb, uint8_t direction, uint8_t reference_index,
    uint8_t field, uint32_t sample_x, uint32_t sample_y,
    cavs_dpb_reference *reference, const cavs_macroblock **macroblock,
    uint8_t *block_index) {
    cavs_dpb_reference selected;
    size_t row;
    size_t column;
    size_t local_y;
    size_t address;
    uint8_t metadata_field;
    cavs_result result;
    if (reference == NULL || macroblock == NULL || block_index == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    result = cavs_dpb_select_reference_entry(
        dpb, direction, reference_index, field, &selected);
    if (result != CAVS_OK) return result;
    if (sample_x >= selected.picture->coded_width ||
        sample_y >= selected.picture->coded_height)
        return CAVS_ERR_INVALID_ARGUMENT;
    column = sample_x / 16U;
    if (selected.picture->field_picture == 0U) {
        local_y = sample_y;
        row = local_y / 16U;
    } else {
        size_t field_rows = selected.picture->macroblock_height / 2U;
        size_t ordinal;
        metadata_field = selected.field;
        if (metadata_field == CAVS_FIELD_BOTH)
            metadata_field = (sample_y & 1U) == 0U ? CAVS_FIELD_TOP :
                                                    CAVS_FIELD_BOTTOM;
        ordinal = metadata_field ==
            first_display_field(selected.picture) ? 0U : 1U;
        local_y = sample_y / 2U;
        row = ordinal * field_rows + local_y / 16U;
    }
    if (row >= selected.picture->macroblock_height ||
        column >= selected.picture->macroblock_width)
        return CAVS_ERR_MISSING_REFERENCE;
    address = row * selected.picture->macroblock_width + column;
    if (address >= selected.picture->macroblock_count ||
        selected.picture->macroblocks[address].end_bit_offset == 0U)
        return CAVS_ERR_MISSING_REFERENCE;
    *reference = selected;
    *macroblock = &selected.picture->macroblocks[address];
    *block_index = (uint8_t)(((local_y & 15U) >= 8U ? 2U : 0U) +
                             ((sample_x & 15U) >= 8U ? 1U : 0U));
    return CAVS_OK;
}

/* Adds current reference picture and retains no more than two pictures. */
static void update_references(cavs_dpb *dpb, cavs_picture *picture) {
    cavs_dpb_reference updated[CAVS_DPB_MAX_REFERENCES];
    size_t updated_count = 0U;
    size_t index;
    cavs_picture *previous = NULL;
    uint8_t fields[2];
    size_t field_count;
    memset(updated, 0, sizeof(updated));
    if (picture->field_picture != 0U) {
        fields[0] = first_display_field(picture);
        fields[1] = fields[0] == CAVS_FIELD_TOP ? CAVS_FIELD_BOTTOM :
                                                  CAVS_FIELD_TOP;
        field_count = 2U;
    } else {
        fields[0] = CAVS_FIELD_BOTH;
        field_count = 1U;
    }
    for (index = 0U; index < field_count; ++index) {
        updated[updated_count].picture = picture;
        updated[updated_count].field = fields[index];
        (void)cavs_dpb_picture_distance(
            picture, fields[index], &updated[updated_count].distance);
        retain_picture(dpb, picture);
        ++updated_count;
    }
    for (index = 0U; index < dpb->reference_count &&
                    updated_count < CAVS_DPB_MAX_REFERENCES; ++index) {
        if (dpb->reference[index].picture == picture) continue;
        if (previous == NULL) previous = dpb->reference[index].picture;
        if (dpb->reference[index].picture != previous) continue;
        updated[updated_count] = dpb->reference[index];
        updated[updated_count].block_distance = 0U;
        retain_picture(dpb, updated[updated_count].picture);
        ++updated_count;
    }
    clear_references(dpb);
    memcpy(dpb->reference, updated,
           updated_count * sizeof(dpb->reference[0]));
    dpb->reference_count = updated_count;
}

/* Appends one owned output pointer; no codec decision. */
static void append_output(cavs_dpb *dpb, cavs_picture *picture) {
    dpb->output[dpb->output_count++] = picture;
}

cavs_result cavs_dpb_store(cavs_dpb *dpb, cavs_picture *picture) {
    int anchor;
    if (dpb == NULL || picture == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (!lifetime_ready(dpb)) return CAVS_ERR_INVALID_STATE;
    if (dpb->draining != 0U || dpb->current_picture != picture)
        return CAVS_ERR_INVALID_STATE;
    if (!picture_metadata_valid(picture) || !picture_complete(picture))
        return CAVS_ERR_INVALID_STATE;
    if ((picture->field_picture == 0U &&
         dpb->current_field != CAVS_FIELD_BOTH) ||
        (picture->field_picture != 0U &&
         dpb->current_field == first_display_field(picture)))
        return CAVS_ERR_INVALID_STATE;
    anchor = picture->picture_type != CAVS_PICTURE_B;
    if ((anchor && dpb->delayed_output != NULL &&
         dpb->output_count == CAVS_DPB_MAX_OUTPUT) ||
        (!anchor && dpb->output_count == CAVS_DPB_MAX_OUTPUT))
        return CAVS_AGAIN;
    if (anchor && dpb->delayed_output != NULL) {
        append_output(dpb, dpb->delayed_output);
        dpb->delayed_output = NULL;
    }
    if (anchor) {
        retain_picture(dpb, picture);
        dpb->delayed_output = picture;
    } else {
        retain_picture(dpb, picture);
        append_output(dpb, picture);
    }
    if (picture->is_reference != 0U)
        update_references(dpb, picture);
    release_picture(dpb, dpb->current_picture);
    dpb->current_picture = NULL;
    dpb->current_distance = 0U;
    dpb->current_field = 0U;
    return CAVS_OK;
}

cavs_result cavs_dpb_pop_output(cavs_dpb *dpb, cavs_picture **picture) {
    size_t index;
    if (dpb == NULL || picture == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (!lifetime_ready(dpb)) return CAVS_ERR_INVALID_STATE;
    if (dpb->output_count == 0U)
        return dpb->draining != 0U ? CAVS_EOF : CAVS_AGAIN;
    *picture = dpb->output[0];
    for (index = 1U; index < dpb->output_count; ++index)
        dpb->output[index - 1U] = dpb->output[index];
    --dpb->output_count;
    dpb->output[dpb->output_count] = NULL;
    return CAVS_OK;
}

cavs_result cavs_dpb_flush(cavs_dpb *dpb) {
    if (dpb == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (!lifetime_ready(dpb)) return CAVS_ERR_INVALID_STATE;
    if (dpb->draining != 0U) return CAVS_OK;
    if (dpb->delayed_output != NULL &&
        dpb->output_count == CAVS_DPB_MAX_OUTPUT)
        return CAVS_AGAIN;
    if (dpb->current_picture != NULL) {
        release_picture(dpb, dpb->current_picture);
        dpb->current_picture = NULL;
        dpb->current_distance = 0U;
        dpb->current_field = 0U;
    }
    if (dpb->delayed_output != NULL) {
        append_output(dpb, dpb->delayed_output);
        dpb->delayed_output = NULL;
    }
    clear_references(dpb);
    dpb->draining = 1U;
    return CAVS_OK;
}

cavs_result cavs_dpb_reset(cavs_dpb *dpb) {
    size_t index;
    if (dpb == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (!lifetime_ready(dpb)) return CAVS_ERR_INVALID_STATE;
    clear_references(dpb);
    for (index = 0U; index < dpb->output_count; ++index)
        release_picture(dpb, dpb->output[index]);
    memset(dpb->output, 0, sizeof(dpb->output));
    dpb->output_count = 0U;
    if (dpb->delayed_output != NULL)
        release_picture(dpb, dpb->delayed_output);
    if (dpb->current_picture != NULL)
        release_picture(dpb, dpb->current_picture);
    dpb->delayed_output = NULL;
    dpb->current_picture = NULL;
    dpb->current_distance = 0U;
    dpb->current_field = 0U;
    dpb->draining = 0U;
    return CAVS_OK;
}

void cavs_dpb_destroy(cavs_dpb *dpb) {
    if (dpb == NULL) return;
    if (lifetime_ready(dpb)) (void)cavs_dpb_reset(dpb);
    memset(dpb, 0, sizeof(*dpb));
}
