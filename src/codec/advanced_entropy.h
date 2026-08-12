/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Broadcast-profile advanced entropy decoder state.
 */
#ifndef CAVS_ADVANCED_ENTROPY_H
#define CAVS_ADVANCED_ENTROPY_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_AE_CONTEXT_COUNT 323U

typedef struct cavs_ae_context {
    uint16_t lg_pmps;
    uint8_t mps;
    uint8_t cycle;
} cavs_ae_context;

typedef struct cavs_ae_decoder {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_offset;
    uint16_t range_s;
    uint16_t range_t;
    uint16_t value_s;
    uint16_t value_t;
} cavs_ae_decoder;

/*
 * GB/T 20090.16-2016 8.4.2.1 initializes every context to
 * { mps = 0, cycle = 0, lgPmps = 1023 } before each slice.
 */
void cavs_ae_contexts_init(cavs_ae_context *contexts, size_t count);

/*
 * GB/T 20090.16-2016 8.4.2.2 and informative Annex E.2 initialize
 * the arithmetic decoder from the first byte-aligned slice payload bit.
 */
cavs_result cavs_ae_decoder_init(cavs_ae_decoder *decoder,
                                 const uint8_t *data, size_t bit_size,
                                 size_t bit_offset);

/*
 * GB/T 20090.16-2016 8.4.3.2 and 8.4.3.5 decode one regular bin and
 * update its context model.
 */
cavs_result cavs_ae_decode(cavs_ae_decoder *decoder,
                           cavs_ae_context *context, uint8_t *bin);

/*
 * GB/T 20090.16-2016 8.4.3.2 defines coefficient-level context weighting.
 * Both participating contexts are updated by the decoded bin.
 */
cavs_result cavs_ae_decode_weighted(cavs_ae_decoder *decoder,
                                    cavs_ae_context *first,
                                    cavs_ae_context *second, uint8_t *bin);

/* GB/T 20090.16-2016 8.4.3.3 decodes one bypass bin. */
cavs_result cavs_ae_decode_bypass(cavs_ae_decoder *decoder, uint8_t *bin);

/*
 * GB/T 20090.16-2016 7.4, 8.4.3.4 and informative Annex E.5 decode one
 * advanced-entropy macroblock stuffing bin. Slice syntax validates the last
 * macroblock's bin separately.
 */
cavs_result cavs_ae_decode_stuffing(cavs_ae_decoder *decoder, uint8_t *bin);

/* Returns the first unread source bit; this helper adds no codec decision. */
size_t cavs_ae_bit_offset(const cavs_ae_decoder *decoder);

#endif
