/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 9.9, Table 21/22 parameters and the three normative
 * WeightingQuantModel distributions for an 8x8 broadcast transform block.
 */
#ifndef CAVS_BROADCAST_QUANT_H
#define CAVS_BROADCAST_QUANT_H

#include <cavs/cavs.h>
#include <stdint.h>

#define CAVS_BROADCAST_QUANT_MATRIX_SIZE 64U

/*
 * GB/T 20090.16-2016 9.9 constructs wqP from the selected parameter set and
 * then maps its six entries through the selected weighting model. The
 * parameter arrays are used only for indices 1 and 2; this keeps the caller
 * from inventing syntax for an unused parameter set.
 */
cavs_result cavs_broadcast_build_weight_matrix(
    uint8_t parameter_index, uint8_t model,
    const int8_t delta1[6], const int8_t delta2[6], uint8_t matrix[64]);

#endif
