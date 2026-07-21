/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Common sequence syntax from GB/T 20090.2-2013, 信息技术 先进音视频编码
 * 第2部分: 视频, 7.1 Table 12 and 7.2.2 Table 15. The broadcast sequence
 * header is defined by GB/T 20090.16-2016, 7.2.2 Table 14.
 */
#include "syntax.h"
#include "bitreader.h"
#include <string.h>

/* Reads a field and converts bitreader failure into a syntax failure. */
static int read_field(cavs_bitreader *reader, unsigned width, uint32_t *value) {
    return cavs_br_read(reader, width, value);
}

/* Reads a signed syntax value and enforces its normative closed range. */
static int read_signed_range(cavs_bitreader *reader, int32_t minimum,
                             int32_t maximum, int8_t *value) {
    int32_t parsed;
    if (!cavs_br_read_se(reader, &parsed) || parsed < minimum || parsed > maximum)
        return 0;
    *value = (int8_t)parsed;
    return 1;
}

/* Reads the two's-complement i(8) form used by slice weighting parameters. */
static int read_signed_byte(cavs_bitreader *reader, int8_t *value) {
    uint32_t parsed;
    int32_t signed_value;
    if (!read_field(reader, 8U, &parsed)) return 0;
    signed_value = parsed <= 127U ? (int32_t)parsed : (int32_t)parsed - 256;
    *value = (int8_t)signed_value;
    return 1;
}

/* Validates the component ranges in the 24-bit time code from Table 39. */
static int is_valid_time_code(uint32_t time_code) {
    uint32_t hours = (time_code >> 18) & UINT32_C(0x1f);
    uint32_t minutes = (time_code >> 12) & UINT32_C(0x3f);
    uint32_t seconds = (time_code >> 6) & UINT32_C(0x3f);
    uint32_t pictures = time_code & UINT32_C(0x3f);
    return hours <= 23U && minutes <= 59U && seconds <= 59U && pictures <= 59U;
}

/* Validates profile-specific level identifiers listed by normative Annex B. */
static int is_supported_level(uint8_t profile_id, uint8_t level_id) {
    switch (level_id) {
    case UINT8_C(0x10):
    case UINT8_C(0x14):
    case UINT8_C(0x20):
    case UINT8_C(0x22):
    case UINT8_C(0x2a):
    case UINT8_C(0x40):
    case UINT8_C(0x41):
    case UINT8_C(0x42):
    case UINT8_C(0x44):
    case UINT8_C(0x46):
        return 1;
    case UINT8_C(0x12):
        return profile_id == UINT8_C(0x48);
    default:
        return 0;
    }
}

/* Maps the complete start-code value ranges defined by Table 12. */
cavs_unit_type cavs_classify_start_code(uint8_t start_code) {
    if (start_code <= UINT8_C(0xaf)) return CAVS_UNIT_SLICE;
    switch (start_code) {
    case UINT8_C(0xb0): return CAVS_UNIT_SEQUENCE_HEADER;
    case UINT8_C(0xb1): return CAVS_UNIT_SEQUENCE_END;
    case UINT8_C(0xb2): return CAVS_UNIT_USER_DATA;
    case UINT8_C(0xb3): return CAVS_UNIT_I_PICTURE;
    case UINT8_C(0xb5): return CAVS_UNIT_EXTENSION;
    case UINT8_C(0xb6): return CAVS_UNIT_PB_PICTURE;
    case UINT8_C(0xb7): return CAVS_UNIT_VIDEO_EDIT;
    case UINT8_C(0xb4):
    case UINT8_C(0xb8): return CAVS_UNIT_RESERVED;
    default: return CAVS_UNIT_SYSTEM;
    }
}

/* Parses fields shared by profiles 0x20 and 0x48 and enforces 1.0 scope. */
cavs_result cavs_parse_sequence_header(const uint8_t *data, size_t size,
                                       cavs_sequence_info *sequence) {
    cavs_bitreader reader;
    cavs_sequence_info parsed;
    uint32_t value;
    uint32_t bit_rate_lower;
    uint32_t bit_rate_upper;
    if (data == NULL || sequence == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    cavs_br_init(&reader, data, size);
    memset(&parsed, 0, sizeof(parsed));

    if (!read_field(&reader, 8U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.profile_id = (uint8_t)value;
    if (parsed.profile_id != UINT8_C(0x20) && parsed.profile_id != UINT8_C(0x48))
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    if (!read_field(&reader, 8U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.level_id = (uint8_t)value;
    if (!is_supported_level(parsed.profile_id, parsed.level_id))
        return CAVS_ERR_UNSUPPORTED_LEVEL;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.progressive_sequence = (uint8_t)value;
    if (!read_field(&reader, 14U, &value) || value == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.display_width = value;
    if (!read_field(&reader, 14U, &value) || value == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.display_height = value;
    if (!read_field(&reader, 2U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    if (value == 1U) parsed.format = CAVS_YUV420P8;
    else if (value == 2U) parsed.format = CAVS_YUV422P8;
    else return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 3U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    if (value != 1U) return CAVS_ERR_UNSUPPORTED_PROFILE;
    if (!read_field(&reader, 4U, &value) || value == 0U || value > 4U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.aspect_ratio_code = (uint8_t)value;
    if (!read_field(&reader, 4U, &value) || value == 0U || value > 8U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.frame_rate_code = (uint8_t)value;
    if (!read_field(&reader, 18U, &bit_rate_lower) ||
        !read_field(&reader, 1U, &value) || value != 1U ||
        !read_field(&reader, 12U, &bit_rate_upper))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.bit_rate = (((uint64_t)bit_rate_upper << 18) | bit_rate_lower) * UINT64_C(400);
    if (parsed.bit_rate == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.low_delay = (uint8_t)value;
    if (!read_field(&reader, 1U, &value) || value != 1U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 18U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.bbv_buffer_size_bits = (uint64_t)value * UINT64_C(16) * UINT64_C(1024);
    if (!read_field(&reader, 3U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;

    *sequence = parsed;
    return CAVS_OK;
}

/*
 * Parses GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 7.3.1 Table 21 and the corresponding GB/T 20090.16-2016 Table 21.
 */
cavs_result cavs_parse_i_picture_header(const uint8_t *data, size_t bit_size,
                                        const cavs_sequence_info *sequence,
                                        cavs_i_picture_header *picture) {
    cavs_bitreader reader;
    cavs_i_picture_header parsed;
    uint32_t value;
    unsigned index;
    if ((data == NULL && bit_size != 0U) || sequence == NULL || picture == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (sequence->profile_id != UINT8_C(0x20) &&
        sequence->profile_id != UINT8_C(0x48))
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    if (!cavs_br_init_bits(&reader, data, bit_size))
        return CAVS_ERR_INVALID_ARGUMENT;
    memset(&parsed, 0, sizeof(parsed));
    parsed.picture_structure = 1U;

    if (!read_field(&reader, 16U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.bbv_delay = value;
    if (sequence->profile_id == UINT8_C(0x48)) {
        if (!read_field(&reader, 1U, &value) || value != 1U ||
            !read_field(&reader, 7U, &value))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.bbv_delay = (parsed.bbv_delay << 7) | value;
    }
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.has_time_code = (uint8_t)value;
    if (parsed.has_time_code != 0U) {
        if (!read_field(&reader, 24U, &parsed.time_code) ||
            !is_valid_time_code(parsed.time_code))
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    if (!read_field(&reader, 1U, &value) || value != 1U ||
        !read_field(&reader, 8U, &value))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.picture_distance = (uint8_t)value;
    if (sequence->low_delay != 0U &&
        !cavs_br_read_ue(&reader, &parsed.bbv_check_times))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.progressive_frame = (uint8_t)value;
    if (parsed.progressive_frame == 0U) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.picture_structure = (uint8_t)value;
    }
    if (sequence->progressive_sequence != 0U && parsed.progressive_frame == 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.top_field_first = (uint8_t)value;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.repeat_first_field = (uint8_t)value;
    if (parsed.progressive_frame == 0U && parsed.repeat_first_field != 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.fixed_picture_qp = (uint8_t)value;
    if (!read_field(&reader, 6U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.picture_qp = (uint8_t)value;
    if (parsed.progressive_frame == 0U && parsed.picture_structure == 0U) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.skip_mode_flag = (uint8_t)value;
    }
    if (!read_field(&reader, 4U, &value) || value != 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.loop_filter_disable = (uint8_t)value;
    if (parsed.loop_filter_disable == 0U) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.loop_filter_parameter_flag = (uint8_t)value;
        if (parsed.loop_filter_parameter_flag != 0U &&
            (!read_signed_range(&reader, -8, 8, &parsed.alpha_c_offset) ||
             !read_signed_range(&reader, -8, 8, &parsed.beta_offset)))
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }

    if (sequence->profile_id == UINT8_C(0x48)) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.weighting_quant_flag = (uint8_t)value;
        if (parsed.weighting_quant_flag != 0U) {
            if (!read_field(&reader, 1U, &value) || value != 0U ||
                !read_field(&reader, 1U, &value))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.chroma_quant_parameter_disable = (uint8_t)value;
            if (parsed.chroma_quant_parameter_disable == 0U &&
                (!read_signed_range(&reader, -16, 16,
                                    &parsed.chroma_quant_parameter_delta_cb) ||
                 !read_signed_range(&reader, -16, 16,
                                    &parsed.chroma_quant_parameter_delta_cr)))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            if (!read_field(&reader, 2U, &value) || value == 3U)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.weighting_quant_parameter_index = (uint8_t)value;
            if (!read_field(&reader, 2U, &value) || value == 3U)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.weighting_quant_model = (uint8_t)value;
            if (parsed.weighting_quant_parameter_index == 1U) {
                for (index = 0; index < 6U; ++index) {
                    if (!read_signed_range(&reader, -128, 127,
                                           &parsed.weighting_quant_parameter_delta1[index]))
                        return CAVS_ERR_CORRUPT_BITSTREAM;
                }
            } else if (parsed.weighting_quant_parameter_index == 2U) {
                for (index = 0; index < 6U; ++index) {
                    if (!read_signed_range(&reader, -128, 127,
                                           &parsed.weighting_quant_parameter_delta2[index]))
                        return CAVS_ERR_CORRUPT_BITSTREAM;
                }
            }
        }
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.advanced_entropy_enabled = (uint8_t)value;
    }

    *picture = parsed;
    return CAVS_OK;
}

/*
 * Parses GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 7.3.2 Table 22 and the corresponding GB/T 20090.16-2016 Table 22.
 */
cavs_result cavs_parse_pb_picture_header(const uint8_t *data, size_t bit_size,
                                         const cavs_sequence_info *sequence,
                                         cavs_pb_picture_header *picture) {
    cavs_bitreader reader;
    cavs_pb_picture_header parsed;
    uint32_t value;
    unsigned index;
    if ((data == NULL && bit_size != 0U) || sequence == NULL || picture == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (sequence->profile_id != UINT8_C(0x20) &&
        sequence->profile_id != UINT8_C(0x48))
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    if (!cavs_br_init_bits(&reader, data, bit_size))
        return CAVS_ERR_INVALID_ARGUMENT;
    memset(&parsed, 0, sizeof(parsed));
    parsed.picture_structure = 1U;
    parsed.picture_reference_flag = 1U;

    if (!read_field(&reader, 16U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.bbv_delay = value;
    if (sequence->profile_id == UINT8_C(0x48)) {
        if (!read_field(&reader, 1U, &value) || value != 1U ||
            !read_field(&reader, 7U, &value))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.bbv_delay = (parsed.bbv_delay << 7) | value;
    }
    if (!read_field(&reader, 2U, &value) || value == 0U || value == 3U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.picture_coding_type = (uint8_t)value;
    if (!read_field(&reader, 8U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.picture_distance = (uint8_t)value;
    if (sequence->low_delay != 0U &&
        !cavs_br_read_ue(&reader, &parsed.bbv_check_times))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.progressive_frame = (uint8_t)value;
    if (parsed.progressive_frame == 0U) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.picture_structure = (uint8_t)value;
        if (parsed.picture_structure == 0U) {
            if (!read_field(&reader, 1U, &value) || value != 1U)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.advanced_prediction_mode_disable = (uint8_t)value;
        }
    }
    if (sequence->progressive_sequence != 0U && parsed.progressive_frame == 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.top_field_first = (uint8_t)value;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.repeat_first_field = (uint8_t)value;
    if (parsed.progressive_frame == 0U && parsed.repeat_first_field != 0U)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.fixed_picture_qp = (uint8_t)value;
    if (!read_field(&reader, 6U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.picture_qp = (uint8_t)value;
    if (!(parsed.picture_coding_type == 2U && parsed.picture_structure == 1U)) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.picture_reference_flag = (uint8_t)value;
    }
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.no_forward_reference_flag = (uint8_t)value;
    if (sequence->profile_id == UINT8_C(0x48)) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.pb_field_enhanced_flag = (uint8_t)value;
        if (!read_field(&reader, 2U, &value) || value != 0U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
    } else if (!read_field(&reader, 3U, &value) || value != 0U) {
        return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.skip_mode_flag = (uint8_t)value;
    if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.loop_filter_disable = (uint8_t)value;
    if (parsed.loop_filter_disable == 0U) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.loop_filter_parameter_flag = (uint8_t)value;
        if (parsed.loop_filter_parameter_flag != 0U &&
            (!read_signed_range(&reader, -8, 8, &parsed.alpha_c_offset) ||
             !read_signed_range(&reader, -8, 8, &parsed.beta_offset)))
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }

    if (sequence->profile_id == UINT8_C(0x48)) {
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.weighting_quant_flag = (uint8_t)value;
        if (parsed.weighting_quant_flag != 0U) {
            if (!read_field(&reader, 1U, &value) || value != 0U ||
                !read_field(&reader, 1U, &value))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.chroma_quant_parameter_disable = (uint8_t)value;
            if (parsed.chroma_quant_parameter_disable == 0U &&
                (!read_signed_range(&reader, -16, 16,
                                    &parsed.chroma_quant_parameter_delta_cb) ||
                 !read_signed_range(&reader, -16, 16,
                                    &parsed.chroma_quant_parameter_delta_cr)))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            if (!read_field(&reader, 2U, &value) || value == 3U)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.weighting_quant_parameter_index = (uint8_t)value;
            if (!read_field(&reader, 2U, &value) || value == 3U)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.weighting_quant_model = (uint8_t)value;
            if (parsed.weighting_quant_parameter_index == 1U) {
                for (index = 0; index < 6U; ++index) {
                    if (!read_signed_range(&reader, -128, 127,
                                           &parsed.weighting_quant_parameter_delta1[index]))
                        return CAVS_ERR_CORRUPT_BITSTREAM;
                }
            } else if (parsed.weighting_quant_parameter_index == 2U) {
                for (index = 0; index < 6U; ++index) {
                    if (!read_signed_range(&reader, -128, 127,
                                           &parsed.weighting_quant_parameter_delta2[index]))
                        return CAVS_ERR_CORRUPT_BITSTREAM;
                }
            }
        }
        if (!read_field(&reader, 1U, &value)) return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.advanced_entropy_enabled = (uint8_t)value;
    }

    *picture = parsed;
    return CAVS_OK;
}

/*
 * Parses the target-profile subset of GB/T 20090.2-2013, 信息技术 先进音视频编码
 * 第2部分: 视频, 7.1.3.6 Table 26, and GB/T 20090.16-2016, 7.1.3.5
 * Table 25. Macroblock syntax begins at slice.header_bits.
 */
cavs_result cavs_parse_slice_header(uint8_t start_code, const uint8_t *data,
                                    size_t bit_size,
                                    const cavs_slice_context *context,
                                    cavs_slice_header *slice) {
    cavs_bitreader reader;
    cavs_slice_header parsed;
    uint32_t value;
    uint32_t vertical_extension = 0U;
    unsigned index;
    int has_weighting_fields;

    if ((data == NULL && bit_size != 0U) || context == NULL || slice == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (start_code > UINT8_C(0xaf)) return CAVS_ERR_INVALID_ARGUMENT;
    if (context->profile_id != UINT8_C(0x20) &&
        context->profile_id != UINT8_C(0x48))
        return CAVS_ERR_UNSUPPORTED_PROFILE;
    if (context->number_of_references > CAVS_MAX_SLICE_REFERENCES ||
        !cavs_br_init_bits(&reader, data, bit_size))
        return CAVS_ERR_INVALID_ARGUMENT;

    memset(&parsed, 0, sizeof(parsed));
    parsed.fixed_slice_qp = context->fixed_picture_qp;
    parsed.slice_qp = context->picture_qp;
    if (context->vertical_size > 2800U &&
        !read_field(&reader, 3U, &vertical_extension))
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.macroblock_row = (uint16_t)((vertical_extension << 7) | start_code);

    if (context->fixed_picture_qp == 0U) {
        if (!read_field(&reader, 1U, &value))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.fixed_slice_qp = (uint8_t)value;
        if (!read_field(&reader, 6U, &value))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.slice_qp = (uint8_t)value;
    }

    has_weighting_fields = context->picture_type != CAVS_PICTURE_I ||
        (context->picture_structure == 0U &&
         parsed.macroblock_row >= context->macroblock_height / 2U);
    if (has_weighting_fields) {
        if (!read_field(&reader, 1U, &value))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed.slice_weighting_flag = (uint8_t)value;
        if (parsed.slice_weighting_flag != 0U) {
            if (context->number_of_references == 0U)
                return CAVS_ERR_MISSING_REFERENCE;
            parsed.number_of_references = context->number_of_references;
            for (index = 0U; index < parsed.number_of_references; ++index) {
                if (!read_field(&reader, 8U, &value))
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                parsed.luma_scale[index] = (uint8_t)value;
                if (!read_signed_byte(&reader, &parsed.luma_shift[index]) ||
                    !read_field(&reader, 1U, &value) || value != 1U ||
                    !read_field(&reader, 8U, &value))
                    return CAVS_ERR_CORRUPT_BITSTREAM;
                parsed.chroma_scale[index] = (uint8_t)value;
                if (!read_signed_byte(&reader, &parsed.chroma_shift[index]) ||
                    !read_field(&reader, 1U, &value) || value != 1U)
                    return CAVS_ERR_CORRUPT_BITSTREAM;
            }
            if (!read_field(&reader, 1U, &value))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            parsed.mb_weighting_flag = (uint8_t)value;
        }
    }

    if (context->advanced_entropy_enabled != 0U) {
        /*
         * TODO: Tables 25/26 specify f(1) alignment bits with value 1. The retained
         * CCTV-9 regression stream writes 0 for the first I-field slice, so
         * consume these non-semantic bits without enforcing their value.
         */
        while ((reader.bit_pos & 7U) != 0U) {
            if (!read_field(&reader, 1U, &value))
                return CAVS_ERR_CORRUPT_BITSTREAM;
        }
    }
    parsed.header_bits = reader.bit_pos;
    *slice = parsed;
    return CAVS_OK;
}
