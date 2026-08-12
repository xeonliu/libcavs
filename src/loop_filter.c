/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016, 信息技术 高效多媒体编码 第16部分: 广播电视视频,
 * 9.11, Figures 32-34, Tables 64-65, and GB/T 20090.2-2013, 信息技术
 * 先进音视频编码 第2部分: 视频, 9.12, Figures 44-46, Tables 73-74.
 */
#include "loop_filter.h"
#include "reconstruction.h"
#include <limits.h>
#include <stddef.h>

static const uint8_t alpha_table[64] = {
     0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  3,  3,
     4,  4,  5,  5,  6,  7,  8,  9, 10, 11, 12, 13, 15, 16, 18, 20,
    22, 24, 26, 28, 30, 33, 33, 35, 35, 36, 37, 37, 39, 39, 42, 44,
    46, 48, 50, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64
};

static const uint8_t beta_table[64] = {
     0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
     2,  2,  3,  3,  3,  3,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,
     6,  7,  7,  7,  8,  8,  8,  9,  9, 10, 10, 11, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 23, 24, 24, 25, 25, 26, 27
};

static const uint8_t clipping_table[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4,
    5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9
};

typedef struct cavs_filter_motion {
    cavs_motion_vector direction[CAVS_MB_DIRECTIONS];
} cavs_filter_motion;

typedef struct cavs_filter_geometry {
    size_t macroblock_width;
    size_t macroblock_height;
    size_t field_rows;
    size_t stride[3];
} cavs_filter_geometry;

/* Clips an integer to a closed interval; this introduces no codec decision. */
static int clip_int(int minimum, int maximum, int value) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* Clips a reconstructed result to eight bits; this introduces no codec decision. */
static uint8_t clip_sample(int value) {
    return (uint8_t)clip_int(0, 255, value);
}

/* Computes an absolute difference without relying on library global state. */
static int difference(int first, int second) {
    return first >= second ? first - second : second - first;
}

/* Implements a portable arithmetic right shift; this introduces no codec decision. */
static int floor_shift(int value, unsigned shift) {
    int divisor = 1 << shift;
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

/* Compares motion components without overflowing signed subtraction. */
static int motion_difference_at_least(int32_t first, int32_t second,
                                      uint8_t threshold) {
    int64_t delta = (int64_t)first - second;
    if (delta < 0) delta = -delta;
    return delta >= threshold;
}

/* Locates the partition covering an 8x8 block; this is representation mapping only. */
static cavs_result block_motion(const cavs_macroblock *macroblock,
                                uint8_t block,
                                cavs_filter_motion *motion) {
    unsigned index;
    unsigned matches = 0U;
    uint8_t x;
    uint8_t y;
    if (macroblock == NULL || motion == NULL || block >= 4U)
        return CAVS_ERR_INVALID_ARGUMENT;
    x = (uint8_t)((block & 1U) * 8U + 4U);
    y = (uint8_t)((block >> 1U) * 8U + 4U);
    for (index = 0U; index < macroblock->partition_count; ++index) {
        const cavs_mb_partition *partition = &macroblock->partition[index];
        unsigned right = (unsigned)partition->x + partition->width;
        unsigned bottom = (unsigned)partition->y + partition->height;
        if (partition->width == 0U || partition->height == 0U ||
            right > 16U || bottom > 16U)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        if (x >= partition->x && x < right && y >= partition->y && y < bottom) {
            motion->direction[0] = partition->motion[0];
            motion->direction[1] = partition->motion[1];
            ++matches;
        }
    }
    if (matches != 1U) return CAVS_ERR_CORRUPT_BITSTREAM;
    return CAVS_OK;
}

/* Validates one direction before normative reference/MV comparisons. */
static cavs_result validate_direction(const cavs_motion_vector *motion) {
    if (motion->valid > 1U) return CAVS_ERR_CORRUPT_BITSTREAM;
    if (motion->valid != 0U && motion->reference_index < 0)
        return CAVS_ERR_MISSING_REFERENCE;
    return CAVS_OK;
}

/*
 * Implements GB/T 20090.16-2016 9.11.2 and GB/T 20090.2-2013 9.12.2:
 * (1) select the two 8x8 blocks; (2) assign 2 for an intra side; (3) for
 * P pictures and predicted I second fields compare one reference/MV pair;
 * (4) for B pictures compare corresponding forward and backward pairs.
 */
cavs_result cavs_loop_filter_boundary_strength(
    const cavs_macroblock *p, uint8_t p_block,
    const cavs_macroblock *q, uint8_t q_block,
    cavs_picture_type picture_type, uint8_t motion_unit,
    uint8_t *strength) {
    cavs_filter_motion p_motion;
    cavs_filter_motion q_motion;
    unsigned directions;
    unsigned index;
    cavs_result result;
    if (p == NULL || q == NULL || strength == NULL || p_block >= 4U ||
        q_block >= 4U || (motion_unit != 4U && motion_unit != 8U) ||
        picture_type > CAVS_PICTURE_B)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (p->is_intra != 0U || q->is_intra != 0U) {
        *strength = 2U;
        return CAVS_OK;
    }
    result = block_motion(p, p_block, &p_motion);
    if (result != CAVS_OK) return result;
    result = block_motion(q, q_block, &q_motion);
    if (result != CAVS_OK) return result;
    directions = picture_type == CAVS_PICTURE_B ? 2U : 1U;
    for (index = 0U; index < directions; ++index) {
        const cavs_motion_vector *pmv = &p_motion.direction[index];
        const cavs_motion_vector *qmv = &q_motion.direction[index];
        result = validate_direction(pmv);
        if (result != CAVS_OK) return result;
        result = validate_direction(qmv);
        if (result != CAVS_OK) return result;
        if (pmv->valid != qmv->valid ||
            (pmv->valid != 0U &&
             (pmv->reference_index != qmv->reference_index ||
              motion_difference_at_least(pmv->x, qmv->x, motion_unit) ||
              motion_difference_at_least(pmv->y, qmv->y, motion_unit)))) {
            *strength = 1U;
            return CAVS_OK;
        }
    }
    *strength = 0U;
    return CAVS_OK;
}

/*
 * Implements the Bs=2 luma process in Part 16 9.11.4 / Part 2 9.12.4.
 * q0 points to the first sample on the q side and step crosses the boundary.
 */
static void filter_strong_luma(uint8_t *q0, ptrdiff_t step,
                               int alpha, int beta) {
    int p2 = q0[-3 * step];
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int q2 = q0[2 * step];
    int sum = p0 + q0_value + 2;
    int inner_alpha = (alpha >> 2) + 2;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    if (difference(p2, p0) < beta &&
        difference(p0, q0_value) < inner_alpha) {
        q0[-step] = (uint8_t)((p1 + p0 + sum) >> 2);
        q0[-2 * step] = (uint8_t)((2 * p1 + sum) >> 2);
    } else {
        q0[-step] = (uint8_t)((2 * p1 + sum) >> 2);
    }
    if (difference(q2, q0_value) < beta &&
        difference(q0_value, p0) < inner_alpha) {
        q0[0] = (uint8_t)((q1 + q0_value + sum) >> 2);
        q0[step] = (uint8_t)((2 * q1 + sum) >> 2);
    } else {
        q0[0] = (uint8_t)((2 * q1 + sum) >> 2);
    }
}

/* Implements the Bs=2 chroma process in Part 16 9.11.4 / Part 2 9.12.4. */
static void filter_strong_chroma(uint8_t *q0, ptrdiff_t step,
                                 int alpha, int beta) {
    int p2 = q0[-3 * step];
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int q2 = q0[2 * step];
    int sum = p0 + q0_value + 2;
    int inner_alpha = (alpha >> 2) + 2;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    if (difference(p2, p0) < beta &&
        difference(p0, q0_value) < inner_alpha)
        q0[-step] = (uint8_t)((p1 + p0 + sum) >> 2);
    else
        q0[-step] = (uint8_t)((2 * p1 + sum) >> 2);
    if (difference(q2, q0_value) < beta &&
        difference(q0_value, p0) < inner_alpha)
        q0[0] = (uint8_t)((q1 + q0_value + sum) >> 2);
    else
        q0[0] = (uint8_t)((2 * q1 + sum) >> 2);
}

/* Implements the Bs=1 luma process in Part 16 9.11.5 / Part 2 9.12.5. */
static void filter_normal_luma(uint8_t *q0, ptrdiff_t step,
                               int alpha, int beta, int clipping) {
    int p2 = q0[-3 * step];
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int q2 = q0[2 * step];
    int delta;
    int filtered_p0;
    int filtered_q0;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    delta = clip_int(-clipping, clipping,
                     floor_shift((q0_value - p0) * 3 + p1 - q1 + 4, 3U));
    filtered_p0 = clip_sample(p0 + delta);
    filtered_q0 = clip_sample(q0_value - delta);
    q0[-step] = (uint8_t)filtered_p0;
    q0[0] = (uint8_t)filtered_q0;
    if (difference(p2, p0) < beta) {
        delta = clip_int(
            -clipping, clipping,
            floor_shift((filtered_p0 - p1) * 3 + p2 - filtered_q0 + 4, 3U));
        q0[-2 * step] = clip_sample(p1 + delta);
    }
    if (difference(q2, q0_value) < beta) {
        delta = clip_int(
            -clipping, clipping,
            floor_shift((q1 - filtered_q0) * 3 + filtered_p0 - q2 + 4, 3U));
        q0[step] = clip_sample(q1 - delta);
    }
}

/* Implements the Bs=1 chroma process in Part 16 9.11.5 / Part 2 9.12.5. */
static void filter_normal_chroma(uint8_t *q0, ptrdiff_t step,
                                 int alpha, int beta, int clipping) {
    int p1 = q0[-2 * step];
    int p0 = q0[-step];
    int q0_value = q0[0];
    int q1 = q0[step];
    int delta;
    if (difference(p0, q0_value) >= alpha || difference(p1, p0) >= beta ||
        difference(q1, q0_value) >= beta)
        return;
    delta = clip_int(-clipping, clipping,
                     floor_shift((q0_value - p0) * 3 + p1 - q1 + 4, 3U));
    q0[-step] = clip_sample(p0 + delta);
    q0[0] = clip_sample(q0_value - delta);
}

/* Applies one normative kernel to a contiguous boundary segment. */
static void filter_segment(uint8_t *q0, ptrdiff_t normal_step,
                           ptrdiff_t along_step, unsigned length,
                           uint8_t strength, uint8_t qp,
                           const cavs_loop_filter_config *config,
                           int chroma) {
    unsigned index;
    int index_a = clip_int(0, 63, (int)qp + config->alpha_c_offset);
    int index_b = clip_int(0, 63, (int)qp + config->beta_offset);
    int alpha = alpha_table[index_a];
    int beta = beta_table[index_b];
    int clipping = clipping_table[index_a];
    if (strength == 0U) return;
    for (index = 0U; index < length; ++index) {
        uint8_t *sample = q0 + (ptrdiff_t)index * along_step;
        if (strength == 2U) {
            if (chroma != 0)
                filter_strong_chroma(sample, normal_step, alpha, beta);
            else
                filter_strong_luma(sample, normal_step, alpha, beta);
        } else if (chroma != 0) {
            filter_normal_chroma(sample, normal_step, alpha, beta, clipping);
        } else {
            filter_normal_luma(sample, normal_step, alpha, beta, clipping);
        }
    }
}

/* Maps a metadata row to its physical field parity and logical field row. */
static void field_position(const cavs_picture *picture,
                           const cavs_loop_filter_config *config,
                           const cavs_filter_geometry *geometry,
                           size_t metadata_row, size_t *logical_row,
                           size_t *parity) {
    size_t field_index = metadata_row / geometry->field_rows;
    *logical_row = metadata_row % geometry->field_rows;
    if (picture->field_picture == 0U) {
        *logical_row = metadata_row;
        *parity = 0U;
    } else if (field_index == 0U) {
        *parity = config->first_field == CAVS_FIELD_BOTTOM ? 1U : 0U;
    } else {
        *parity = config->first_field == CAVS_FIELD_BOTTOM ? 0U : 1U;
    }
}

/* Returns the prior metadata row only when it belongs to the same field. */
static int has_top_neighbor(const cavs_picture *picture,
                            const cavs_filter_geometry *geometry,
                            size_t row) {
    if (picture->field_picture == 0U) return row != 0U;
    return row % geometry->field_rows != 0U;
}

/* Validates picture storage and derives dimensions; no codec decision is made. */
static cavs_result validate_geometry(const cavs_picture *picture,
                                     cavs_filter_geometry *geometry) {
    size_t expected_count;
    unsigned plane;
    if (picture == NULL || geometry == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (picture->format != CAVS_YUV420P8 || picture->coded_width == 0U ||
        picture->coded_height == 0U || picture->coded_width % 16U != 0U ||
        picture->coded_height % 16U != 0U || picture->field_picture > 1U ||
        picture->filtered > 1U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (picture->field_picture != 0U && picture->coded_height % 32U != 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    geometry->macroblock_width = picture->coded_width / 16U;
    geometry->macroblock_height = picture->coded_height / 16U;
    geometry->field_rows = picture->field_picture != 0U ?
        geometry->macroblock_height / 2U : geometry->macroblock_height;
    if (geometry->macroblock_width > SIZE_MAX / geometry->macroblock_height)
        return CAVS_ERR_INVALID_ARGUMENT;
    expected_count = geometry->macroblock_width * geometry->macroblock_height;
    if (picture->macroblock_width != geometry->macroblock_width ||
        picture->macroblock_height != geometry->macroblock_height ||
        picture->macroblock_count != expected_count)
        return CAVS_ERR_INVALID_ARGUMENT;
    for (plane = 0U; plane < 3U; ++plane) {
        size_t width = plane == 0U ? picture->coded_width :
                                     picture->coded_width / 2U;
        if (picture->plane[plane] == NULL || picture->stride[plane] <= 0 ||
            (uintmax_t)picture->stride[plane] > SIZE_MAX ||
            (size_t)picture->stride[plane] < width)
            return CAVS_ERR_INVALID_ARGUMENT;
        geometry->stride[plane] = (size_t)picture->stride[plane];
        if (picture->field_picture != 0U &&
            geometry->stride[plane] > (size_t)PTRDIFF_MAX / 2U)
            return CAVS_ERR_INVALID_ARGUMENT;
    }
    return CAVS_OK;
}

/* Validates metadata completely before any in-place sample modification. */
static cavs_result validate_macroblocks(const cavs_picture *picture,
                                        const cavs_loop_filter_config *config,
                                        const cavs_filter_geometry *geometry,
                                        size_t row_begin, size_t row_end) {
    size_t index;
    if (picture->macroblocks == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (row_begin > row_end || row_end > geometry->macroblock_height)
        return CAVS_ERR_INVALID_ARGUMENT;
    index = row_begin * geometry->macroblock_width;
    for (; index < row_end * geometry->macroblock_width; ++index) {
        const cavs_macroblock *macroblock = &picture->macroblocks[index];
        size_t row = index / geometry->macroblock_width;
        size_t column = index % geometry->macroblock_width;
        unsigned partition_index;
        uint8_t block;
        if (macroblock->address != index || macroblock->row != row ||
            macroblock->column != column || macroblock->qp > 63U ||
            macroblock->is_intra > 1U || macroblock->type >= CAVS_MB_INVALID ||
            (macroblock->is_intra != 0U) !=
                (macroblock->type == CAVS_MB_I_8X8))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        if (macroblock->is_intra != 0U) continue;
        if (macroblock->partition_count == 0U ||
            macroblock->partition_count > CAVS_MAX_MB_PARTITIONS)
            return CAVS_ERR_CORRUPT_BITSTREAM;
        for (partition_index = 0U;
             partition_index < macroblock->partition_count;
             ++partition_index) {
            const cavs_mb_partition *partition =
                &macroblock->partition[partition_index];
            if (partition->direction > CAVS_PRED_BIDIRECTIONAL)
                return CAVS_ERR_CORRUPT_BITSTREAM;
            if (picture->picture_type != CAVS_PICTURE_B &&
                partition->direction != CAVS_PRED_FORWARD)
                return CAVS_ERR_CORRUPT_BITSTREAM;
        }
        for (block = 0U; block < 4U; ++block) {
            cavs_filter_motion motion;
            cavs_result result = block_motion(macroblock, block, &motion);
            unsigned directions = picture->picture_type == CAVS_PICTURE_B ? 2U : 1U;
            unsigned direction;
            if (result != CAVS_OK) return result;
            for (direction = 0U; direction < directions; ++direction) {
                result = validate_direction(&motion.direction[direction]);
                if (result != CAVS_OK) return result;
            }
        }
    }
    (void)config;
    return CAVS_OK;
}

/* Maps and averages macroblock QPs for one luma or chroma edge. */
static cavs_result edge_qp(const cavs_macroblock *p,
                           const cavs_macroblock *q, int8_t chroma_delta,
                           uint8_t *luma, uint8_t *chroma) {
    uint8_t p_chroma;
    uint8_t q_chroma;
    int p_index;
    int q_index;
    cavs_result result;
    if (p == NULL || q == NULL || luma == NULL || chroma == NULL)
        return CAVS_ERR_INVALID_ARGUMENT;
    *luma = (uint8_t)(((unsigned)p->qp + q->qp + 1U) >> 1U);
    p_index = clip_int(0, 63, (int)p->qp + chroma_delta);
    q_index = clip_int(0, 63, (int)q->qp + chroma_delta);
    result = cavs_map_chroma_qp((uint8_t)p_index, 0, &p_chroma);
    if (result != CAVS_OK) return result;
    result = cavs_map_chroma_qp((uint8_t)q_index, 0, &q_chroma);
    if (result != CAVS_OK) return result;
    *chroma = (uint8_t)(((unsigned)p_chroma + q_chroma + 1U) >> 1U);
    return CAVS_OK;
}

/* Derives the two segment strengths on one macroblock edge. */
static cavs_result edge_strengths(const cavs_macroblock *p0, uint8_t p0_block,
                                  const cavs_macroblock *q0, uint8_t q0_block,
                                  const cavs_macroblock *p1, uint8_t p1_block,
                                  const cavs_macroblock *q1, uint8_t q1_block,
                                  cavs_picture_type picture_type,
                                  uint8_t motion_unit, uint8_t strength[2]) {
    cavs_result result = cavs_loop_filter_boundary_strength(
        p0, p0_block, q0, q0_block, picture_type, motion_unit, &strength[0]);
    if (result != CAVS_OK) return result;
    return cavs_loop_filter_boundary_strength(
        p1, p1_block, q1, q1_block, picture_type, motion_unit, &strength[1]);
}

/* Filters one external vertical macroblock edge, including both chroma planes. */
static void filter_external_vertical(
    cavs_picture *picture, const cavs_loop_filter_config *config,
    const cavs_filter_geometry *geometry, size_t x, size_t y,
    ptrdiff_t line_step, const uint8_t strength[2], uint8_t luma_qp,
    uint8_t cb_qp, uint8_t cr_qp) {
    unsigned segment;
    unsigned plane;
    uint8_t *luma = picture->plane[0] + y * geometry->stride[0] + x;
    for (segment = 0U; segment < 2U; ++segment)
        filter_segment(luma + (ptrdiff_t)(segment * 8U) * line_step,
                       1, line_step, 8U, strength[segment], luma_qp,
                       config, 0);
    for (plane = 1U; plane < 3U; ++plane) {
        size_t chroma_y = y / 2U +
            (picture->field_picture != 0U ? y & 1U : 0U);
        uint8_t qp = plane == 1U ? cb_qp : cr_qp;
        uint8_t *chroma = picture->plane[plane] +
            chroma_y * geometry->stride[plane] + x / 2U;
        ptrdiff_t chroma_step = picture->field_picture != 0U ?
            (ptrdiff_t)(geometry->stride[plane] * 2U) :
            (ptrdiff_t)geometry->stride[plane];
        for (segment = 0U; segment < 2U; ++segment)
            filter_segment(chroma + (ptrdiff_t)(segment * 4U) * chroma_step,
                           1, chroma_step, 4U, strength[segment], qp,
                           config, 1);
    }
}

/* Filters one external horizontal macroblock edge, including both chroma planes. */
static void filter_external_horizontal(
    cavs_picture *picture, const cavs_loop_filter_config *config,
    const cavs_filter_geometry *geometry, size_t x, size_t y,
    ptrdiff_t line_step, const uint8_t strength[2], uint8_t luma_qp,
    uint8_t cb_qp, uint8_t cr_qp) {
    unsigned segment;
    unsigned plane;
    uint8_t *luma = picture->plane[0] + y * geometry->stride[0] + x;
    for (segment = 0U; segment < 2U; ++segment)
        filter_segment(luma + segment * 8U, line_step, 1, 8U,
                       strength[segment], luma_qp, config, 0);
    for (plane = 1U; plane < 3U; ++plane) {
        size_t chroma_y = y / 2U +
            (picture->field_picture != 0U ? y & 1U : 0U);
        uint8_t qp = plane == 1U ? cb_qp : cr_qp;
        uint8_t *chroma = picture->plane[plane] +
            chroma_y * geometry->stride[plane] + x / 2U;
        ptrdiff_t chroma_step = picture->field_picture != 0U ?
            (ptrdiff_t)(geometry->stride[plane] * 2U) :
            (ptrdiff_t)geometry->stride[plane];
        for (segment = 0U; segment < 2U; ++segment)
            filter_segment(chroma + segment * 4U, chroma_step, 1, 4U,
                           strength[segment], qp, config, 1);
    }
}

/* Filters the internal vertical luma edge of one macroblock. */
static void filter_internal_vertical_luma(
    cavs_picture *picture, const cavs_loop_filter_config *config,
    const cavs_filter_geometry *geometry, size_t x, size_t y,
    ptrdiff_t line_step, const uint8_t strength[2], uint8_t qp) {
    unsigned segment;
    uint8_t *luma = picture->plane[0] + y * geometry->stride[0] + x;
    for (segment = 0U; segment < 2U; ++segment)
        filter_segment(luma + 8U + (ptrdiff_t)(segment * 8U) * line_step,
                       1, line_step, 8U, strength[segment], qp, config, 0);
}

/* Filters the internal horizontal luma edge of one macroblock. */
static void filter_internal_horizontal_luma(
    cavs_picture *picture, const cavs_loop_filter_config *config,
    const cavs_filter_geometry *geometry, size_t x, size_t y,
    ptrdiff_t line_step, const uint8_t strength[2], uint8_t qp) {
    unsigned segment;
    uint8_t *luma = picture->plane[0] + y * geometry->stride[0] + x;
    for (segment = 0U; segment < 2U; ++segment)
        filter_segment(luma + (ptrdiff_t)8U * line_step + segment * 8U,
                       line_step, 1, 8U, strength[segment], qp, config, 0);
}

/*
 * Implements Part 16 9.11.1 / Part 2 9.12.1 traversal: for each macroblock,
 * filter vertical edges left-to-right, then horizontal edges top-to-bottom.
 */
static cavs_result filter_macroblocks(
    cavs_picture *picture, const cavs_loop_filter_config *config,
    const cavs_filter_geometry *geometry, size_t row_begin, size_t row_end) {
    size_t row;
    for (row = row_begin; row < row_end; ++row) {
        size_t column;
        size_t logical_row;
        size_t parity;
        field_position(picture, config, geometry, row, &logical_row, &parity);
        for (column = 0U; column < geometry->macroblock_width; ++column) {
            size_t index = row * geometry->macroblock_width + column;
            cavs_macroblock *current = &picture->macroblocks[index];
            size_t x = column * 16U;
            size_t y = logical_row * (picture->field_picture != 0U ? 32U : 16U) + parity;
            ptrdiff_t line_step = picture->field_picture != 0U ?
                (ptrdiff_t)(geometry->stride[0] * 2U) :
                (ptrdiff_t)geometry->stride[0];
            uint8_t vertical[2];
            uint8_t horizontal[2];
            uint8_t luma_qp;
            uint8_t cb_qp;
            uint8_t cr_qp;
            cavs_result result;

            if (column != 0U) {
                cavs_macroblock *left = &picture->macroblocks[index - 1U];
                if (left->slice_id == current->slice_id) {
                    result = edge_strengths(left, 1U, current, 0U,
                                            left, 3U, current, 2U,
                                            picture->picture_type,
                                            config->motion_unit, vertical);
                    if (result != CAVS_OK) return result;
                    result = edge_qp(left, current,
                                     config->chroma_qp_delta_cb,
                                     &luma_qp, &cb_qp);
                    if (result != CAVS_OK) return result;
                    result = edge_qp(left, current,
                                     config->chroma_qp_delta_cr,
                                     &luma_qp, &cr_qp);
                    if (result != CAVS_OK) return result;
                    filter_external_vertical(picture, config, geometry, x, y,
                                             line_step, vertical, luma_qp,
                                             cb_qp, cr_qp);
                }
            }
            result = edge_strengths(current, 0U, current, 1U,
                                    current, 2U, current, 3U,
                                    picture->picture_type,
                                    config->motion_unit, vertical);
            if (result != CAVS_OK) return result;
            filter_internal_vertical_luma(picture, config, geometry, x, y,
                                          line_step, vertical, current->qp);

            if (has_top_neighbor(picture, geometry, row)) {
                cavs_macroblock *top =
                    &picture->macroblocks[index - geometry->macroblock_width];
                if (top->slice_id == current->slice_id) {
                    result = edge_strengths(top, 2U, current, 0U,
                                            top, 3U, current, 1U,
                                            picture->picture_type,
                                            config->motion_unit, horizontal);
                    if (result != CAVS_OK) return result;
                    result = edge_qp(top, current,
                                     config->chroma_qp_delta_cb,
                                     &luma_qp, &cb_qp);
                    if (result != CAVS_OK) return result;
                    result = edge_qp(top, current,
                                     config->chroma_qp_delta_cr,
                                     &luma_qp, &cr_qp);
                    if (result != CAVS_OK) return result;
                    filter_external_horizontal(picture, config, geometry,
                                               x, y, line_step, horizontal,
                                               luma_qp, cb_qp, cr_qp);
                }
            }
            result = edge_strengths(current, 0U, current, 2U,
                                    current, 1U, current, 3U,
                                    picture->picture_type,
                                    config->motion_unit, horizontal);
            if (result != CAVS_OK) return result;
            filter_internal_horizontal_luma(picture, config, geometry, x, y,
                                            line_step, horizontal, current->qp);
        }
    }
    return CAVS_OK;
}

/* Validates the syntax-controlled filter configuration before modification. */
static cavs_result validate_config(
    const cavs_picture *picture, const cavs_loop_filter_config *config) {
    if (config == NULL || config->enabled > 1U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if ((config->motion_unit != 4U && config->motion_unit != 8U) ||
        config->alpha_c_offset < -8 || config->alpha_c_offset > 8 ||
        config->beta_offset < -8 || config->beta_offset > 8 ||
        config->chroma_qp_delta_cb < -16 ||
        config->chroma_qp_delta_cb > 16 ||
        config->chroma_qp_delta_cr < -16 ||
        config->chroma_qp_delta_cr > 16 ||
        (picture->field_picture != 0U &&
         config->first_field != CAVS_FIELD_TOP &&
         config->first_field != CAVS_FIELD_BOTTOM))
        return CAVS_ERR_INVALID_ARGUMENT;
    return CAVS_OK;
}

cavs_result cavs_loop_filter_field(
    cavs_picture *picture, const cavs_loop_filter_config *config,
    uint8_t field) {
    cavs_filter_geometry geometry;
    size_t row_begin;
    size_t row_end;
    cavs_result result;
    result = validate_geometry(picture, &geometry);
    if (result != CAVS_OK) return result;
    result = validate_config(picture, config);
    if (result != CAVS_OK) return result;
    if (picture->field_picture == 0U ||
        (field != CAVS_FIELD_TOP && field != CAVS_FIELD_BOTTOM))
        return CAVS_ERR_INVALID_ARGUMENT;
    if (picture->picture_type > CAVS_PICTURE_B ||
        (picture->completed_fields & (uint8_t)~CAVS_FIELD_BOTH) != 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (picture->filtered != 0U) return CAVS_ERR_INVALID_STATE;
    if ((picture->completed_fields & field) == 0U)
        return CAVS_ERR_INVALID_STATE;
    row_begin = field == config->first_field ? 0U : geometry.field_rows;
    row_end = row_begin + geometry.field_rows;
    if (config->enabled == 0U) return CAVS_OK;
    result = validate_macroblocks(picture, config, &geometry,
                                  row_begin, row_end);
    if (result != CAVS_OK) return result;
    return filter_macroblocks(picture, config, &geometry,
                              row_begin, row_end);
}

cavs_result cavs_loop_filter_picture(
    cavs_picture *picture, const cavs_loop_filter_config *config) {
    cavs_filter_geometry geometry;
    cavs_result result;
    result = validate_geometry(picture, &geometry);
    if (result != CAVS_OK) return result;
    result = validate_config(picture, config);
    if (result != CAVS_OK) return result;
    if (picture->picture_type > CAVS_PICTURE_B ||
        (picture->completed_fields & (uint8_t)~CAVS_FIELD_BOTH) != 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    if (picture->filtered != 0U) return CAVS_ERR_INVALID_STATE;
    if ((picture->completed_fields & CAVS_FIELD_BOTH) != CAVS_FIELD_BOTH)
        return CAVS_ERR_INVALID_STATE;
    if (config->enabled == 0U) {
        picture->filtered = 1U;
        return CAVS_OK;
    }
    result = validate_macroblocks(picture, config, &geometry, 0U,
                                  geometry.macroblock_height);
    if (result != CAVS_OK) return result;
    result = filter_macroblocks(picture, config, &geometry, 0U,
                                geometry.macroblock_height);
    if (result != CAVS_OK) return result;
    picture->filtered = 1U;
    return CAVS_OK;
}
