/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * normative Annex A, 伪起始码. GB/T 20090.16-2016 defines the same process
 * in its normative Annex A.
 */
#include "codec/pseudo_start_code.h"
#include "safe.h"
#include <string.h>

/* Packs a selected number of high-order source bits into the output stream. */
static void append_bits(uint8_t *destination, size_t *position,
                        uint8_t value, unsigned count) {
    unsigned index;
    for (index = 0; index < count; ++index) {
        unsigned bit = ((unsigned)value >> (7U - index)) & 1U;
        size_t output_position = (*position)++;
        destination[output_position / 8U] |=
            (uint8_t)(bit << (7U - (output_position % 8U)));
    }
}

/* Removes prevention bits and reports the exact number of valid output bits. */
int cavs_remove_pseudo_start_codes(const uint8_t *source, size_t source_size,
                                   uint8_t *destination, size_t destination_size,
                                   size_t *output_bits) {
    size_t index;
    size_t bit_count;
    size_t output_size;
    size_t position = 0;
    size_t escape_count = 0;
    if (output_bits == NULL || (source == NULL && source_size != 0U)) return 0;
    for (index = 2; index < source_size; ++index) {
        if (source[index - 2] == 0U && source[index - 1] == 0U && source[index] == 2U)
            ++escape_count;
    }
    if (!cavs_size_mul(source_size, 8U, &bit_count) || escape_count > bit_count / 2U)
        return 0;
    bit_count -= escape_count * 2U;
    output_size = bit_count / 8U + (bit_count % 8U != 0U ? 1U : 0U);
    if (output_size > destination_size || (destination == NULL && output_size != 0U)) return 0;
    if (output_size != 0U) memset(destination, 0, output_size);
    for (index = 0; index < source_size; ++index) {
        unsigned count = 8U;
        if (index >= 2U && source[index - 2] == 0U && source[index - 1] == 0U && source[index] == 2U)
            count = 6U;
        append_bits(destination, &position, source[index], count);
    }
    *output_bits = bit_count;
    return 1;
}
