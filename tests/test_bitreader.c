/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Unit tests for safety primitives and standard-defined bit transformations.
 */
#include "bitreader.h"
#include "pseudo_start_code.h"
#include "safe.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>

/* Runs syntax tests implemented in the companion translation unit. */
void test_syntax(void);
void test_macroblock(void);
void test_coefficients(void);
void test_reconstruction(void);
void test_prediction(void);

/* Exercises fixed-width reads and exact final-byte bit limits. */
static void test_fixed_bits(void) {
    static const uint8_t data[] = { 0xb3, 0x80 };
    cavs_bitreader br;
    uint32_t value;
    cavs_br_init(&br, data, sizeof(data));
    assert(cavs_br_read(&br, 4, &value) && value == 11U);
    assert(cavs_br_read(&br, 4, &value) && value == 3U);
    assert(cavs_br_init_bits(&br, data + 1, 1U));
    assert(cavs_br_read(&br, 1, &value) && value == 1U);
    assert(!cavs_br_read(&br, 1, &value));
}

/* Checks independently transcribed examples from Tables 42 and 43. */
static void test_exponential_golomb(void) {
    static const uint8_t order_zero[] = { 0xa6, 0x40 }; /* 1, 010, 011, 00100 */
    static const uint8_t order_one[] = { 0xc0 };       /* 11 -> CodeNum 1 */
    cavs_bitreader br;
    uint32_t value;
    int32_t signed_value;
    cavs_br_init(&br, order_zero, sizeof(order_zero));
    assert(cavs_br_read_ue(&br, &value) && value == 0U);
    assert(cavs_br_read_ue(&br, &value) && value == 1U);
    assert(cavs_br_read_ue(&br, &value) && value == 2U);
    assert(cavs_br_read_ue(&br, &value) && value == 3U);
    cavs_br_init(&br, order_one, sizeof(order_one));
    assert(cavs_br_read_ue_k(&br, 1U, &value) && value == 1U);
    cavs_br_init(&br, order_zero, sizeof(order_zero));
    assert(cavs_br_read_se(&br, &signed_value) && signed_value == 0);
    assert(cavs_br_read_se(&br, &signed_value) && signed_value == 1);
    assert(cavs_br_read_se(&br, &signed_value) && signed_value == -1);
    cavs_br_init_bits(&br, order_zero, 2U);
    assert(cavs_br_read(&br, 1U, &value) && value == 1U);
    assert(!cavs_br_read_ue(&br, &value));
    assert(br.bit_pos == 1U);
}

/* Verifies Annex A removal, repacking, and output-capacity rejection. */
static void test_pseudo_start_code(void) {
    static const uint8_t encoded[] = { 0x00, 0x00, 0x02, 0xc0 };
    uint8_t decoded[4];
    size_t output_bits;
    assert(cavs_remove_pseudo_start_codes(encoded, sizeof(encoded), decoded,
                                          sizeof(decoded), &output_bits));
    assert(output_bits == 30U);
    assert(decoded[0] == 0U && decoded[1] == 0U && decoded[2] == 3U && decoded[3] == 0U);
    assert(!cavs_remove_pseudo_start_codes(encoded, sizeof(encoded), decoded, 3U, &output_bits));
}

/* Covers arithmetic overflow and alignment-independent byte loads. */
static void test_safe_helpers(void) {
    static const uint8_t bytes[] = { 0xff, 0x12, 0x34, 0x56, 0x78 };
    size_t value;
    assert(cavs_size_add(2U, 3U, &value) && value == 5U);
    assert(!cavs_size_add(SIZE_MAX, 1U, &value));
    assert(cavs_size_mul(7U, 9U, &value) && value == 63U);
    assert(!cavs_size_mul(SIZE_MAX, 2U, &value));
    assert(cavs_load_be16(bytes + 1) == UINT16_C(0x1234));
    assert(cavs_load_be32(bytes + 1) == UINT32_C(0x12345678));
}

/* Runs all safety-foundation test groups. */
int main(void) {
    test_fixed_bits();
    test_exponential_golomb();
    test_pseudo_start_code();
    test_safe_helpers();
    test_syntax();
    test_macroblock();
    test_coefficients();
    test_reconstruction();
    test_prediction();
    return 0;
}
