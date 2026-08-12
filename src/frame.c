/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "frame.h"
#include "safe.h"
#include <limits.h>
#include <string.h>

typedef struct cavs_frame_storage {
    cavs_frame public_frame;
    unsigned references;
    cavs_free_fn free;
    void *allocator_opaque;
    uint8_t *buffer;
    uint8_t *planes[3];
    cavs_picture picture;
    void *macroblocks;
    uint8_t filtered_fields;
} cavs_frame_storage;

static cavs_frame_storage *frame_storage(cavs_frame *frame) {
    return (cavs_frame_storage *)(void *)frame;
}

static const cavs_frame_storage *const_frame_storage(const cavs_frame *frame) {
    return (const cavs_frame_storage *)(const void *)frame;
}

static cavs_frame_storage *picture_storage(cavs_picture *picture) {
    return picture == NULL ? NULL : (cavs_frame_storage *)picture->owner;
}

cavs_result cavs_frame_allocate(
    const cavs_decoder_config *config, const cavs_frame_parameters *parameters,
    cavs_frame **frame) {
    cavs_frame_storage *storage;
    uint32_t coded_width;
    uint32_t coded_height;
    uint32_t macroblock_height;
    size_t luma_size;
    size_t chroma_size;
    size_t total_size;
    size_t chroma_width;
    size_t chroma_height;
    size_t macroblock_count;
    size_t macroblock_bytes;
    const cavs_sequence_info *sequence;
    if (config == NULL || parameters == NULL || frame == NULL ||
        parameters->sequence == NULL || parameters->macroblock_size == 0U)
        return CAVS_ERR_INVALID_ARGUMENT;
    sequence = parameters->sequence;
    if (sequence->display_width > UINT32_MAX - 15U ||
        sequence->display_height > UINT32_MAX - 31U)
        return CAVS_ERR_INVALID_ARGUMENT;
    coded_width = (sequence->display_width + 15U) & ~UINT32_C(15);
    coded_height = sequence->progressive_sequence != 0U ?
        (sequence->display_height + 15U) & ~UINT32_C(15) :
        (sequence->display_height + 31U) & ~UINT32_C(31);
    chroma_width = coded_width / 2U;
    chroma_height = coded_height / 2U;
    if (!cavs_size_mul((size_t)coded_width, (size_t)coded_height,
                       &luma_size) ||
        !cavs_size_mul(chroma_width, chroma_height, &chroma_size) ||
        !cavs_size_add(luma_size, chroma_size, &total_size) ||
        !cavs_size_add(total_size, chroma_size, &total_size) ||
        luma_size > (size_t)PTRDIFF_MAX ||
        chroma_width > (size_t)PTRDIFF_MAX)
        return CAVS_ERR_OUT_OF_MEMORY;
    macroblock_height = sequence->progressive_sequence != 0U ?
        coded_height / 16U : 2U * (coded_height / 32U);
    if (!cavs_size_mul((size_t)(coded_width / 16U),
                       (size_t)macroblock_height, &macroblock_count) ||
        !cavs_size_mul(macroblock_count, parameters->macroblock_size,
                       &macroblock_bytes))
        return CAVS_ERR_OUT_OF_MEMORY;
    storage = (cavs_frame_storage *)config->alloc(
        config->allocator_opaque, sizeof(*storage));
    if (storage == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(storage, 0, sizeof(*storage));
    storage->buffer = (uint8_t *)config->alloc(
        config->allocator_opaque, total_size);
    if (storage->buffer == NULL) {
        config->free(config->allocator_opaque, storage);
        return CAVS_ERR_OUT_OF_MEMORY;
    }
    storage->macroblocks = config->alloc(
        config->allocator_opaque, macroblock_bytes);
    if (storage->macroblocks == NULL) {
        config->free(config->allocator_opaque, storage->buffer);
        config->free(config->allocator_opaque, storage);
        return CAVS_ERR_OUT_OF_MEMORY;
    }
    storage->free = config->free;
    storage->allocator_opaque = config->allocator_opaque;
    storage->references = 1U;
    storage->planes[0] = storage->buffer;
    storage->planes[1] = storage->planes[0] + luma_size;
    storage->planes[2] = storage->planes[1] + chroma_size;
    memset(storage->buffer, 128, total_size);
    memset(storage->macroblocks, 0, macroblock_bytes);

    storage->public_frame.plane[0] = storage->planes[0];
    storage->public_frame.plane[1] = storage->planes[1];
    storage->public_frame.plane[2] = storage->planes[2];
    storage->public_frame.stride[0] = (ptrdiff_t)coded_width;
    storage->public_frame.stride[1] = (ptrdiff_t)chroma_width;
    storage->public_frame.stride[2] = (ptrdiff_t)chroma_width;
    storage->public_frame.coded_width = coded_width;
    storage->public_frame.coded_height = coded_height;
    storage->public_frame.display_width = sequence->display_width;
    storage->public_frame.display_height = sequence->display_height;
    storage->public_frame.format = CAVS_YUV420P8;
    storage->public_frame.picture_type = parameters->picture_type;
    storage->public_frame.pts = parameters->pts;
    storage->public_frame.dts = parameters->dts;
    storage->public_frame.top_field_first =
        (unsigned)(parameters->top_field_first != 0U);
    storage->public_frame.repeat_first_field =
        (unsigned)(parameters->repeat_first_field != 0U);
    storage->public_frame.field_picture =
        (unsigned)(parameters->picture_structure == 0U);

    storage->picture.plane[0] = storage->planes[0];
    storage->picture.plane[1] = storage->planes[1];
    storage->picture.plane[2] = storage->planes[2];
    storage->picture.stride[0] = (ptrdiff_t)coded_width;
    storage->picture.stride[1] = (ptrdiff_t)chroma_width;
    storage->picture.stride[2] = (ptrdiff_t)chroma_width;
    storage->picture.coded_width = coded_width;
    storage->picture.coded_height = coded_height;
    storage->picture.display_width = sequence->display_width;
    storage->picture.display_height = sequence->display_height;
    storage->picture.format = CAVS_YUV420P8;
    storage->picture.picture_type = parameters->picture_type;
    storage->picture.picture_distance = parameters->picture_distance;
    storage->picture.pts = parameters->pts;
    storage->picture.dts = parameters->dts;
    storage->picture.macroblock_width = (uint16_t)(coded_width / 16U);
    storage->picture.macroblock_height = (uint16_t)macroblock_height;
    storage->picture.macroblocks = storage->macroblocks;
    storage->picture.macroblock_count = macroblock_count;
    storage->picture.top_field_first = parameters->top_field_first;
    storage->picture.repeat_first_field = parameters->repeat_first_field;
    storage->picture.field_picture =
        (uint8_t)(parameters->picture_structure == 0U);
    storage->picture.is_reference =
        (uint8_t)(parameters->picture_type != CAVS_PICTURE_B);
    storage->picture.owner = storage;
    *frame = &storage->public_frame;
    return CAVS_OK;
}

void cavs_frame_release(cavs_frame **frame) {
    cavs_frame_storage *storage;
    cavs_free_fn free_fn;
    void *opaque;
    if (frame == NULL || *frame == NULL) return;
    storage = frame_storage(*frame);
    free_fn = storage->free;
    opaque = storage->allocator_opaque;
    *frame = NULL;
    if (storage->references == 0U) return;
    --storage->references;
    if (storage->references == 0U) {
        if (storage->macroblocks != NULL)
            free_fn(opaque, storage->macroblocks);
        if (storage->buffer != NULL) free_fn(opaque, storage->buffer);
        free_fn(opaque, storage);
    }
}

cavs_picture *cavs_frame_picture(cavs_frame *frame) {
    return frame == NULL ? NULL : &frame_storage(frame)->picture;
}

cavs_frame *cavs_picture_frame(cavs_picture *picture) {
    cavs_frame_storage *storage = picture_storage(picture);
    return storage == NULL ? NULL : &storage->public_frame;
}

void *cavs_frame_plane(cavs_frame *frame, unsigned plane) {
    if (frame == NULL || plane >= 3U) return NULL;
    return frame_storage(frame)->planes[plane];
}

uint8_t cavs_frame_filtered_fields(const cavs_frame *frame) {
    return frame == NULL ? 0U : const_frame_storage(frame)->filtered_fields;
}

void cavs_frame_mark_filtered(cavs_frame *frame, uint8_t field) {
    if (frame != NULL) frame_storage(frame)->filtered_fields |= field;
}

void cavs_frame_retain_picture(void *opaque, cavs_picture *picture) {
    cavs_frame_storage *storage = picture_storage(picture);
    (void)opaque;
    if (storage != NULL && storage->references != UINT_MAX)
        ++storage->references;
}

void cavs_frame_release_picture(void *opaque, cavs_picture *picture) {
    cavs_frame *frame = cavs_picture_frame(picture);
    (void)opaque;
    cavs_frame_release(&frame);
}

cavs_frame *cavs_frame_ref(cavs_frame *frame) {
    cavs_frame_storage *storage;
    if (frame == NULL) return NULL;
    storage = frame_storage(frame);
    if (storage->references == UINT_MAX) return NULL;
    ++storage->references;
    return frame;
}

void cavs_frame_unref(cavs_frame **frame) {
    cavs_frame_release(frame);
}

#ifdef CAVS_TESTING
unsigned cavs_frame_test_reference_count(const cavs_frame *frame) {
    return frame == NULL ? 0U : const_frame_storage(frame)->references;
}
#endif
