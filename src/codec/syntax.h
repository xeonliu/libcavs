/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Parsers for standard-defined video syntax structures.
 */
#ifndef CAVS_SYNTAX_H
#define CAVS_SYNTAX_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

/** Start-code values from GB/T 20090.2-2013, Table 12. */
typedef enum cavs_unit_type {
    CAVS_UNIT_SLICE = 0,
    CAVS_UNIT_SEQUENCE_HEADER,
    CAVS_UNIT_SEQUENCE_END,
    CAVS_UNIT_USER_DATA,
    CAVS_UNIT_I_PICTURE,
    CAVS_UNIT_EXTENSION,
    CAVS_UNIT_PB_PICTURE,
    CAVS_UNIT_VIDEO_EDIT,
    CAVS_UNIT_RESERVED,
    CAVS_UNIT_SYSTEM
} cavs_unit_type;

/** Parsed I-picture header fields required by reconstruction and output. */
typedef struct cavs_i_picture_header {
    uint32_t bbv_delay;
    uint8_t has_time_code;
    uint32_t time_code;
    uint8_t picture_distance;
    uint32_t bbv_check_times;
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t top_field_first;
    uint8_t repeat_first_field;
    uint8_t fixed_picture_qp;
    uint8_t picture_qp;
    uint8_t skip_mode_flag;
    uint8_t loop_filter_disable;
    uint8_t loop_filter_parameter_flag;
    int8_t alpha_c_offset;
    int8_t beta_offset;
    uint8_t weighting_quant_flag;
    uint8_t chroma_quant_parameter_disable;
    int8_t chroma_quant_parameter_delta_cb;
    int8_t chroma_quant_parameter_delta_cr;
    uint8_t weighting_quant_parameter_index;
    uint8_t weighting_quant_model;
    int8_t weighting_quant_parameter_delta1[6];
    int8_t weighting_quant_parameter_delta2[6];
    uint8_t advanced_entropy_enabled;
} cavs_i_picture_header;

/** Parsed P/B-picture header fields required by reference management. */
typedef struct cavs_pb_picture_header {
    uint32_t bbv_delay;
    uint8_t picture_coding_type;
    uint8_t picture_distance;
    uint32_t bbv_check_times;
    uint8_t progressive_frame;
    uint8_t picture_structure;
    uint8_t advanced_prediction_mode_disable;
    uint8_t top_field_first;
    uint8_t repeat_first_field;
    uint8_t fixed_picture_qp;
    uint8_t picture_qp;
    uint8_t picture_reference_flag;
    uint8_t no_forward_reference_flag;
    uint8_t pb_field_enhanced_flag;
    uint8_t skip_mode_flag;
    uint8_t loop_filter_disable;
    uint8_t loop_filter_parameter_flag;
    int8_t alpha_c_offset;
    int8_t beta_offset;
    uint8_t weighting_quant_flag;
    uint8_t chroma_quant_parameter_disable;
    int8_t chroma_quant_parameter_delta_cb;
    int8_t chroma_quant_parameter_delta_cr;
    uint8_t weighting_quant_parameter_index;
    uint8_t weighting_quant_model;
    int8_t weighting_quant_parameter_delta1[6];
    int8_t weighting_quant_parameter_delta2[6];
    uint8_t advanced_entropy_enabled;
} cavs_pb_picture_header;

#define CAVS_MAX_SLICE_REFERENCES 4U

/** Picture state required to interpret a target-profile slice header. */
typedef struct cavs_slice_context {
    uint8_t profile_id;
    uint32_t vertical_size;
    uint32_t macroblock_height;
    cavs_picture_type picture_type;
    uint8_t picture_structure;
    uint8_t fixed_picture_qp;
    uint8_t picture_qp;
    uint8_t advanced_entropy_enabled;
    uint8_t number_of_references;
} cavs_slice_context;

/** Parsed fields shared by baseline and broadcast slice headers. */
typedef struct cavs_slice_header {
    uint16_t macroblock_row;
    uint8_t fixed_slice_qp;
    uint8_t slice_qp;
    uint8_t slice_weighting_flag;
    uint8_t number_of_references;
    uint8_t luma_scale[CAVS_MAX_SLICE_REFERENCES];
    int8_t luma_shift[CAVS_MAX_SLICE_REFERENCES];
    uint8_t chroma_scale[CAVS_MAX_SLICE_REFERENCES];
    int8_t chroma_shift[CAVS_MAX_SLICE_REFERENCES];
    uint8_t mb_weighting_flag;
    size_t header_bits;
} cavs_slice_header;

/* Classifies one eight-bit start-code value. */
cavs_unit_type cavs_classify_start_code(uint8_t start_code);

/* Parses and validates the common baseline/broadcast sequence header. */
cavs_result cavs_parse_sequence_header(const uint8_t *data, size_t size,
                                       cavs_sequence_info *sequence);

/* Parses an I-picture header using state established by its sequence header. */
cavs_result cavs_parse_i_picture_header(const uint8_t *data, size_t bit_size,
                                        const cavs_sequence_info *sequence,
                                        cavs_i_picture_header *picture);

/* Parses a P/B-picture header using state established by its sequence header. */
cavs_result cavs_parse_pb_picture_header(const uint8_t *data, size_t bit_size,
                                         const cavs_sequence_info *sequence,
                                         cavs_pb_picture_header *picture);

/* Parses fields before the first macroblock in a target-profile slice. */
cavs_result cavs_parse_slice_header(uint8_t start_code, const uint8_t *data,
                                    size_t bit_size,
                                    const cavs_slice_context *context,
                                    cavs_slice_header *slice);

#endif
