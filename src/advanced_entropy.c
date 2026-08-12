/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GB/T 20090.16-2016 8.4 and informative Annex E. This is a bounded,
 * single-slice implementation with no global decoder state.
 */
#include "advanced_entropy.h"

static int read_bit(cavs_ae_decoder *decoder, uint16_t *bit) {
    size_t offset;
    if (decoder == NULL || bit == NULL || decoder->bit_offset >= decoder->bit_size)
        return 0;
    offset = decoder->bit_offset++;
    *bit = (uint16_t)((decoder->data[offset / 8U] >>
                       (7U - (offset % 8U))) & UINT8_C(1));
    return 1;
}

/* Input bookkeeping only; this helper adds no codec decision. */
static int shift_in(cavs_ae_decoder *decoder, uint16_t *value) {
    uint16_t bit;
    if (!read_bit(decoder, &bit)) return 0;
    *value = (uint16_t)((*value << 1) | bit);
    return 1;
}

void cavs_ae_contexts_init(cavs_ae_context *contexts, size_t count) {
    size_t index;
    if (contexts == NULL) return;
    for (index = 0U; index < count; ++index) {
        contexts[index].lg_pmps = UINT16_C(1023);
        contexts[index].mps = 0U;
        contexts[index].cycle = 0U;
    }
}

cavs_result cavs_ae_decoder_init(cavs_ae_decoder *decoder,
                                 const uint8_t *data, size_t bit_size,
                                 size_t bit_offset) {
    cavs_ae_decoder parsed;
    unsigned index;
    if (decoder == NULL || (data == NULL && bit_size != 0U) ||
        bit_offset > bit_size || (bit_offset & 7U) != 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    parsed.data = data;
    parsed.bit_size = bit_size;
    parsed.bit_offset = bit_offset;
    parsed.range_s = 0U;
    parsed.range_t = UINT16_C(255);
    parsed.value_s = 0U;
    parsed.value_t = 0U;
    for (index = 0U; index < 9U; ++index) {
        if (!shift_in(&parsed, &parsed.value_t))
            return CAVS_ERR_CORRUPT_BITSTREAM;
    }
    while (parsed.value_t < UINT16_C(256)) {
        if (!shift_in(&parsed, &parsed.value_t))
            return CAVS_ERR_CORRUPT_BITSTREAM;
        ++parsed.value_s;
    }
    parsed.value_t &= UINT16_C(255);
    *decoder = parsed;
    return CAVS_OK;
}

/*
 * GB/T 20090.16-2016 8.4.3.5 update_ctx. The arithmetic shift widths and
 * constants below are the normative pseudocode values.
 */
static void update_context(cavs_ae_context *context, uint8_t bin) {
    unsigned cwr = context->cycle <= 1U ? 3U :
                   (context->cycle == 2U ? 4U : 5U);
    uint16_t probability = context->lg_pmps;
    if (bin != context->mps) {
        if (context->cycle <= 2U) ++context->cycle;
        if (cwr == 3U) probability = (uint16_t)(probability + 197U);
        else if (cwr == 4U) probability = (uint16_t)(probability + 95U);
        else probability = (uint16_t)(probability + 46U);
        if (probability > UINT16_C(1023)) {
            probability = (uint16_t)(UINT16_C(2047) - probability);
            context->mps ^= UINT8_C(1);
        }
    } else {
        if (context->cycle == 0U) context->cycle = 1U;
        probability = (uint16_t)(probability - (probability >> cwr) -
                                 (probability >> (cwr + 2U)));
    }
    context->lg_pmps = probability;
}

/*
 * GB/T 20090.16-2016 Annex E.3-E.5 share this interval subdivision and
 * renormalization. Context selection and updating are handled by callers.
 */
static cavs_result decode_probability(cavs_ae_decoder *decoder,
                                      uint8_t predicted_mps,
                                      uint16_t lg_pmps, uint8_t *bin) {
    uint16_t range_s2;
    uint16_t range_t2;
    uint16_t lps_range;
    uint16_t next_value;
    int split_wrap;
    int is_lps;
    if (decoder == NULL || bin == NULL || predicted_mps > 1U ||
        lg_pmps > UINT16_C(1023))
        return CAVS_ERR_INVALID_ARGUMENT;

    split_wrap = decoder->range_t < (lg_pmps >> 2U);
    range_s2 = (uint16_t)(decoder->range_s + (split_wrap ? 1U : 0U));
    range_t2 = split_wrap ?
        (uint16_t)(UINT16_C(256) + decoder->range_t - (lg_pmps >> 2U)) :
        (uint16_t)(decoder->range_t - (lg_pmps >> 2U));
    is_lps = (range_s2 > decoder->value_s ||
             (range_s2 == decoder->value_s && decoder->value_t >= range_t2));
    if (!is_lps) {
        *bin = predicted_mps;
        decoder->range_s = range_s2;
        decoder->range_t = range_t2;
    } else {
        *bin = (uint8_t)!predicted_mps;
        lps_range = split_wrap ?
            (uint16_t)(decoder->range_t + (lg_pmps >> 2U)) :
            (uint16_t)(lg_pmps >> 2U);
        if (range_s2 == decoder->value_s) {
            decoder->value_t = (uint16_t)(decoder->value_t - range_t2);
        } else {
            next_value = decoder->value_t;
            if (!shift_in(decoder, &next_value))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            decoder->value_t = (uint16_t)(UINT16_C(256) + next_value - range_t2);
        }
        while (lps_range < UINT16_C(256)) {
            lps_range <<= 1U;
            if (!shift_in(decoder, &decoder->value_t))
                return CAVS_ERR_CORRUPT_BITSTREAM;
        }
        decoder->range_s = 0U;
        decoder->range_t = lps_range & UINT16_C(255);
    }

    if (is_lps) {
        decoder->range_s = 0U;
        decoder->value_s = 0U;
        while (decoder->value_t < UINT16_C(256)) {
            if (!shift_in(decoder, &decoder->value_t))
                return CAVS_ERR_CORRUPT_BITSTREAM;
            ++decoder->value_s;
        }
        decoder->value_t &= UINT16_C(255);
    }
    return CAVS_OK;
}

cavs_result cavs_ae_decode(cavs_ae_decoder *decoder,
                           cavs_ae_context *context, uint8_t *bin) {
    cavs_result result;
    uint8_t parsed;
    if (context == NULL || bin == NULL || context->mps > 1U ||
        context->cycle > 3U || context->lg_pmps > UINT16_C(1023))
        return CAVS_ERR_INVALID_ARGUMENT;
    result = decode_probability(decoder, context->mps, context->lg_pmps,
                                &parsed);
    if (result != CAVS_OK) return result;
    update_context(context, parsed);
    *bin = parsed;
    return CAVS_OK;
}

cavs_result cavs_ae_decode_weighted(cavs_ae_decoder *decoder,
                                    cavs_ae_context *first,
                                    cavs_ae_context *second, uint8_t *bin) {
    uint8_t predicted;
    uint8_t parsed;
    uint16_t probability;
    cavs_result result;
    if (first == NULL || second == NULL || bin == NULL ||
        first->mps > 1U || second->mps > 1U || first->cycle > 3U ||
        second->cycle > 3U || first->lg_pmps > UINT16_C(1023) ||
        second->lg_pmps > UINT16_C(1023))
        return CAVS_ERR_INVALID_ARGUMENT;
    if (first->mps == second->mps) {
        predicted = first->mps;
        probability = (uint16_t)((first->lg_pmps + second->lg_pmps) / 2U);
    } else if (first->lg_pmps < second->lg_pmps) {
        predicted = first->mps;
        probability = (uint16_t)(UINT16_C(1023) -
            ((second->lg_pmps - first->lg_pmps) >> 1U));
    } else {
        predicted = second->mps;
        probability = (uint16_t)(UINT16_C(1023) -
            ((first->lg_pmps - second->lg_pmps) >> 1U));
    }
    result = decode_probability(decoder, predicted, probability, &parsed);
    if (result != CAVS_OK) return result;
    update_context(first, parsed);
    update_context(second, parsed);
    *bin = parsed;
    return CAVS_OK;
}

cavs_result cavs_ae_decode_bypass(cavs_ae_decoder *decoder, uint8_t *bin) {
    return decode_probability(decoder, 0U, UINT16_C(1023), bin);
}

cavs_result cavs_ae_decode_stuffing(cavs_ae_decoder *decoder, uint8_t *bin) {
    return decode_probability(decoder, 0U, UINT16_C(4), bin);
}

size_t cavs_ae_bit_offset(const cavs_ae_decoder *decoder) {
    return decoder == NULL ? 0U : decoder->bit_offset;
}
