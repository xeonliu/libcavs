/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Scalar reconstruction tools for the 0x48 broadcast-profile path.
 */
#ifndef CAVS_BROADCAST_RECONSTRUCTION_H
#define CAVS_BROADCAST_RECONSTRUCTION_H

#include "image.h"
#include <stddef.h>
#include <stdint.h>

#define CAVS_BLOCK_4X4_COEFFICIENTS 16U
#define CAVS_BROADCAST_LUMA_SAMPLES 256U
#define CAVS_BROADCAST_CHROMA_SAMPLES 64U

typedef struct cavs_field_plane {
    uint8_t *data;
    size_t width;
    size_t height;
    ptrdiff_t stride;
} cavs_field_plane;

typedef struct cavs_macroblock_prediction_420 {
    uint8_t luma[CAVS_BROADCAST_LUMA_SAMPLES];
    uint8_t chroma[2][CAVS_BROADCAST_CHROMA_SAMPLES];
} cavs_macroblock_prediction_420;

typedef struct cavs_broadcast_reconstruction_context {
    cavs_picture *picture;
    const cavs_macroblock_prediction_420 *inter_prediction;
    uint16_t slice_id;
    uint8_t field;
    uint8_t weighting_quant_flag;
    int8_t chroma_qp_delta_cb;
    int8_t chroma_qp_delta_cr;
} cavs_broadcast_reconstruction_context;

/*
 * GB/T 20090.16-2016 6.2 and 9.8.2 expose a field as alternating lines of
 * the frame sample matrix. GB/T 20090.2-2013 6.2 uses the same layout.
 * This helper constructs a plane view only and introduces no codec decision.
 */
cavs_result cavs_picture_field_plane(cavs_picture *picture, unsigned plane,
                                     uint8_t field, cavs_field_plane *view);

/*
 * GB/T 20090.16-2016 9.5-9.7, Figures 22-23 and Table 62: inverse-scan has
 * already been performed by the entropy module; this applies the default
 * (non-weighted) 8x8 inverse quantization and inverse transform.
 */
cavs_result cavs_broadcast_inverse_quantize_8x8(
    const int16_t quant[64], uint8_t qp, int32_t coefficients[64]);
cavs_result cavs_broadcast_inverse_transform_8x8(
    const int32_t coefficients[64], int16_t residual[64]);

/*
 * GB/T 20090.2-2013 9.7.2, 9.8.3 and Table 71 define the compatible 4x4
 * inverse quantization and inverse transform used by the unified contract.
 */
cavs_result cavs_broadcast_inverse_quantize_4x4(
    const int16_t quant[16], uint8_t qp, int32_t coefficients[16]);
cavs_result cavs_broadcast_inverse_transform_4x4(
    const int32_t coefficients[16], int16_t residual[16]);

/*
 * GB/T 20090.16-2016 9.4, 9.6-9.10 and GB/T 20090.2-2013 9.8-9.11:
 * reconstructs one macroblock into unfiltered picture storage. Intra
 * prediction reads only already committed samples from the same slice and
 * field. The macroblock and all samples are committed atomically.
 */
cavs_result cavs_broadcast_reconstruct_macroblock(
    const cavs_broadcast_reconstruction_context *context,
    const cavs_macroblock *macroblock);

#endif
