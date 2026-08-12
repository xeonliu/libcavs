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

typedef struct cavs_dpb_reference {
    cavs_picture *picture;
    uint16_t distance;
    uint8_t field;
} cavs_dpb_reference;

typedef struct cavs_dpb {
    cavs_dpb_reference reference[CAVS_DPB_MAX_REFERENCES];
    size_t reference_count;
    cavs_picture *output[CAVS_DPB_MAX_OUTPUT];
    size_t output_count;
} cavs_dpb;

/*
 * GB/T 20090.16-2016 6.5, 9.5 and Figures 14-20 select field/frame
 * references by display distance. GB/T 20090.2-2013 9.4.2 defines the
 * shared distance arithmetic. Queue storage itself introduces no codec
 * decision.
 */
cavs_result cavs_dpb_init(cavs_dpb *dpb);
cavs_result cavs_dpb_select_reference(
    const cavs_dpb *dpb, uint8_t direction, uint8_t reference_index,
    uint8_t field, cavs_picture **picture);
cavs_result cavs_dpb_store(cavs_dpb *dpb, cavs_picture *picture);
cavs_result cavs_dpb_pop_output(cavs_dpb *dpb, cavs_picture **picture);
cavs_result cavs_dpb_flush(cavs_dpb *dpb);

#endif
