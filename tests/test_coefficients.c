/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013 Annex D basic-entropy coefficient tests.
 */
#include "coefficients.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct bit_writer {
    uint8_t data[128];
    size_t bit_pos;
} bit_writer;

typedef struct table_vector {
    cavs_basic_block_kind kind;
    uint8_t table_index;
    uint8_t entry_index;
    uint8_t code;
    uint8_t run;
    uint8_t level;
} table_vector;

static void put_bit(bit_writer *writer, uint32_t bit) {
    size_t byte = writer->bit_pos / 8U;
    unsigned shift = 7U - (unsigned)(writer->bit_pos % 8U);
    assert(byte < sizeof(writer->data));
    if (bit != 0U) writer->data[byte] |= (uint8_t)(1U << shift);
    ++writer->bit_pos;
}

static void put_bits(bit_writer *writer, uint32_t value, unsigned count) {
    while (count != 0U) {
        --count;
        put_bit(writer, (value >> count) & 1U);
    }
}

static void put_ue_k(bit_writer *writer, uint32_t code_num, unsigned order) {
    unsigned zeros = 0U;
    unsigned suffix_bits;
    uint64_t base;
    uint64_t span;
    for (;;) {
        suffix_bits = zeros + order;
        base = (UINT64_C(1) << suffix_bits) - (UINT64_C(1) << order);
        span = UINT64_C(1) << suffix_bits;
        if ((uint64_t)code_num < base + span) break;
        ++zeros;
    }
    while (zeros-- != 0U) put_bit(writer, 0U);
    put_bit(writer, 1U);
    put_bits(writer, (uint32_t)((uint64_t)code_num - base), suffix_bits);
}

static unsigned table_count(cavs_basic_block_kind kind) {
    return kind == CAVS_BASIC_CHROMA ? 5U : 7U;
}

static unsigned table_order(cavs_basic_block_kind kind, unsigned index) {
    static const uint8_t inter[] = { 3, 2, 2, 2, 2, 2, 2 };
    static const uint8_t chroma[] = { 2, 0, 1, 1, 0 };
    if (kind == CAVS_BASIC_INTER_LUMA) return inter[index];
    if (kind == CAVS_BASIC_CHROMA) return chroma[index];
    return 2U;
}

static uint32_t table_eob(cavs_basic_block_kind kind, unsigned index) {
    static const uint8_t intra[] = { 0, 8, 8, 8, 6, 0, 0 };
    static const uint8_t inter[] = { 0, 2, 2, 2, 2, 0, 0 };
    static const uint8_t chroma[] = { 0, 0, 2, 0, 0 };
    if (kind == CAVS_BASIC_INTRA_LUMA) return intra[index];
    if (kind == CAVS_BASIC_INTER_LUMA) return inter[index];
    return chroma[index];
}

static unsigned selected_table(cavs_basic_block_kind kind, uint32_t level) {
    if (kind == CAVS_BASIC_INTRA_LUMA) {
        if (level <= 2U) return (unsigned)level;
        if (level <= 4U) return 3U;
        if (level <= 7U) return 4U;
        if (level <= 10U) return 5U;
        return 6U;
    }
    if (kind == CAVS_BASIC_INTER_LUMA) {
        if (level <= 3U) return (unsigned)level;
        if (level <= 6U) return 4U;
        if (level <= 9U) return 5U;
        return 6U;
    }
    if (level <= 2U) return (unsigned)level;
    if (level <= 4U) return 3U;
    return 4U;
}

static uint32_t seed_level(cavs_basic_block_kind kind, unsigned index) {
    static const uint8_t intra[] = { 0, 1, 2, 3, 5, 8, 11 };
    static const uint8_t inter[] = { 0, 1, 2, 3, 4, 7, 10 };
    static const uint8_t chroma[] = { 0, 1, 2, 3, 5 };
    if (kind == CAVS_BASIC_INTRA_LUMA) return intra[index];
    if (kind == CAVS_BASIC_INTER_LUMA) return inter[index];
    return chroma[index];
}

static void put_seed(bit_writer *writer, cavs_basic_block_kind kind,
                     unsigned table_index) {
    static const uint8_t intra_code[] = { 0, 0, 22, 38 };
    static const uint8_t inter_code[] = { 0, 0, 26, 40 };
    static const uint8_t chroma_code[] = { 0, 0, 14, 32 };
    uint32_t level = seed_level(kind, table_index);
    uint32_t reference = kind == CAVS_BASIC_CHROMA ? 5U : 4U;
    const uint8_t *codes = kind == CAVS_BASIC_INTRA_LUMA ? intra_code :
                           kind == CAVS_BASIC_INTER_LUMA ? inter_code :
                           chroma_code;
    if (table_index <= 3U) {
        put_ue_k(writer, codes[table_index], table_order(kind, 0U));
    } else {
        put_ue_k(writer, 60U, table_order(kind, 0U));
        put_ue_k(writer, level - reference,
                 kind == CAVS_BASIC_INTRA_LUMA ? 1U : 0U);
    }
}

static void test_normative_table_samples(void) {
    static const table_vector vectors[] = {
        { CAVS_BASIC_INTRA_LUMA, 0, 0, 0, 0, 1 },
        { CAVS_BASIC_INTRA_LUMA, 1, 28, 57, 14, 1 },
        { CAVS_BASIC_INTRA_LUMA, 2, 17, 15, 3, 1 },
        { CAVS_BASIC_INTRA_LUMA, 3, 21, 57, 2, 4 },
        { CAVS_BASIC_INTRA_LUMA, 4, 27, 39, 3, 1 },
        { CAVS_BASIC_INTRA_LUMA, 5, 28, 57, 2, 2 },
        { CAVS_BASIC_INTRA_LUMA, 6, 28, 55, 1, 3 },
        { CAVS_BASIC_INTER_LUMA, 0, 28, 56, 25, 1 },
        { CAVS_BASIC_INTER_LUMA, 1, 6, 57, 1, 3 },
        { CAVS_BASIC_INTER_LUMA, 2, 28, 57, 13, 1 },
        { CAVS_BASIC_INTER_LUMA, 3, 17, 57, 2, 4 },
        { CAVS_BASIC_INTER_LUMA, 4, 28, 47, 6, 1 },
        { CAVS_BASIC_INTER_LUMA, 5, 27, 55, 3, 2 },
        { CAVS_BASIC_INTER_LUMA, 6, 28, 53, 3, 1 },
        { CAVS_BASIC_CHROMA, 0, 28, 54, 24, 1 },
        { CAVS_BASIC_CHROMA, 1, 28, 57, 19, 1 },
        { CAVS_BASIC_CHROMA, 2, 19, 57, 3, 3 },
        { CAVS_BASIC_CHROMA, 3, 28, 57, 7, 1 },
        { CAVS_BASIC_CHROMA, 4, 28, 57, 4, 1 }
    };
    size_t index;
    for (index = 0U; index < sizeof(vectors) / sizeof(vectors[0]); ++index) {
        uint32_t code, run, level;
        unsigned count;
        assert(cavs_test_basic_vlc_info(vectors[index].kind,
                    vectors[index].table_index, vectors[index].entry_index,
                    &code, &run, &level, &count));
        assert(code == vectors[index].code);
        assert(run == vectors[index].run);
        assert(level == vectors[index].level);
        assert(count > vectors[index].entry_index);
    }
}

static void decode_table_entry(cavs_basic_block_kind kind,
                               unsigned table_index, unsigned entry_index,
                               int negative) {
    bit_writer writer;
    cavs_basic_coefficients parsed;
    uint32_t code, run, level;
    uint32_t maximum;
    unsigned count;
    unsigned final_table;
    unsigned parsed_index = table_index == 0U ? 0U : 1U;
    memset(&writer, 0, sizeof(writer));
    put_bits(&writer, 5U, 3U);
    assert(cavs_test_basic_vlc_info(kind, table_index, entry_index,
                                   &code, &run, &level, &count));
    if (table_index != 0U) put_seed(&writer, kind, table_index);
    put_ue_k(&writer, code + (negative != 0), table_order(kind, table_index));
    maximum = seed_level(kind, table_index);
    if (level > maximum) maximum = level;
    final_table = selected_table(kind, maximum);
    put_ue_k(&writer, table_eob(kind, final_table),
             table_order(kind, final_table));
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 3U,
                                              kind, &parsed) == CAVS_OK);
    assert(parsed.count == parsed_index + 1U);
    assert(parsed.run[parsed_index] == run);
    assert(parsed.level[parsed_index] ==
           (negative != 0 ? -(int32_t)level : (int32_t)level));
    assert(parsed.end_bit_offset == writer.bit_pos);
}

static void test_every_vlc_entry_and_eob(void) {
    cavs_basic_block_kind kind;
    for (kind = CAVS_BASIC_INTRA_LUMA; kind <= CAVS_BASIC_CHROMA;
         kind = (cavs_basic_block_kind)(kind + 1)) {
        unsigned table_index;
        for (table_index = 0U; table_index < table_count(kind); ++table_index) {
            uint32_t code, run, level;
            unsigned entry_index;
            unsigned count;
            assert(cavs_test_basic_vlc_info(kind, table_index, 0U,
                                           &code, &run, &level, &count));
            for (entry_index = 0U; entry_index < count; ++entry_index) {
                decode_table_entry(kind, table_index, entry_index, 0);
                decode_table_entry(kind, table_index, entry_index, 1);
            }
            if (table_index != 0U) {
                bit_writer writer;
                cavs_basic_coefficients parsed;
                memset(&writer, 0, sizeof(writer));
                put_seed(&writer, kind, table_index);
                put_ue_k(&writer, table_eob(kind, table_index),
                         table_order(kind, table_index));
                assert(cavs_decode_basic_coefficients_8x8(
                           writer.data, writer.bit_pos, 0U, kind, &parsed) == CAVS_OK);
                assert(parsed.count == 1U);
            }
        }
    }
}

static void test_escape_and_reverse_run(void) {
    bit_writer writer;
    cavs_basic_coefficients parsed;
    memset(&writer, 0, sizeof(writer));
    put_ue_k(&writer, 0U, 2U);  /* (run 0, level 1), then VLC1_Intra. */
    put_ue_k(&writer, 17U, 2U); /* (run 1, level 2), then VLC2_Intra. */
    put_ue_k(&writer, 8U, 2U);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_OK);
    assert(parsed.count == 2U && parsed.scan_coefficients[1] == 2);
    assert(parsed.scan_coefficients[2] == 1);

    memset(&writer, 0, sizeof(writer));
    put_ue_k(&writer, 59U, 2U);
    put_ue_k(&writer, 2U, 1U);
    put_ue_k(&writer, 6U, 2U);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_OK);
    assert(parsed.run[0] == 0U && parsed.level[0] == -6);

    memset(&writer, 0, sizeof(writer));
    put_ue_k(&writer, 60U, 2U);
    put_ue_k(&writer, 0U, 0U);
    put_ue_k(&writer, 0U, 0U);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_CHROMA, &parsed) == CAVS_OK);
    assert(parsed.level[0] == 5);
}

static void test_boundaries_and_errors(void) {
    bit_writer writer;
    cavs_basic_coefficients parsed;
    cavs_basic_coefficients unchanged;
    memset(&writer, 0, sizeof(writer));
    put_ue_k(&writer, 186U, 2U); /* Positive escape with run 63. */
    put_ue_k(&writer, 0U, 1U);
    put_ue_k(&writer, 8U, 2U);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_OK);
    assert(parsed.count == 1U && parsed.scan_coefficients[63] == 1);

    put_ue_k(&writer, 0U, 2U);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_OK);

    memset(&writer, 0, sizeof(writer));
    put_ue_k(&writer, 186U, 2U);
    put_ue_k(&writer, 0U, 1U);
    put_ue_k(&writer, 0U, 2U);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_ERR_CORRUPT_BITSTREAM);

    memset(&writer, 0, sizeof(writer));
    put_ue_k(&writer, 60U, 2U);
    put_ue_k(&writer, (uint32_t)INT32_MAX, 1U);
    memset(&unchanged, 0xa5, sizeof(unchanged));
    parsed = unchanged;
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(memcmp(&parsed, &unchanged, sizeof(parsed)) == 0);

    memset(&writer, 0, sizeof(writer));
    put_ue_k(&writer, 255U, 2U);
    put_ue_k(&writer, 0U, 1U);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos - 1U, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_ERR_CORRUPT_BITSTREAM);
    assert(cavs_decode_basic_coefficients_8x8(NULL, 1U, 0U,
               CAVS_BASIC_INTRA_LUMA, &parsed) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos,
               writer.bit_pos + 1U, CAVS_BASIC_INTRA_LUMA,
               &parsed) == CAVS_ERR_INVALID_ARGUMENT);
    assert(cavs_decode_basic_coefficients_8x8(writer.data, writer.bit_pos, 0U,
               (cavs_basic_block_kind)3, &parsed) == CAVS_ERR_INVALID_ARGUMENT);
}

void test_coefficients(void) {
    test_normative_table_samples();
    test_every_vlc_entry_and_eob();
    test_escape_and_reverse_run();
    test_boundaries_and_errors();
}
