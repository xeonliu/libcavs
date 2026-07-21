#include "bitreader.h"
#include <assert.h>
#include <stdint.h>
int main(void) {
    static const uint8_t data[] = { 0xb3, 0x80 };
    cavs_bitreader br; uint32_t value;
    cavs_br_init(&br, data, sizeof(data));
    assert(cavs_br_read(&br, 4, &value) && value == 11U);
    assert(cavs_br_read(&br, 4, &value) && value == 3U);
    cavs_br_init(&br, data + 1, 1); assert(cavs_br_read_ue(&br, &value) && value == 0U);
    assert(!cavs_br_read(&br, 8, &value));
    return 0;
}

