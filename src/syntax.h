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

/* Classifies one eight-bit start-code value. */
cavs_unit_type cavs_classify_start_code(uint8_t start_code);

/* Parses and validates the common baseline/broadcast sequence header. */
cavs_result cavs_parse_sequence_header(const uint8_t *data, size_t size,
                                       cavs_sequence_info *sequence);

#endif
