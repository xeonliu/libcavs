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
