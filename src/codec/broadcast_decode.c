/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 7.4 and 9.2-9.10 broadcast field-slice decoding.
 */
#include "codec/broadcast_decode.h"
#include "codec/broadcast_macroblock.h"
#include "codec/broadcast_motion.h"
#include "codec/slice_decode.h"
#include "frame.h"
#include <string.h>

typedef struct cavs_broadcast_reader_state {
    cavs_broadcast_slice_decoder entropy;
    const cavs_dpb *dpb;
    cavs_picture *picture;
    cavs_picture_type picture_type;
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t skip_mode_flag;
    uint8_t picture_reference_flag;
    uint8_t fixed_qp;
    uint8_t previous_qp;
    int8_t previous_qp_delta;
    uint8_t mb_weighting_flag;
    uint16_t slice_id;
    uint16_t start_row;
    uint32_t skip_remaining;
    uint8_t explicit_pending;
    size_t last_bit_offset;
    cavs_broadcast_motion_context motion;
    cavs_macroblock_prediction_420 *prediction;
    cavs_macroblock *entropy_row;
} cavs_broadcast_reader_state;

typedef struct cavs_motion_profile_capability {
    cavs_luma_motion_precision luma_precision;
    uint8_t motion_unit;
} cavs_motion_profile_capability;

int cavs_broadcast_picture_supported(
    const cavs_sequence_info *sequence, cavs_picture_type picture_type,
    const cavs_i_picture_header *i_picture,
    const cavs_pb_picture_header *pb_picture) {
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t advanced_entropy;
    uint8_t weighting_quant;
    if (sequence == NULL || i_picture == NULL || pb_picture == NULL ||
        sequence->profile_id != UINT8_C(0x48) ||
        sequence->format != CAVS_YUV420P8 ||
        sequence->progressive_sequence != 0U)
        return 0;
    if (picture_type == CAVS_PICTURE_I) {
        progressive_frame = i_picture->progressive_frame;
        picture_structure = i_picture->picture_structure;
        advanced_entropy = i_picture->advanced_entropy_enabled;
        weighting_quant = i_picture->weighting_quant_flag;
    } else {
        progressive_frame = pb_picture->progressive_frame;
        picture_structure = pb_picture->picture_structure;
        advanced_entropy = pb_picture->advanced_entropy_enabled;
        weighting_quant = pb_picture->weighting_quant_flag;
    }
    return progressive_frame == 0U && picture_structure == 0U &&
        advanced_entropy != 0U && weighting_quant == 0U;
}

/*
 * GB/T 20090.2-2013 9.10.2 and GB/T 20090.16-2016 9.9.2 use quarter-sample
 * luma motion for the currently supported profiles. Eighth-sample motion is
 * selected only by profiles whose explicit advanced-subpixel syntax has been
 * parsed; those profiles are outside the current sequence parser.
 */
static cavs_result broadcast_motion_profile_capability(
    const cavs_sequence_info *sequence,
    cavs_motion_profile_capability *capability) {
    if (sequence == NULL || capability == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (sequence->profile_id != UINT8_C(0x48))
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    capability->luma_precision = CAVS_LUMA_MOTION_QUARTER;
    capability->motion_unit = 4U;
    return CAVS_OK;
}

cavs_result cavs_broadcast_loop_filter_config(
    cavs_picture_type picture_type, const cavs_i_picture_header *i_picture,
    const cavs_pb_picture_header *pb_picture, const cavs_picture *picture,
    cavs_loop_filter_config *filter) {
    cavs_motion_profile_capability motion;
    if (i_picture == NULL || pb_picture == NULL || picture == NULL ||
        filter == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    motion.luma_precision = CAVS_LUMA_MOTION_QUARTER;
    motion.motion_unit = 4U;
    memset(filter, 0, sizeof(*filter));
    filter->enabled = (uint8_t)(picture_type == CAVS_PICTURE_I ?
        i_picture->loop_filter_disable == 0U :
        pb_picture->loop_filter_disable == 0U);
    filter->motion_unit = motion.motion_unit;
    filter->first_field = picture->top_field_first != 0U ?
        CAVS_FIELD_TOP : CAVS_FIELD_BOTTOM;
    filter->alpha_c_offset = picture_type == CAVS_PICTURE_I ?
        i_picture->alpha_c_offset : pb_picture->alpha_c_offset;
    filter->beta_offset = picture_type == CAVS_PICTURE_I ?
        i_picture->beta_offset : pb_picture->beta_offset;
    filter->chroma_qp_delta_cb = picture_type == CAVS_PICTURE_I ?
        i_picture->chroma_quant_parameter_delta_cb :
        pb_picture->chroma_quant_parameter_delta_cb;
    filter->chroma_qp_delta_cr = picture_type == CAVS_PICTURE_I ?
        i_picture->chroma_quant_parameter_delta_cr :
        pb_picture->chroma_quant_parameter_delta_cr;
    return CAVS_OK;
}

static cavs_result broadcast_field_for_row(const cavs_picture *picture,
                                           uint16_t row, uint8_t *field) {
    uint16_t field_rows;
    uint8_t first;
    if (picture == NULL || field == NULL || picture->field_picture == 0U ||
        (picture->macroblock_height & 1U) != 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    field_rows = (uint16_t)(picture->macroblock_height / 2U);
    if (row != 0U && row != field_rows)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    first = picture->top_field_first != 0U ? CAVS_FIELD_TOP :
                                             CAVS_FIELD_BOTTOM;
    *field = row == 0U ? first :
        (first == CAVS_FIELD_TOP ? CAVS_FIELD_BOTTOM : CAVS_FIELD_TOP);
    return CAVS_OK;
}

/* Expands one run-coded skipped macroblock without inventing source bits. */
static void broadcast_skipped_macroblock(
    cavs_broadcast_reader_state *reader, uint32_t address,
    cavs_macroblock *macroblock) {
    memset(macroblock, 0, sizeof(*macroblock));
    macroblock->address = address;
    macroblock->row = (uint16_t)(address /
        reader->picture->macroblock_width);
    macroblock->column = (uint16_t)(address %
        reader->picture->macroblock_width);
    macroblock->slice_id = reader->slice_id;
    macroblock->type = reader->picture_type == CAVS_PICTURE_B ?
        CAVS_MB_B_SKIP : CAVS_MB_P_SKIP;
    macroblock->raw_type = 0U;
    macroblock->is_skipped = 1U;
    macroblock->transform_8x8 = 1U;
    macroblock->partition_count = 1U;
    macroblock->partition[0].width = 16U;
    macroblock->partition[0].height = 16U;
    macroblock->partition[0].direction =
        reader->picture_type == CAVS_PICTURE_B ?
        CAVS_PRED_BIDIRECTIONAL : CAVS_PRED_FORWARD;
    macroblock->qp = reader->previous_qp;
    macroblock->end_bit_offset = reader->last_bit_offset;
}

/* Copies the current field's ordered DPB references into motion state. */
static cavs_result broadcast_motion_references(
    const cavs_dpb *dpb, uint8_t field,
    cavs_broadcast_motion_context *motion) {
    unsigned direction;
    unsigned index;
    if (dpb == NULL || motion == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
        for (index = 0U; index < CAVS_BROADCAST_REFERENCE_COUNT; ++index) {
            cavs_dpb_reference reference;
            cavs_result result = cavs_dpb_select_reference_entry(
                dpb, (uint8_t)direction, (uint8_t)index, field, &reference);
            if (result == CAVS_ERR_MISSING_REFERENCE) break;
            if (result != CAVS_OK) return result;
            motion->reference[direction][index].picture = reference.picture;
            motion->reference[direction][index].distance_index =
                reference.distance;
            motion->reference[direction][index].block_distance =
                reference.block_distance;
            motion->reference[direction][index].field = reference.field;
            motion->reference[direction][index].valid = 1U;
        }
    }
    return CAVS_OK;
}

/* Returns the committed partition covering one co-located 8x8 center. */
static const cavs_mb_partition *broadcast_partition_at(
    const cavs_macroblock *macroblock, uint8_t block) {
    unsigned index;
    uint8_t x = (uint8_t)((block & 1U) * 8U + 4U);
    uint8_t y = (uint8_t)((block >> 1U) * 8U + 4U);
    if (macroblock == NULL || block >= CAVS_MB_MOTION_BLOCKS)
        return NULL;
    for (index = 0U; index < macroblock->partition_count; ++index) {
        const cavs_mb_partition *partition = &macroblock->partition[index];
        if (x >= partition->x &&
            (unsigned)x < (unsigned)partition->x + partition->width &&
            y >= partition->y &&
            (unsigned)y < (unsigned)partition->y + partition->height)
            return partition;
    }
    return NULL;
}

/* Returns whether one entropy macroblock contains direct-mode partitions. */
static int broadcast_macroblock_uses_direct(
    const cavs_macroblock *macroblock) {
    unsigned index;
    if (macroblock->type == CAVS_MB_B_SKIP ||
        macroblock->type == CAVS_MB_B_DIRECT)
        return 1;
    if (macroblock->type != CAVS_MB_B_INTER) return 0;
    for (index = 0U; index < macroblock->partition_count; ++index) {
        const cavs_mb_partition *partition = &macroblock->partition[index];
        if (partition->direction == CAVS_PRED_BIDIRECTIONAL &&
            partition->motion[CAVS_PRED_FORWARD].valid == 0U &&
            partition->motion[CAVS_PRED_BACKWARD].valid == 0U)
            return 1;
    }
    return 0;
}

/* Maps a selected reference and frame-sample position to frozen MB metadata. */
static cavs_result broadcast_colocated_macroblock(
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
        uint8_t first = selected.picture->top_field_first != 0U ?
            CAVS_FIELD_TOP : CAVS_FIELD_BOTTOM;
        metadata_field = selected.field;
        if (metadata_field == CAVS_FIELD_BOTH)
            metadata_field = (sample_y & 1U) == 0U ? CAVS_FIELD_TOP :
                                                    CAVS_FIELD_BOTTOM;
        ordinal = metadata_field == first ? 0U : 1U;
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

/*
 * GB/T 20090.16-2016 9.6.1 assigns DistanceIndex and BlockDistance to the
 * reference block selected while decoding each motion vector. Clause 9.9.1
 * b) later requires the co-located P vector's DistanceIndexRef (and, for
 * field correction, its physical field). A numeric reference_index is only
 * meaningful in that P field's transient list, so freeze the resolved
 * identity before a newer reference picture can replace an older DPB entry.
 */
static cavs_result broadcast_freeze_motion_references(
    const cavs_broadcast_motion_context *context,
    cavs_macroblock *macroblock) {
    unsigned partition_index;
    if (context == NULL || macroblock == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (partition_index = 0U;
         partition_index < macroblock->partition_count; ++partition_index) {
        unsigned direction;
        for (direction = 0U; direction < CAVS_MB_DIRECTIONS; ++direction) {
            cavs_motion_vector *motion =
                &macroblock->partition[partition_index].motion[direction];
            const cavs_broadcast_reference *reference;
            if (motion->valid == 0U) continue;
            if (motion->valid > 1U || motion->reference_index < 0 ||
                motion->reference_index >=
                    (int8_t)CAVS_BROADCAST_REFERENCE_COUNT)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            reference = &context->reference[direction]
                [(unsigned)motion->reference_index];
            if (reference->valid == 0U)
                return CAVS_ERR_MISSING_REFERENCE;
            if (reference->valid > 1U || reference->distance_index >= 512U ||
                (reference->field != CAVS_FIELD_TOP &&
                 reference->field != CAVS_FIELD_BOTTOM &&
                 reference->field != CAVS_FIELD_BOTH))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            motion->reference_distance_index = reference->distance_index;
            motion->reference_field = reference->field;
            motion->reference_identity_valid = 1U;
        }
    }
    return CAVS_OK;
}

/*
 * Implements GB/T 20090.16-2016 9.9.1, Figures 26-29: locate each
 * co-located block in the default backward P field and retain its forward
 * vector plus the exact field reference selected by that vector.
 */
static cavs_result broadcast_colocated_motion(
    cavs_broadcast_reader_state *reader, const cavs_macroblock *macroblock) {
    size_t field_rows;
    size_t local_row;
    uint32_t base_x;
    uint32_t base_y;
    uint8_t parity;
    uint8_t block;
    if (reader == NULL || macroblock == NULL || reader->dpb == NULL ||
        reader->picture == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    memset(reader->motion.colocated, 0,
           sizeof(reader->motion.colocated));
    if (reader->picture_type != CAVS_PICTURE_B ||
        !broadcast_macroblock_uses_direct(macroblock))
        return CAVS_OK;
    if (reader->picture_structure != 0U)
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    field_rows = reader->picture->macroblock_height / 2U;
    if (field_rows == 0U) return CAVS_ERR_INVALID_STATE;
    local_row = macroblock->row % field_rows;
    parity = reader->motion.current_field == CAVS_FIELD_BOTTOM ? 1U : 0U;
    base_x = (uint32_t)macroblock->column * 16U;
    base_y = (uint32_t)local_row * 32U + parity;
    for (block = 0U; block < CAVS_MB_MOTION_BLOCKS; ++block) {
        cavs_dpb_reference colocated_reference;
        const cavs_macroblock *colocated_macroblock;
        const cavs_mb_partition *partition;
        const cavs_motion_vector *motion;
        uint8_t colocated_block;
        uint32_t sample_x = base_x + (uint32_t)(block & 1U) * 8U + 4U;
        uint32_t sample_y = base_y +
            ((uint32_t)(block >> 1U) * 8U + 4U) * 2U;
        cavs_broadcast_colocated_block *destination =
            &reader->motion.colocated[block];
        cavs_result result = broadcast_colocated_macroblock(
            reader->dpb, CAVS_DPB_BACKWARD,
            reader->motion.default_reference_index[CAVS_PRED_BACKWARD],
            reader->motion.current_field, sample_x, sample_y,
            &colocated_reference, &colocated_macroblock, &colocated_block);
        if (result != CAVS_OK) return result;
        destination->available = 1U;
        destination->intra = colocated_macroblock->is_intra;
        destination->distance_index = colocated_reference.distance;
        destination->picture_structure =
            (uint8_t)(colocated_reference.picture->field_picture == 0U);
        destination->field = colocated_reference.field;
        if (destination->intra != 0U) continue;
        partition = broadcast_partition_at(colocated_macroblock,
                                           colocated_block);
        if (partition == NULL) return CAVS_ERR_INVALID_STATE;
        motion = &partition->motion[CAVS_PRED_FORWARD];
        if (motion->valid == 0U || motion->reference_index < 0 ||
            motion->reference_identity_valid == 0U)
            return CAVS_ERR_MISSING_REFERENCE;
        if (motion->reference_identity_valid > 1U ||
            motion->reference_distance_index >= 512U ||
            (motion->reference_field != CAVS_FIELD_TOP &&
             motion->reference_field != CAVS_FIELD_BOTTOM &&
             motion->reference_field != CAVS_FIELD_BOTH))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        destination->motion.x = motion->x;
        destination->motion.y = motion->y;
        destination->reference_distance_index =
            motion->reference_distance_index;
        /*
         * GB/T 20090.16-2016 9.9.1 b) step 2:
         * BlockDistanceRef = (DistanceIndexCol - DistanceIndexRef + 512)
         *                    % 512.
         */
        destination->block_distance = (uint16_t)(
            ((unsigned)colocated_reference.distance +
             CAVS_DPB_DISTANCE_MODULUS -
             motion->reference_distance_index) %
            CAVS_DPB_DISTANCE_MODULUS);
        if (destination->block_distance == 0U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        destination->reference_field = motion->reference_field;
    }
    return CAVS_OK;
}

/* Normalizes inter motion and produces this macroblock's prediction samples. */
static cavs_result prepare_broadcast_inter_prediction(
    cavs_broadcast_reader_state *reader, cavs_macroblock *macroblock) {
    cavs_macroblock assembled;
    cavs_result result;
    if (reader == NULL || macroblock == NULL || reader->prediction == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (macroblock->is_intra != 0U) return CAVS_OK;
    result = broadcast_colocated_motion(reader, macroblock);
    if (result != CAVS_OK) return result;
    result = cavs_assemble_broadcast_picture_macroblock_motion(
        &reader->motion, reader->picture, macroblock, &assembled);
    if (result != CAVS_OK) return result;
    result = broadcast_freeze_motion_references(&reader->motion, &assembled);
    if (result != CAVS_OK) return result;
    result = cavs_predict_broadcast_macroblock_420(
        &reader->motion, &assembled, reader->prediction);
    if (result != CAVS_OK) return result;
    *macroblock = assembled;
    return CAVS_OK;
}

/* Reads one explicit or run-expanded broadcast macroblock atomically. */
static cavs_result read_broadcast_macroblock(
    void *opaque, uint32_t macroblock_address, cavs_macroblock *macroblock) {
    cavs_broadcast_reader_state *reader =
        (cavs_broadcast_reader_state *)opaque;
    cavs_broadcast_reader_state working;
    cavs_macroblock parsed;
    cavs_macroblock entropy_macroblock;
    cavs_broadcast_mb_context context;
    uint32_t field_end;
    uint32_t field_start;
    int predicted_field;
    cavs_result result;
    if (reader == NULL || macroblock == NULL || reader->picture == NULL ||
        reader->entropy_row == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    working = *reader;
    field_start = (uint32_t)working.start_row *
        working.picture->macroblock_width;
    field_end = field_start +
        (uint32_t)(working.picture->macroblock_height / 2U) *
        working.picture->macroblock_width;
    if (macroblock_address == field_end)
        return cavs_broadcast_slice_finish(&working.entropy);
    if (macroblock_address < field_start || macroblock_address > field_end)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    predicted_field = working.picture_type != CAVS_PICTURE_I ||
        (working.picture_structure == 0U &&
         macroblock_address >= working.picture->macroblock_count / 2U);
    if (working.skip_remaining == 0U && working.explicit_pending == 0U &&
        working.skip_mode_flag != 0U && predicted_field) {
        result = cavs_broadcast_decode_skip_run(
            &working.entropy, field_end - macroblock_address,
            &working.skip_remaining);
        if (result != CAVS_OK) return result;
        working.explicit_pending = 1U;
        working.last_bit_offset = cavs_ae_bit_offset(
            &working.entropy.arithmetic);
        if (working.skip_remaining != 0U)
            working.previous_qp_delta = 0;
    }
    if (working.skip_remaining != 0U) {
        broadcast_skipped_macroblock(&working, macroblock_address, &parsed);
        --working.skip_remaining;
        entropy_macroblock = parsed;
        result = prepare_broadcast_inter_prediction(&working, &parsed);
        if (result != CAVS_OK) return result;
        working.entropy_row[parsed.column] = entropy_macroblock;
        *reader = working;
        *macroblock = parsed;
        return CAVS_OK;
    }
    working.explicit_pending = 0U;
    memset(&context, 0, sizeof(context));
    context.profile_id = UINT8_C(0x48);
    context.format = CAVS_YUV420P8;
    context.picture_type = working.picture_type;
    context.progressive_frame = working.progressive_frame;
    context.picture_structure = working.picture_structure;
    context.skip_mode_flag = working.skip_mode_flag;
    context.picture_reference_flag = working.picture_reference_flag;
    context.fixed_qp = working.fixed_qp;
    context.previous_qp = working.previous_qp;
    context.previous_qp_delta = working.previous_qp_delta;
    context.mb_weighting_flag = working.mb_weighting_flag;
    context.macroblock_index = macroblock_address;
    context.macroblock_width = working.picture->macroblock_width;
    context.macroblock_height = working.picture->macroblock_height;
    context.slice_id = working.slice_id;
    if (macroblock_address % working.picture->macroblock_width != 0U)
        context.left = &working.entropy_row[
            macroblock_address % working.picture->macroblock_width - 1U];
    if (macroblock_address >= field_start +
            working.picture->macroblock_width)
        context.top = &working.entropy_row[
            macroblock_address % working.picture->macroblock_width];
    result = cavs_decode_broadcast_macroblock(&working.entropy, &context,
                                              &parsed);
    if (result != CAVS_OK) return result;
    working.previous_qp = parsed.qp;
    working.previous_qp_delta = parsed.qp_delta;
    working.last_bit_offset = parsed.end_bit_offset;
    entropy_macroblock = parsed;
    result = prepare_broadcast_inter_prediction(&working, &parsed);
    if (result != CAVS_OK) return result;
    working.entropy_row[parsed.column] = entropy_macroblock;
    *reader = working;
    *macroblock = parsed;
    return CAVS_OK;
}

cavs_result cavs_broadcast_decode_slice(
    const cavs_broadcast_decode_context *decode,
    const uint8_t *data, size_t bit_size, uint8_t *decoded_field) {
    cavs_picture *picture;
    cavs_broadcast_reader_state reader;
    cavs_macroblock *entropy_row;
    cavs_broadcast_reconstruction_context reconstruction;
    cavs_slice_cursor cursor;
    uint8_t field;
    uint8_t first;
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t skip_mode_flag;
    uint8_t picture_reference_flag;
    int8_t chroma_delta_cb;
    int8_t chroma_delta_cr;
    cavs_motion_profile_capability motion;
    cavs_result result;
    if (decode == NULL || decode->config == NULL ||
        decode->sequence == NULL || decode->dpb == NULL ||
        decode->frame == NULL || decode->i_picture == NULL ||
        decode->pb_picture == NULL || decode->slice == NULL ||
        decode->prediction == NULL || decode->next_slice_id == NULL ||
        decoded_field == NULL || data == NULL ||
        decode->slice->header_bits > bit_size)
        return decode == NULL || data == NULL || decoded_field == NULL ?
            CAVS_ERR_INVALID_ARGUMENT : CAVS_ERR_CORRUPT_BITSTREAM;
    picture = cavs_frame_picture(decode->frame);
    result = broadcast_field_for_row(picture,
                                     decode->slice->macroblock_row, &field);
    if (result != CAVS_OK) return result;
    first = picture->top_field_first != 0U ? CAVS_FIELD_TOP :
                                            CAVS_FIELD_BOTTOM;
    if ((picture->completed_fields == 0U && field != first) ||
        (picture->completed_fields == first && field == first) ||
        (picture->completed_fields != 0U &&
         picture->completed_fields != first))
        return CAVS_ERR_INVALID_STATE;
    result = cavs_dpb_begin_picture(decode->dpb, picture, field);
    if (result != CAVS_OK) return result;
    if (decode->picture_type == CAVS_PICTURE_I) {
        progressive_frame = decode->i_picture->progressive_frame;
        picture_structure = decode->i_picture->picture_structure;
        skip_mode_flag = decode->i_picture->skip_mode_flag;
        picture_reference_flag = 1U;
        chroma_delta_cb = decode->i_picture->chroma_quant_parameter_delta_cb;
        chroma_delta_cr = decode->i_picture->chroma_quant_parameter_delta_cr;
    } else {
        progressive_frame = decode->pb_picture->progressive_frame;
        picture_structure = decode->pb_picture->picture_structure;
        skip_mode_flag = decode->pb_picture->skip_mode_flag;
        picture_reference_flag = decode->pb_picture->picture_reference_flag;
        chroma_delta_cb = decode->pb_picture->chroma_quant_parameter_delta_cb;
        chroma_delta_cr = decode->pb_picture->chroma_quant_parameter_delta_cr;
    }
    memset(&reader, 0, sizeof(reader));
    reader.dpb = decode->dpb;
    reader.picture = picture;
    reader.picture_type = decode->picture_type;
    reader.progressive_frame = progressive_frame;
    reader.picture_structure = picture_structure;
    reader.skip_mode_flag = skip_mode_flag;
    reader.picture_reference_flag = picture_reference_flag;
    reader.fixed_qp = decode->slice->fixed_slice_qp;
    reader.previous_qp = decode->slice->slice_qp;
    reader.mb_weighting_flag = decode->slice->mb_weighting_flag;
    reader.slice_id = (*decode->next_slice_id)++;
    reader.start_row = decode->slice->macroblock_row;
    reader.prediction = decode->prediction;
    reader.motion.picture_type = decode->picture_type;
    reader.motion.picture_structure = picture_structure;
    reader.motion.current_field = field;
    reader.motion.second_field = (uint8_t)(field != first);
    reader.motion.pb_field_enhanced = decode->picture_type == CAVS_PICTURE_I ?
        0U : decode->pb_picture->pb_field_enhanced_flag;
    result = broadcast_motion_profile_capability(decode->sequence, &motion);
    if (result != CAVS_OK) return result;
    reader.motion.precision = motion.luma_precision;
    reader.motion.current_distance_index = decode->dpb->current_distance;
    reader.motion.default_reference_index[CAVS_PRED_FORWARD] = 0U;
    reader.motion.default_reference_index[CAVS_PRED_BACKWARD] = 0U;
    result = broadcast_motion_references(decode->dpb, field,
                                         &reader.motion);
    if (result != CAVS_OK) return result;
    entropy_row = (cavs_macroblock *)decode->config->alloc(
        decode->config->allocator_opaque,
        (size_t)picture->macroblock_width * sizeof(*entropy_row));
    if (entropy_row == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(entropy_row, 0,
        (size_t)picture->macroblock_width * sizeof(*entropy_row));
    reader.entropy_row = entropy_row;
    result = cavs_broadcast_slice_init(&reader.entropy, data, bit_size,
                                       decode->slice->header_bits);
    if (result != CAVS_OK) {
        decode->config->free(decode->config->allocator_opaque, entropy_row);
        return result;
    }
    reader.last_bit_offset = cavs_ae_bit_offset(&reader.entropy.arithmetic);
    memset(&reconstruction, 0, sizeof(reconstruction));
    reconstruction.picture = picture;
    reconstruction.inter_prediction = decode->prediction;
    reconstruction.slice_id = reader.slice_id;
    reconstruction.field = field;
    reconstruction.chroma_qp_delta_cb = chroma_delta_cb;
    reconstruction.chroma_qp_delta_cr = chroma_delta_cr;
    result = cavs_slice_cursor_init(picture, field,
        decode->slice->macroblock_row, decode->slice->header_bits,
        bit_size, &cursor);
    if (result == CAVS_OK)
        result = cavs_slice_decode(&cursor, read_broadcast_macroblock, &reader,
                                   &reconstruction);
    decode->config->free(decode->config->allocator_opaque, entropy_row);
    if (result != CAVS_OK) return result;
    *decoded_field = field;
    return CAVS_OK;
}
