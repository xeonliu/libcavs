/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Broadcast-profile inter-macroblock motion assembly.
 */
#ifndef CAVS_BROADCAST_MOTION_H
#define CAVS_BROADCAST_MOTION_H

#include "codec/macroblock.h"
#include "codec/motion.h"
#include "codec/syntax.h"
#include "codec/broadcast_weighting.h"
#include <stdint.h>

#define CAVS_BROADCAST_REFERENCE_COUNT 4U
#define CAVS_BROADCAST_NO_PARTITION (-1)
#define CAVS_BROADCAST_FIELD_TOP UINT8_C(1)
#define CAVS_BROADCAST_FIELD_BOTTOM UINT8_C(2)
#define CAVS_BROADCAST_FIELD_FRAME UINT8_C(3)

struct cavs_picture;
struct cavs_macroblock_prediction_420;

typedef enum cavs_broadcast_motion_mode {
    CAVS_BROADCAST_MOTION_FORWARD = 0,
    CAVS_BROADCAST_MOTION_BACKWARD,
    CAVS_BROADCAST_MOTION_SYMMETRIC,
    CAVS_BROADCAST_MOTION_BIDIRECTIONAL,
    CAVS_BROADCAST_MOTION_DIRECT
} cavs_broadcast_motion_mode;

typedef struct cavs_broadcast_reference {
    const struct cavs_picture *picture;
    uint16_t distance_index;
    uint16_t block_distance;
    uint8_t field;
    uint8_t valid;
} cavs_broadcast_reference;

typedef struct cavs_broadcast_motion_candidate {
    cavs_motion_candidate value;
    int8_t source_partition;
} cavs_broadcast_motion_candidate;

typedef struct cavs_broadcast_motion_partition {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
    cavs_broadcast_motion_mode mode;
    int8_t reference_index[CAVS_MB_DIRECTIONS];
    cavs_luma_motion_vector difference[CAVS_MB_DIRECTIONS];
    cavs_broadcast_motion_candidate
        candidate[CAVS_MB_DIRECTIONS][CAVS_MOTION_NEIGHBOR_COUNT];
} cavs_broadcast_motion_partition;

typedef struct cavs_broadcast_colocated_block {
    cavs_luma_motion_vector motion;
    uint16_t distance_index;
    uint16_t reference_distance_index;
    uint16_t block_distance;
    uint8_t picture_structure;
    uint8_t field;
    uint8_t reference_field;
    uint8_t available;
    uint8_t intra;
} cavs_broadcast_colocated_block;

typedef struct cavs_broadcast_motion_context {
    cavs_picture_type picture_type;
    uint8_t picture_structure;
    uint8_t current_field;
    uint8_t second_field;
    uint8_t pb_field_enhanced;
    /* GB/T 20090.16-2016 7.3.2.5: prohibit forward reference candidates. */
    uint8_t no_forward_reference;
    /* GB/T 20090.16-2016 7.4.6, 7.4.8-7.4.11 and 9.3. */
    uint8_t slice_weighting_flag;
    uint8_t mb_weighting_flag;
    uint8_t weight_parameter_count;
    uint8_t luma_scale[CAVS_BROADCAST_WEIGHT_COUNT];
    int8_t luma_shift[CAVS_BROADCAST_WEIGHT_COUNT];
    uint8_t chroma_scale[CAVS_BROADCAST_WEIGHT_COUNT];
    int8_t chroma_shift[CAVS_BROADCAST_WEIGHT_COUNT];
    cavs_luma_motion_precision precision;
    uint16_t current_distance_index;
    uint8_t default_reference_index[CAVS_MB_DIRECTIONS];
    cavs_broadcast_reference
        reference[CAVS_MB_DIRECTIONS][CAVS_BROADCAST_REFERENCE_COUNT];
    cavs_broadcast_colocated_block colocated[CAVS_MB_MOTION_BLOCKS];
} cavs_broadcast_motion_context;

typedef struct cavs_broadcast_motion_syntax {
    cavs_macroblock_type type;
    uint8_t partition_count;
    cavs_broadcast_motion_partition partition[CAVS_MAX_MB_PARTITIONS];
} cavs_broadcast_motion_syntax;

/*
 * GB/T 20090.16-2016 9.4.6 and 9.9.1, Tables 55-57 and Figures 24-29:
 * (1) validate the P/B partition template and explicit reference state;
 * (2) resolve A/B/C/D candidates, including earlier partitions of this MB;
 * (3) derive P_Skip, decoded P/B, symmetric, or direct vectors;
 * (4) apply PB-field-enhanced direct-mode field/reference corrections;
 * (5) atomically write normalized partitions to the unified macroblock.
 * An I picture's predicted second field uses the same P path.
 */
cavs_result cavs_assemble_broadcast_macroblock_motion(
    const cavs_broadcast_motion_context *context,
    const cavs_broadcast_motion_syntax *syntax,
    cavs_macroblock *macroblock);

/*
 * GB/T 20090.16-2016 9.4.3 and 9.4.6, Figures 11-12 and Table 58:
 * (1) validate the entropy macroblock against frozen picture coordinates;
 * (2) map its direction, reference-index, and MVD fields to motion syntax;
 * (3) acquire A/B/C/D only from earlier same-field, same-slice blocks;
 * (4) preserve earlier partitions of this macroblock as local candidates;
 * (5) atomically derive and write the normalized unified motion metadata.
 * This picture-storage adapter introduces no additional prediction mode.
 */
cavs_result cavs_assemble_broadcast_picture_macroblock_motion(
    const cavs_broadcast_motion_context *context,
    const struct cavs_picture *picture,
    const cavs_macroblock *entropy_macroblock,
    cavs_macroblock *macroblock);

/*
 * GB/T 20090.16-2016 9.9.2 and Table 63:
 * (1) expose the selected frame or physical field reference plane;
 * (2) interpolate each unified luma/chroma partition from its motion vector;
 * (3) average forward/backward predictions for bidirectional modes;
 * (4) atomically produce the YUV420 prediction consumed by broadcast
 * reconstruction. This storage adapter introduces no additional mode choice.
 */
cavs_result cavs_predict_broadcast_macroblock_420(
    const cavs_broadcast_motion_context *context,
    const cavs_macroblock *macroblock,
    struct cavs_macroblock_prediction_420 *prediction);

#endif
