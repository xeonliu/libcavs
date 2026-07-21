/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bit syntax primitives for GB/T 20090.2-2013, 信息技术 先进音视频编码
 * 第2部分: 视频, 8.2, Tables 42 and 43.
 */
#include "bitreader.h"
#include <limits.h>

/* Initializes a reader over complete bytes, rejecting a size overflow. */
void cavs_br_init(cavs_bitreader *br, const uint8_t *data, size_t size) {
    if (br == NULL) return;
    br->data = data;
    br->bit_size = size <= SIZE_MAX / 8U ? size * 8U : 0U;
    br->bit_pos = 0;
}

/* Initializes a reader when the final storage byte is only partly valid. */
int cavs_br_init_bits(cavs_bitreader *br, const uint8_t *data, size_t bit_size) {
    if (br == NULL || (data == NULL && bit_size != 0U)) return 0;
    br->data = data;
    br->bit_size = bit_size;
    br->bit_pos = 0;
    return 1;
}

/* Reports remaining input without allowing an underflow on invalid state. */
size_t cavs_br_bits_left(const cavs_bitreader *br) {
    if (br == NULL || br->bit_pos > br->bit_size) return 0;
    return br->bit_size - br->bit_pos;
}

/* Reads a fixed-width value and leaves the reader unchanged on failure. */
int cavs_br_read(cavs_bitreader *br, unsigned count, uint32_t *value) {
    uint32_t out = 0;
    unsigned i;
    if (br == NULL || value == NULL || count > 32U ||
        (br->data == NULL && count != 0U) ||
        (size_t)count > cavs_br_bits_left(br)) return 0;
    for (i = 0; i < count; ++i) {
        size_t pos = br->bit_pos++;
        out = (out << 1) |
              (((uint32_t)br->data[pos / 8U] >> (7U - (pos % 8U))) & UINT32_C(1));
    }
    *value = out;
    return 1;
}

/* Parses the order-k code construction specified by Table 42. */
int cavs_br_read_ue_k(cavs_bitreader *br, unsigned order, uint32_t *value) {
    unsigned zeros = 0;
    uint32_t bit, suffix;
    uint64_t code_num;
    size_t initial_position;
    unsigned suffix_bits;
    if (br == NULL || value == NULL || order > 31U) return 0;
    initial_position = br->bit_pos;
    while (zeros <= 31U) {
        if (!cavs_br_read(br, 1, &bit)) {
            br->bit_pos = initial_position;
            return 0;
        }
        if (bit != 0U) break;
        ++zeros;
    }
    if (zeros > 31U || zeros > 32U - order) {
        br->bit_pos = initial_position;
        return 0;
    }
    suffix_bits = zeros + order;
    if (!cavs_br_read(br, suffix_bits, &suffix)) {
        br->bit_pos = initial_position;
        return 0;
    }
    code_num = (UINT64_C(1) << suffix_bits) - (UINT64_C(1) << order) + suffix;
    if (code_num > UINT32_MAX) {
        br->bit_pos = initial_position;
        return 0;
    }
    *value = (uint32_t)code_num;
    return 1;
}

/* Parses the ue(v) shorthand, which is an order-0 code. */
int cavs_br_read_ue(cavs_bitreader *br, uint32_t *value) {
    return cavs_br_read_ue_k(br, 0U, value);
}

/* Maps CodeNum to se(v) as independently transcribed from Table 43. */
int cavs_br_read_se(cavs_bitreader *br, int32_t *value) {
    uint32_t code_num;
    int64_t magnitude;
    size_t initial_position;
    if (br == NULL || value == NULL) return 0;
    initial_position = br->bit_pos;
    if (!cavs_br_read_ue(br, &code_num)) return 0;
    magnitude = ((int64_t)code_num + 1) / 2;
    if (magnitude > INT32_MAX) {
        br->bit_pos = initial_position;
        return 0;
    }
    *value = (int32_t)((code_num & 1U) != 0U ? magnitude : -magnitude);
    return 1;
}
