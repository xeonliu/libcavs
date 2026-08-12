/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Picture-reconstruction cursor for one complete slice payload.
 */
#ifndef CAVS_SLICE_DECODE_H
#define CAVS_SLICE_DECODE_H

#include "codec/broadcast_reconstruction.h"
#include <stddef.h>
#include <stdint.h>

typedef struct cavs_slice_cursor {
    size_t bit_offset;
    size_t bit_size;
    uint32_t macroblock_address;
    uint16_t row;
    uint16_t column;
    uint16_t start_row;
    uint16_t end_row;
    uint8_t field;
    uint8_t finished;
} cavs_slice_cursor;

/*
 * Reads one complete macroblock from the stateful arithmetic decoder in
 * opaque. CAVS_EOF means that the entropy module has decoded the required
 * value-one final aec_mb_stuffing_bit and found no further macroblock. Source
 * bits not shifted into the arithmetic state are finalization data, not
 * byte-aligned padding syntax. The callback must copy its full arithmetic
 * state before decoding and commit it only on CAVS_OK.
 * A successful macroblock must set end_bit_offset no earlier than the cursor's
 * current offset and no later than the declared payload size. Arithmetic bins
 * may be consumed entirely from the decoder's prefetched range/value state,
 * so even an explicit macroblock need not advance the first-unread source bit.
 */
typedef cavs_result (*cavs_slice_macroblock_reader)(
    void *opaque, uint32_t macroblock_address, cavs_macroblock *macroblock);

/*
 * GB/T 20090.16-2016 6.3, 7.4 and 9.3 initialize raster scan at the slice
 * row. For successive-field pictures, rows [0,H/32) are the first field and
 * rows [H/32,2*H/32) are the second field.
 */
cavs_result cavs_slice_cursor_init(const cavs_picture *picture, uint8_t field,
                                   uint16_t slice_row, size_t header_bits,
                                   size_t payload_bits,
                                   cavs_slice_cursor *cursor);

/*
 * GB/T 20090.16-2016 7.4 and 9.3: a slice ends at the next slice start row,
 * or at the field end for the last slice. The explicit range keeps that
 * boundary separate from the payload's arithmetic finalization bits.
 */
cavs_result cavs_slice_cursor_init_range(
    const cavs_picture *picture, uint8_t field, uint16_t slice_row,
    uint16_t end_row, size_t header_bits, size_t payload_bits,
    cavs_slice_cursor *cursor);

/* Decodes exactly the declared slice-row range and verifies payload end. */
cavs_result cavs_slice_decode(
    cavs_slice_cursor *cursor, cavs_slice_macroblock_reader read_macroblock,
    void *reader_opaque,
    const cavs_broadcast_reconstruction_context *reconstruction);

#endif
