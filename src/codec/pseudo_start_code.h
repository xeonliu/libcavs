/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Normative pseudo-start-code removal for AVS video payloads.
 */
#ifndef CAVS_PSEUDO_START_CODE_H
#define CAVS_PSEUDO_START_CODE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Removes the two prevention bits from each raw 00 00 02 sequence.
 * The caller decides whether the unit type is exempt under normative Annex A.
 */
int cavs_remove_pseudo_start_codes(const uint8_t *source, size_t source_size,
                                   uint8_t *destination, size_t destination_size,
                                   size_t *output_bits);

#endif
