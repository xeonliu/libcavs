/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Internal decoded-picture buffer and display queue contract.
 */
#ifndef CAVS_DPB_H
#define CAVS_DPB_H

#include "image.h"
#include <stddef.h>
#include <stdint.h>

#define CAVS_DPB_MAX_REFERENCES 4U
#define CAVS_DPB_MAX_OUTPUT 4U
#define CAVS_DPB_DISTANCE_MODULUS 512U

typedef enum cavs_dpb_direction {
    CAVS_DPB_FORWARD = 0,
    CAVS_DPB_BACKWARD = 1
} cavs_dpb_direction;

typedef void (*cavs_dpb_picture_retain)(void *opaque,
                                        cavs_picture *picture);
typedef void (*cavs_dpb_picture_release)(void *opaque,
                                         cavs_picture *picture);

typedef struct cavs_dpb_lifetime {
    cavs_dpb_picture_retain retain;
    cavs_dpb_picture_release release;
    void *opaque;
} cavs_dpb_lifetime;

typedef struct cavs_dpb_reference {
    cavs_picture *picture;
    /* DistanceIndex from 9.6.1, not picture_distance from the bitstream. */
    uint16_t distance;
    /* Populated by selection; zero in persistent DPB entries. */
    uint16_t block_distance;
    uint8_t field;
} cavs_dpb_reference;

typedef struct cavs_dpb {
    cavs_dpb_reference reference[CAVS_DPB_MAX_REFERENCES];
    size_t reference_count;
    cavs_picture *output[CAVS_DPB_MAX_OUTPUT];
    size_t output_count;
    cavs_picture *delayed_output;
    cavs_picture *current_picture;
    uint16_t current_distance;
    uint8_t current_field;
    uint8_t draining;
    cavs_dpb_lifetime lifetime;
} cavs_dpb;

/*
 * GB/T 20090.16-2016 6.5, 9.4.5 and Figures 14-20 select field/frame
 * references by display distance. GB/T 20090.2-2013 9.5 defines the
 * shared distance arithmetic. Queue storage itself introduces no codec
 * decision.
 */
cavs_result cavs_dpb_init(cavs_dpb *dpb);
cavs_result cavs_dpb_set_lifetime(cavs_dpb *dpb,
                                  const cavs_dpb_lifetime *lifetime);

/*
 * GB/T 20090.16-2016 9.4.6.1 and GB/T 20090.2-2013 9.6.1 define
 * DistanceIndex modulo 512. Physical field parity is independent of whether
 * that field is first in display order.
 */
cavs_result cavs_dpb_picture_distance(const cavs_picture *picture,
                                      uint8_t field,
                                      uint16_t *distance_index);

/*
 * Establishes the current frame or physical field used by reference lookup.
 * Successive fields of one picture are begun with the same picture pointer;
 * end_picture discards an uncommitted picture rather than completing a field.
 */
cavs_result cavs_dpb_begin_picture(cavs_dpb *dpb, cavs_picture *picture,
                                   uint8_t field);
cavs_result cavs_dpb_end_picture(cavs_dpb *dpb, cavs_picture *picture);

cavs_result cavs_dpb_select_reference_entry(
    const cavs_dpb *dpb, uint8_t direction, uint8_t reference_index,
    uint8_t field, cavs_dpb_reference *reference);
cavs_result cavs_dpb_select_reference(
    const cavs_dpb *dpb, uint8_t direction, uint8_t reference_index,
    uint8_t field, cavs_picture **picture);

/*
 * Returns co-located macroblock metadata at a luma frame-sample position.
 * block_index is the 8x8 luma block index within that macroblock.
 */
cavs_result cavs_dpb_colocated_macroblock(
    const cavs_dpb *dpb, uint8_t direction, uint8_t reference_index,
    uint8_t field, uint32_t sample_x, uint32_t sample_y,
    cavs_dpb_reference *reference, const cavs_macroblock **macroblock,
    uint8_t *block_index);

cavs_result cavs_dpb_store(cavs_dpb *dpb, cavs_picture *picture);
/* Transfers the DPB-owned output reference to the caller on success. */
cavs_result cavs_dpb_pop_output(cavs_dpb *dpb, cavs_picture **picture);
/* EOF and sequence-end both use this idempotent drain transition. */
cavs_result cavs_dpb_flush(cavs_dpb *dpb);
cavs_result cavs_dpb_reset(cavs_dpb *dpb);
void cavs_dpb_destroy(cavs_dpb *dpb);

#endif
