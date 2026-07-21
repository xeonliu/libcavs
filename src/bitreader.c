#include "bitreader.h"
#include <limits.h>

void cavs_br_init(cavs_bitreader *br, const uint8_t *data, size_t size) {
    br->data = data; br->size = size; br->bit_pos = 0;
}

int cavs_br_read(cavs_bitreader *br, unsigned count, uint32_t *value) {
    uint32_t out = 0;
    unsigned i;
    if (br == NULL || value == NULL || count > 32U ||
        br->size > SIZE_MAX / 8U || br->bit_pos > br->size * 8U ||
        (size_t)count > br->size * 8U - br->bit_pos) return 0;
    for (i = 0; i < count; ++i) {
        size_t pos = br->bit_pos++;
        out = (out << 1) | (uint32_t)((br->data[pos / 8U] >> (7U - (pos % 8U))) & 1U);
    }
    *value = out;
    return 1;
}

int cavs_br_read_ue(cavs_bitreader *br, uint32_t *value) {
    unsigned zeros = 0;
    uint32_t bit, suffix;
    while (zeros < 32U) {
        if (!cavs_br_read(br, 1, &bit)) return 0;
        if (bit != 0U) break;
        ++zeros;
    }
    if (zeros == 32U) return 0;
    if (zeros == 0U) { *value = 0; return 1; }
    if (!cavs_br_read(br, zeros, &suffix)) return 0;
    *value = (UINT32_C(1) << zeros) - UINT32_C(1) + suffix;
    return 1;
}

