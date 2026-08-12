/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.2-2013, 信息技术 先进音视频编码 第2部分: 视频,
 * 8.3.1, 9.5.1.2, 9.5.3, 9.7.1-9.7.2, Figures 33-34, Table 70,
 * and normative Annex D Tables D.1-D.20.
 */
#include "codec/coefficients.h"
#include "bitreader.h"
#include <limits.h>
#include <string.h>


/* GB/T 20090.2-2013 9.5.3 Figures 33-34 inverse-scan mappings. */
static const uint8_t frame_scan[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

static const uint8_t field_scan[64] = {
     0,  3, 11, 16, 22, 32, 38, 55,
     1,  6, 12, 20, 25, 33, 42, 57,
     2,  7, 15, 21, 28, 37, 43, 58,
     4, 10, 19, 27, 31, 39, 47, 59,
     5, 14, 24, 30, 36, 44, 50, 60,
     8, 17, 26, 35, 41, 48, 52, 61,
     9, 18, 29, 40, 46, 51, 54, 62,
    13, 23, 34, 45, 49, 53, 56, 63
};

/* GB/T 20090.2-2013 Table 70 chroma QP mapping. */
static const uint8_t chroma_qp_table[64] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 42, 43, 43, 44, 44,
    45, 45, 46, 46, 47, 47, 48, 48, 48, 49, 49, 49, 50, 50, 50, 51
};

static const uint16_t dequant_table[64] = {
    32768, 36061, 38968, 42495, 46341, 50535, 55437, 60424,
    32932, 35734, 38968, 42495, 46177, 50535, 55109, 59933,
    65535, 35734, 38968, 42577, 46341, 50617, 55027, 60097,
    32809, 35734, 38968, 42454, 46382, 50576, 55109, 60056,
    65535, 35734, 38968, 42495, 46320, 50515, 55109, 60076,
    65535, 35744, 38968, 42495, 46341, 50535, 55099, 60087,
    65535, 35734, 38973, 42500, 46341, 50535, 55109, 60097,
    32771, 35734, 38965, 42497, 46341, 50535, 55109, 60099
};

static const uint8_t dequant_shift[64] = {
    14,14,14,14,14,14,14,14, 13,13,13,13,13,13,13,13,
    13,12,12,12,12,12,12,12, 11,11,11,11,11,11,11,11,
    11,10,10,10,10,10,10,10, 10, 9, 9, 9, 9, 9, 9, 9,
     9, 8, 8, 8, 8, 8, 8, 8,  7, 7, 7, 7, 7, 7, 7, 7
};

static int64_t floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

typedef struct cavs_vlc_entry {
    uint8_t code;
    uint8_t run;
    uint8_t level;
} cavs_vlc_entry;

typedef struct cavs_vlc_table {
    const cavs_vlc_entry *entries;
    uint8_t entry_count;
    uint8_t eob;
    uint8_t has_eob;
    uint8_t max_run;
    uint8_t golomb_order;
} cavs_vlc_table;

#define E(code_value, run_value, level_value) \
    { (code_value), (run_value), (level_value) }
#define ENTRY_COUNT(entries_value) \
    ((uint8_t)(sizeof(entries_value) / sizeof((entries_value)[0])))

/* Tables D.1-D.7: intra-coded luma. Only positive-level entries are stored. */
static const cavs_vlc_entry intra0[] = {
    E(0,0,1), E(22,0,2), E(38,0,3), E(2,1,1), E(32,1,2),
    E(4,2,1), E(44,2,2), E(6,3,1), E(50,3,2), E(8,4,1), E(54,4,2),
    E(10,5,1), E(12,6,1), E(14,7,1), E(16,8,1), E(18,9,1), E(20,10,1),
    E(24,11,1), E(26,12,1), E(28,13,1), E(30,14,1), E(34,15,1),
    E(36,16,1), E(40,17,1), E(42,18,1), E(46,19,1), E(48,20,1),
    E(52,21,1), E(56,22,1)
};
static const cavs_vlc_entry intra1[] = {
    E(0,0,1), E(4,0,2), E(15,0,3), E(27,0,4), E(41,0,5), E(55,0,6),
    E(2,1,1), E(17,1,2), E(35,1,3), E(6,2,1), E(25,2,2), E(53,2,3),
    E(9,3,1), E(33,3,2), E(11,4,1), E(39,4,2), E(13,5,1), E(45,5,2),
    E(19,6,1), E(49,6,2), E(21,7,1), E(51,7,2), E(23,8,1), E(29,9,1),
    E(31,10,1), E(37,11,1), E(43,12,1), E(47,13,1), E(57,14,1)
};
static const cavs_vlc_entry intra2[] = {
    E(0,0,1), E(2,0,2), E(6,0,3), E(13,0,4), E(17,0,5), E(27,0,6),
    E(35,0,7), E(45,0,8), E(55,0,9), E(4,1,1), E(11,1,2), E(21,1,3),
    E(33,1,4), E(49,1,5), E(9,2,1), E(23,2,2), E(37,2,3), E(15,3,1),
    E(29,3,2), E(51,3,3), E(19,4,1), E(39,4,2), E(25,5,1), E(43,5,2),
    E(31,6,1), E(53,6,2), E(41,7,1), E(47,8,1), E(57,9,1)
};
static const cavs_vlc_entry intra3[] = {
    E(0,0,1), E(2,0,2), E(4,0,3), E(9,0,4), E(11,0,5), E(17,0,6),
    E(21,0,7), E(25,0,8), E(33,0,9), E(39,0,10), E(45,0,11), E(55,0,12),
    E(6,1,1), E(13,1,2), E(19,1,3), E(29,1,4), E(35,1,5), E(47,1,6),
    E(15,2,1), E(27,2,2), E(41,2,3), E(57,2,4), E(23,3,1), E(37,3,2),
    E(53,3,3), E(31,4,1), E(51,4,2), E(43,5,1), E(49,6,1)
};
static const cavs_vlc_entry intra4[] = {
    E(0,0,1), E(2,0,2), E(4,0,3), E(7,0,4), E(9,0,5), E(11,0,6),
    E(15,0,7), E(17,0,8), E(21,0,9), E(23,0,10), E(29,0,11), E(33,0,12),
    E(35,0,13), E(43,0,14), E(47,0,15), E(49,0,16), E(57,0,17),
    E(13,1,1), E(19,1,2), E(27,1,3), E(31,1,4), E(37,1,5), E(45,1,6),
    E(55,1,7), E(25,2,1), E(41,2,2), E(51,2,3), E(39,3,1), E(53,4,1)
};
static const cavs_vlc_entry intra5[] = {
    E(1,0,1), E(3,0,2), E(5,0,3), E(7,0,4), E(9,0,5), E(11,0,6),
    E(13,0,7), E(15,0,8), E(17,0,9), E(19,0,10), E(23,0,11), E(25,0,12),
    E(27,0,13), E(31,0,14), E(33,0,15), E(37,0,16), E(41,0,17),
    E(45,0,18), E(49,0,19), E(51,0,20), E(55,0,21), E(21,1,1),
    E(29,1,2), E(35,1,3), E(43,1,4), E(47,1,5), E(53,1,6),
    E(39,2,1), E(57,2,2)
};
static const cavs_vlc_entry intra6[] = {
    E(1,0,1), E(3,0,2), E(5,0,3), E(7,0,4), E(9,0,5), E(11,0,6),
    E(13,0,7), E(15,0,8), E(17,0,9), E(19,0,10), E(21,0,11), E(23,0,12),
    E(25,0,13), E(27,0,14), E(29,0,15), E(31,0,16), E(35,0,17),
    E(37,0,18), E(39,0,19), E(41,0,20), E(43,0,21), E(47,0,22),
    E(49,0,23), E(51,0,24), E(53,0,25), E(57,0,26), E(33,1,1),
    E(45,1,2), E(55,1,3)
};

/* Tables D.8-D.14: inter-coded luma. */
static const cavs_vlc_entry inter0[] = {
    E(0,0,1), E(26,0,2), E(40,0,3), E(2,1,1), E(46,1,2), E(4,2,1),
    E(6,3,1), E(8,4,1), E(10,5,1), E(12,6,1), E(14,7,1), E(16,8,1),
    E(18,9,1), E(20,10,1), E(22,11,1), E(24,12,1), E(28,13,1),
    E(30,14,1), E(32,15,1), E(34,16,1), E(36,17,1), E(38,18,1),
    E(42,19,1), E(44,20,1), E(48,21,1), E(50,22,1), E(52,23,1),
    E(54,24,1), E(56,25,1)
};
static const cavs_vlc_entry inter1[] = {
    E(0,0,1), E(13,0,2), E(29,0,3), E(47,0,4), E(3,1,1), E(23,1,2),
    E(57,1,3), E(5,2,1), E(35,2,2), E(7,3,1), E(39,3,2), E(9,4,1),
    E(43,4,2), E(11,5,1), E(49,5,2), E(15,6,1), E(55,6,2), E(17,7,1),
    E(19,8,1), E(21,9,1), E(25,10,1), E(27,11,1), E(31,12,1),
    E(33,13,1), E(37,14,1), E(41,15,1), E(45,16,1), E(51,17,1), E(53,18,1)
};
static const cavs_vlc_entry inter2[] = {
    E(0,0,1), E(5,0,2), E(11,0,3), E(23,0,4), E(35,0,5), E(47,0,6),
    E(3,1,1), E(13,1,2), E(27,1,3), E(49,1,4), E(7,2,1), E(21,2,2),
    E(45,2,3), E(9,3,1), E(29,3,2), E(55,3,3), E(15,4,1), E(37,4,2),
    E(17,5,1), E(41,5,2), E(19,6,1), E(53,6,2), E(25,7,1), E(31,8,1),
    E(33,9,1), E(39,10,1), E(43,11,1), E(51,12,1), E(57,13,1)
};
static const cavs_vlc_entry inter3[] = {
    E(0,0,1), E(3,0,2), E(7,0,3), E(13,0,4), E(17,0,5), E(27,0,6),
    E(35,0,7), E(43,0,8), E(55,0,9), E(5,1,1), E(11,1,2), E(21,1,3),
    E(33,1,4), E(51,1,5), E(9,2,1), E(23,2,2), E(37,2,3), E(57,2,4),
    E(15,3,1), E(29,3,2), E(47,3,3), E(19,4,1), E(41,4,2), E(25,5,1),
    E(49,5,2), E(31,6,1), E(39,7,1), E(45,8,1), E(53,9,1)
};
static const cavs_vlc_entry inter4[] = {
    E(0,0,1), E(3,0,2), E(5,0,3), E(9,0,4), E(11,0,5), E(17,0,6),
    E(21,0,7), E(25,0,8), E(33,0,9), E(41,0,10), E(45,0,11), E(55,0,12),
    E(7,1,1), E(13,1,2), E(19,1,3), E(29,1,4), E(35,1,5), E(49,1,6),
    E(15,2,1), E(27,2,2), E(43,2,3), E(57,2,4), E(23,3,1), E(37,3,2),
    E(51,3,3), E(31,4,1), E(53,4,2), E(39,5,1), E(47,6,1)
};
static const cavs_vlc_entry inter5[] = {
    E(1,0,1), E(3,0,2), E(5,0,3), E(7,0,4), E(9,0,5), E(13,0,6),
    E(15,0,7), E(17,0,8), E(21,0,9), E(25,0,10), E(29,0,11), E(33,0,12),
    E(39,0,13), E(43,0,14), E(49,0,15), E(53,0,16), E(11,1,1), E(19,1,2),
    E(27,1,3), E(31,1,4), E(41,1,5), E(45,1,6), E(57,1,7), E(23,2,1),
    E(37,2,2), E(51,2,3), E(35,3,1), E(55,3,2), E(47,4,1)
};
static const cavs_vlc_entry inter6[] = {
    E(1,0,1), E(3,0,2), E(5,0,3), E(7,0,4), E(9,0,5), E(11,0,6),
    E(13,0,7), E(17,0,8), E(19,0,9), E(21,0,10), E(23,0,11), E(25,0,12),
    E(29,0,13), E(33,0,14), E(35,0,15), E(39,0,16), E(41,0,17),
    E(43,0,18), E(47,0,19), E(49,0,20), E(57,0,21), E(15,1,1),
    E(27,1,2), E(37,1,3), E(45,1,4), E(55,1,5), E(31,2,1), E(51,2,2),
    E(53,3,1)
};

/* Tables D.15-D.19: chroma. */
static const cavs_vlc_entry chroma0[] = {
    E(0,0,1), E(14,0,2), E(32,0,3), E(56,0,4), E(2,1,1), E(48,1,2),
    E(4,2,1), E(6,3,1), E(8,4,1), E(10,5,1), E(12,6,1), E(16,7,1),
    E(18,8,1), E(20,9,1), E(22,10,1), E(24,11,1), E(26,12,1),
    E(28,13,1), E(30,14,1), E(34,15,1), E(36,16,1), E(38,17,1),
    E(40,18,1), E(42,19,1), E(44,20,1), E(46,21,1), E(50,22,1),
    E(52,23,1), E(54,24,1)
};
static const cavs_vlc_entry chroma1[] = {
    E(1,0,1), E(5,0,2), E(15,0,3), E(29,0,4), E(43,0,5), E(3,1,1),
    E(21,1,2), E(45,1,3), E(7,2,1), E(37,2,2), E(9,3,1), E(41,3,2),
    E(11,4,1), E(53,4,2), E(13,5,1), E(17,6,1), E(19,7,1), E(23,8,1),
    E(25,9,1), E(27,10,1), E(31,11,1), E(33,12,1), E(35,13,1),
    E(39,14,1), E(47,15,1), E(49,16,1), E(51,17,1), E(55,18,1), E(57,19,1)
};
static const cavs_vlc_entry chroma2[] = {
    E(0,0,1), E(3,0,2), E(7,0,3), E(11,0,4), E(17,0,5), E(27,0,6),
    E(33,0,7), E(47,0,8), E(53,0,9), E(5,1,1), E(13,1,2), E(21,1,3),
    E(37,1,4), E(55,1,5), E(9,2,1), E(23,2,2), E(41,2,3), E(15,3,1),
    E(31,3,2), E(57,3,3), E(19,4,1), E(43,4,2), E(25,5,1), E(45,5,2),
    E(29,6,1), E(35,7,1), E(39,8,1), E(49,9,1), E(51,10,1)
};
static const cavs_vlc_entry chroma3[] = {
    E(1,0,1), E(3,0,2), E(5,0,3), E(7,0,4), E(11,0,5), E(15,0,6),
    E(19,0,7), E(23,0,8), E(29,0,9), E(35,0,10), E(43,0,11), E(47,0,12),
    E(53,0,13), E(9,1,1), E(13,1,2), E(21,1,3), E(31,1,4), E(39,1,5),
    E(51,1,6), E(17,2,1), E(27,2,2), E(37,2,3), E(25,3,1), E(41,3,2),
    E(33,4,1), E(55,4,2), E(45,5,1), E(49,6,1), E(57,7,1)
};
static const cavs_vlc_entry chroma4[] = {
    E(1,0,1), E(3,0,2), E(5,0,3), E(7,0,4), E(9,0,5), E(11,0,6),
    E(13,0,7), E(15,0,8), E(19,0,9), E(21,0,10), E(23,0,11), E(27,0,12),
    E(29,0,13), E(33,0,14), E(37,0,15), E(41,0,16), E(43,0,17),
    E(51,0,18), E(55,0,19), E(17,1,1), E(25,1,2), E(31,1,3), E(39,1,4),
    E(45,1,5), E(53,1,6), E(35,2,1), E(49,2,2), E(47,3,1), E(57,4,1)
};

static const cavs_vlc_table intra_tables[] = {
    { intra0, ENTRY_COUNT(intra0), 0, 0, 22, 2 },
    { intra1, ENTRY_COUNT(intra1), 8, 1, 14, 2 },
    { intra2, ENTRY_COUNT(intra2), 8, 1, 9, 2 },
    { intra3, ENTRY_COUNT(intra3), 8, 1, 6, 2 },
    { intra4, ENTRY_COUNT(intra4), 6, 1, 4, 2 },
    { intra5, ENTRY_COUNT(intra5), 0, 1, 2, 2 },
    { intra6, ENTRY_COUNT(intra6), 0, 1, 1, 2 }
};
static const cavs_vlc_table inter_tables[] = {
    { inter0, ENTRY_COUNT(inter0), 0, 0, 25, 3 },
    { inter1, ENTRY_COUNT(inter1), 2, 1, 18, 2 },
    { inter2, ENTRY_COUNT(inter2), 2, 1, 13, 2 },
    { inter3, ENTRY_COUNT(inter3), 2, 1, 9, 2 },
    { inter4, ENTRY_COUNT(inter4), 2, 1, 6, 2 },
    { inter5, ENTRY_COUNT(inter5), 0, 1, 4, 2 },
    { inter6, ENTRY_COUNT(inter6), 0, 1, 3, 2 }
};
static const cavs_vlc_table chroma_tables[] = {
    { chroma0, ENTRY_COUNT(chroma0), 0, 0, 24, 2 },
    { chroma1, ENTRY_COUNT(chroma1), 0, 1, 19, 0 },
    { chroma2, ENTRY_COUNT(chroma2), 2, 1, 10, 1 },
    { chroma3, ENTRY_COUNT(chroma3), 0, 1, 7, 1 },
    { chroma4, ENTRY_COUNT(chroma4), 0, 1, 4, 0 }
};

static const cavs_vlc_entry *find_entry(const cavs_vlc_table *table,
                                        uint32_t code) {
    unsigned index;
    for (index = 0U; index < table->entry_count; ++index) {
        if (table->entries[index].code == code) return &table->entries[index];
    }
    return NULL;
}

static unsigned select_table(cavs_basic_block_kind kind, uint32_t level) {
    if (kind == CAVS_BASIC_INTRA_LUMA) {
        if (level == 1U) return 1U;
        if (level == 2U) return 2U;
        if (level <= 4U) return 3U;
        if (level <= 7U) return 4U;
        if (level <= 10U) return 5U;
        return 6U;
    }
    if (kind == CAVS_BASIC_INTER_LUMA) {
        if (level == 1U) return 1U;
        if (level == 2U) return 2U;
        if (level == 3U) return 3U;
        if (level <= 6U) return 4U;
        if (level <= 9U) return 5U;
        return 6U;
    }
    if (level == 1U) return 1U;
    if (level == 2U) return 2U;
    if (level <= 4U) return 3U;
    return 4U;
}

static const cavs_vlc_table *table_for(cavs_basic_block_kind kind,
                                       unsigned table_index) {
    if (kind == CAVS_BASIC_INTRA_LUMA) return &intra_tables[table_index];
    if (kind == CAVS_BASIC_INTER_LUMA) return &inter_tables[table_index];
    return &chroma_tables[table_index];
}

static uint32_t reference_level(const cavs_vlc_table *table, uint32_t run) {
    uint32_t maximum = 0U;
    unsigned index;
    if (run > table->max_run) return 1U;
    for (index = 0U; index < table->entry_count; ++index) {
        if (table->entries[index].run == run &&
            table->entries[index].level > maximum)
            maximum = table->entries[index].level;
    }
    return maximum + 1U;
}

static cavs_result reconstruct_scan(cavs_basic_coefficients *parsed) {
    int coefficient = -1;
    int index;
    for (index = (int)parsed->count - 1; index >= 0; --index) {
        coefficient += (int)parsed->run[index] + 1;
        if (coefficient >= (int)CAVS_COEFFICIENT_COUNT_8X8)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed->scan_coefficients[coefficient] = parsed->level[index];
    }
    return CAVS_OK;
}

cavs_result cavs_decode_basic_coefficients_8x8(
    const uint8_t *data, size_t bit_size, size_t bit_offset,
    cavs_basic_block_kind kind, cavs_basic_coefficients *coefficients) {
    cavs_bitreader reader;
    cavs_basic_coefficients parsed;
    const cavs_vlc_table *table;
    const cavs_vlc_entry *entry;
    uint32_t code;
    uint32_t run;
    uint32_t magnitude;
    uint32_t escape_difference;
    uint32_t maximum_level = 0U;
    uint32_t occupied = 0U;
    unsigned table_index = 0U;
    int negative;

    if ((data == NULL && bit_size != 0U) || coefficients == NULL ||
        kind > CAVS_BASIC_CHROMA || bit_offset > bit_size ||
        !cavs_br_init_bits(&reader, data, bit_size))
        return CAVS_ERR_INVALID_ARGUMENT;
    reader.bit_pos = bit_offset;
    memset(&parsed, 0, sizeof(parsed));

    for (;;) {
        table = table_for(kind, table_index);
        if (!cavs_br_read_ue_k(&reader, table->golomb_order, &code))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        if (table->has_eob && code == table->eob) break;
        if (parsed.count >= CAVS_COEFFICIENT_COUNT_8X8)
            return CAVS_ERR_CORRUPT_BITSTREAM;

        if (code >= 59U) {
            run = (code - 59U) / 2U;
            if (run >= CAVS_COEFFICIENT_COUNT_8X8 ||
                !cavs_br_read_ue_k(&reader,
                    kind == CAVS_BASIC_INTRA_LUMA ? 1U : 0U,
                    &escape_difference))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            magnitude = reference_level(table, run);
            if (escape_difference > (uint32_t)INT32_MAX - magnitude)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            magnitude += escape_difference;
            negative = (code & 1U) != 0U;
        } else {
            entry = find_entry(table, code);
            negative = 0;
            if (entry == NULL) {
                if (code == 0U) return CAVS_ERR_CORRUPT_BITSTREAM;
                entry = find_entry(table, code - 1U);
                if (entry == NULL) return CAVS_ERR_CORRUPT_BITSTREAM;
                negative = 1;
            }
            run = entry->run;
            magnitude = entry->level;
        }
        if (run + 1U > CAVS_COEFFICIENT_COUNT_8X8 - occupied)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        occupied += run + 1U;
        parsed.run[parsed.count] = (uint8_t)run;
        parsed.level[parsed.count] = negative ? -(int32_t)magnitude : (int32_t)magnitude;
        ++parsed.count;
        if (magnitude > maximum_level) {
            maximum_level = magnitude;
            table_index = select_table(kind, magnitude);
        }
    }
    if (parsed.count == 0U || reconstruct_scan(&parsed) != CAVS_OK)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    parsed.end_bit_offset = reader.bit_pos;
    *coefficients = parsed;
    return CAVS_OK;
}


cavs_result cavs_inverse_scan_8x8(const int32_t scan[64],
                                  cavs_scan_mode_8x8 mode,
                                  int32_t matrix[64]) {
    const uint8_t *mapping;
    int32_t parsed[64];
    unsigned index;
    if (scan == NULL || matrix == NULL ||
        (mode != CAVS_SCAN_8X8_FRAME && mode != CAVS_SCAN_8X8_FIELD))
        return CAVS_ERR_INVALID_ARGUMENT;
    mapping = mode == CAVS_SCAN_8X8_FRAME ? frame_scan : field_scan;
    for (index = 0U; index < 64U; ++index)
        parsed[index] = scan[mapping[index]];
    for (index = 0U; index < 64U; ++index) matrix[index] = parsed[index];
    return CAVS_OK;
}

cavs_result cavs_map_chroma_qp(uint8_t luma_qp, int8_t delta,
                               uint8_t *chroma_qp) {
    int combined;
    if (chroma_qp == NULL || luma_qp > 63U)
        return CAVS_ERR_INVALID_ARGUMENT;
    combined = (int)luma_qp + (int)delta;
    if (combined < 0 || combined > 63)
        return CAVS_ERR_CORRUPT_BITSTREAM;
    *chroma_qp = chroma_qp_table[combined];
    return CAVS_OK;
}

cavs_result cavs_inverse_quantize_8x8(const int32_t quant[64],
                                      const int32_t predicted_quant[64],
                                      const uint8_t weights[64], uint8_t qp,
                                      int32_t coefficients[64]) {
    int32_t parsed[64];
    unsigned index;
    unsigned shift;
    if (quant == NULL || predicted_quant == NULL || weights == NULL ||
        coefficients == NULL || qp > 63U)
        return CAVS_ERR_INVALID_ARGUMENT;
    shift = dequant_shift[qp];
    for (index = 0U; index < 64U; ++index) {
        int64_t difference = (int64_t)quant[index] - predicted_quant[index];
        int64_t value;
        if (difference < -2048 || difference > 2047)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        value = floor_shift(difference * weights[index], 3U);
        value = floor_shift(value * dequant_table[qp], 4U);
        value = floor_shift(value + (INT64_C(1) << (shift - 1U)), shift);
        if (value < -8192 || value > 8191)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        parsed[index] = (int32_t)value;
    }
    for (index = 0U; index < 64U; ++index) coefficients[index] = parsed[index];
    return CAVS_OK;
}

#ifdef CAVS_TESTING
int cavs_test_basic_vlc_info(cavs_basic_block_kind kind, unsigned table_index,
                             unsigned entry_index, uint32_t *code,
                             uint32_t *run, uint32_t *level,
                             unsigned *entry_count) {
    const cavs_vlc_table *table;
    unsigned table_count;
    if (kind == CAVS_BASIC_CHROMA) table_count = 5U;
    else if (kind <= CAVS_BASIC_INTER_LUMA) table_count = 7U;
    else return 0;
    if (table_index >= table_count || code == NULL || run == NULL ||
        level == NULL || entry_count == NULL)
        return 0;
    table = table_for(kind, table_index);
    *entry_count = table->entry_count;
    if (entry_index >= table->entry_count) return 0;
    *code = table->entries[entry_index].code;
    *run = table->entries[entry_index].run;
    *level = table->entries[entry_index].level;
    return 1;
}
#endif

#undef ENTRY_COUNT
#undef E
