#ifndef CAVS_BITREADER_H
#define CAVS_BITREADER_H
#include <stddef.h>
#include <stdint.h>
typedef struct cavs_bitreader { const uint8_t *data; size_t size; size_t bit_pos; } cavs_bitreader;
void cavs_br_init(cavs_bitreader *br, const uint8_t *data, size_t size);
int cavs_br_read(cavs_bitreader *br, unsigned count, uint32_t *value);
int cavs_br_read_ue(cavs_bitreader *br, uint32_t *value);
#endif

