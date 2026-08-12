/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Baseline-profile 8x8 intra prediction from GB/T 20090.2-2013
 * 9.9.1-9.9.4, Tables 65-66, and Figure 20.
 */
#ifndef CAVS_PREDICTION_H
#define CAVS_PREDICTION_H

#include <cavs/cavs.h>
#include <stddef.h>
#include <stdint.h>

#define CAVS_INTRA_REFERENCE_COUNT_8X8 17U
#define CAVS_PREDICTION_BLOCK_SAMPLES_8X8 64U

typedef struct cavs_intra_references_8x8 {
    /* Index 0 is the shared top-left sample after the 9.9.2 fallback rule. */
    uint8_t top[CAVS_INTRA_REFERENCE_COUNT_8X8];
    uint8_t left[CAVS_INTRA_REFERENCE_COUNT_8X8];
    uint32_t top_available;
    uint32_t left_available;
} cavs_intra_references_8x8;

typedef struct cavs_intra_availability_8x8 {
    /* Bit 0 describes sample 1 and bit 15 describes sample 16. */
    uint16_t top;
    uint16_t left;
    uint8_t top_left;
} cavs_intra_availability_8x8;

typedef enum cavs_intra_luma_mode_8x8 {
    CAVS_INTRA_LUMA_VERTICAL_8X8 = 0,
    CAVS_INTRA_LUMA_HORIZONTAL_8X8 = 1,
    CAVS_INTRA_LUMA_DC_8X8 = 2,
    CAVS_INTRA_LUMA_DOWN_LEFT_8X8 = 3,
    CAVS_INTRA_LUMA_DOWN_RIGHT_8X8 = 4
} cavs_intra_luma_mode_8x8;

typedef enum cavs_intra_chroma_mode_8x8 {
    CAVS_INTRA_CHROMA_DC_8X8 = 0,
    CAVS_INTRA_CHROMA_HORIZONTAL_8X8 = 1,
    CAVS_INTRA_CHROMA_VERTICAL_8X8 = 2,
    CAVS_INTRA_CHROMA_PLANE_8X8 = 3
} cavs_intra_chroma_mode_8x8;

/*
 * Acquires 9.9.2 references from a row-major picture or field plane. The
 * availability flags must already include picture, slice, decoding-order,
 * and constrained-DC restrictions.
 */
cavs_result cavs_acquire_intra_references_8x8(
    const uint8_t *plane, size_t width, size_t height, size_t stride,
    size_t x0, size_t y0, const cavs_intra_availability_8x8 *availability,
    cavs_intra_references_8x8 *references);

/* Produces row-major predMatrix[x,y] for one baseline luma block. */
cavs_result cavs_dsp_predict_intra_luma_8x8_c(
    const cavs_intra_references_8x8 *references,
    cavs_intra_luma_mode_8x8 mode,
    uint8_t prediction[CAVS_PREDICTION_BLOCK_SAMPLES_8X8]);

/* Produces row-major predMatrix[x,y] for one baseline chroma block. */
cavs_result cavs_dsp_predict_intra_chroma_8x8_c(
    const cavs_intra_references_8x8 *references,
    cavs_intra_chroma_mode_8x8 mode,
    uint8_t prediction[CAVS_PREDICTION_BLOCK_SAMPLES_8X8]);

#endif
