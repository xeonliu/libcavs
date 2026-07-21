/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Public API for the independent libcavs decoder implementation.
 */
#ifndef CAVS_CAVS_H
#define CAVS_CAVS_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(CAVS_BUILDING_DLL)
#define CAVS_API __declspec(dllexport)
#elif defined(_WIN32)
#define CAVS_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define CAVS_API __attribute__((visibility("default")))
#else
#define CAVS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cavs_decoder cavs_decoder;
typedef struct cavs_frame cavs_frame;

typedef enum cavs_result {
    CAVS_OK = 0,
    CAVS_AGAIN = 1,
    CAVS_EOF = 2,
    CAVS_ERR_INVALID_ARGUMENT = -1,
    CAVS_ERR_OUT_OF_MEMORY = -2,
    CAVS_ERR_CORRUPT_BITSTREAM = -3,
    CAVS_ERR_UNSUPPORTED_PROFILE = -4,
    CAVS_ERR_UNSUPPORTED_LEVEL = -5,
    CAVS_ERR_INVALID_STATE = -6,
    CAVS_ERR_MISSING_REFERENCE = -7
} cavs_result;

typedef enum cavs_pixel_format { CAVS_YUV420P8 = 0, CAVS_YUV422P8 = 1 } cavs_pixel_format;
typedef enum cavs_picture_type { CAVS_PICTURE_I = 0, CAVS_PICTURE_P = 1, CAVS_PICTURE_B = 2 } cavs_picture_type;
typedef enum cavs_event_type {
    CAVS_EVENT_SEQUENCE = 0,
    CAVS_EVENT_FRAME = 1,
    CAVS_EVENT_METADATA = 2,
    CAVS_EVENT_RAW_EXTENSION = 3,
    CAVS_EVENT_END = 4
} cavs_event_type;
typedef enum cavs_log_level { CAVS_LOG_ERROR = 0, CAVS_LOG_WARNING = 1, CAVS_LOG_INFO = 2, CAVS_LOG_DEBUG = 3 } cavs_log_level;

typedef void *(*cavs_alloc_fn)(void *opaque, size_t size);
typedef void (*cavs_free_fn)(void *opaque, void *ptr);
typedef void (*cavs_log_fn)(void *opaque, cavs_log_level level, const char *message);

typedef struct cavs_decoder_config {
    cavs_alloc_fn alloc;
    cavs_free_fn free;
    void *allocator_opaque;
    cavs_log_fn log;
    void *log_opaque;
} cavs_decoder_config;

typedef struct cavs_packet {
    const uint8_t *data;
    size_t size;
    int64_t pts;
    int64_t dts;
    void *opaque;
} cavs_packet;

/** Parameters established by the current video sequence header. */
typedef struct cavs_sequence_info {
    uint8_t profile_id;
    uint8_t level_id;
    uint8_t progressive_sequence;
    uint32_t display_width;
    uint32_t display_height;
    cavs_pixel_format format;
    uint8_t aspect_ratio_code;
    uint8_t frame_rate_code;
    uint64_t bit_rate;
    uint8_t low_delay;
    uint64_t bbv_buffer_size_bits;
} cavs_sequence_info;

struct cavs_frame {
    const uint8_t *plane[3];
    ptrdiff_t stride[3];
    uint32_t coded_width, coded_height;
    uint32_t display_width, display_height;
    cavs_pixel_format format;
    cavs_picture_type picture_type;
    int64_t pts, dts;
    unsigned top_field_first : 1;
    unsigned repeat_first_field : 1;
    unsigned field_picture : 1;
};

typedef struct cavs_event {
    cavs_event_type type;
    cavs_frame *frame;
    cavs_sequence_info sequence;
    const uint8_t *data;
    size_t size;
} cavs_event;

/** Creates a decoder using optional caller-provided allocation and logging. */
CAVS_API cavs_result cavs_decoder_create(const cavs_decoder_config *config, cavs_decoder **decoder);

/** Submits one complete Annex-B unit, including its 00 00 01 xx start code. */
CAVS_API cavs_result cavs_decoder_send_nal(cavs_decoder *decoder, const cavs_packet *packet);

/** Receives the next queued event without blocking. */
CAVS_API cavs_result cavs_decoder_receive_event(cavs_decoder *decoder, cavs_event *event);

/** Signals end of input and starts draining delayed frames and the end event. */
CAVS_API cavs_result cavs_decoder_flush(cavs_decoder *decoder);

/** Clears stream state so that the instance can accept a new sequence. */
CAVS_API cavs_result cavs_decoder_reset(cavs_decoder *decoder);

/** Releases a decoder and every resource still owned by it. */
CAVS_API void cavs_decoder_destroy(cavs_decoder *decoder);

/** Acquires another reference to a library-owned output frame. */
CAVS_API cavs_frame *cavs_frame_ref(cavs_frame *frame);

/** Releases a frame reference and clears the caller's pointer. */
CAVS_API void cavs_frame_unref(cavs_frame **frame);

/** Returns the library version as a stable, process-lifetime string. */
CAVS_API const char *cavs_version(void);

/** Returns a process-lifetime English description of a result code. */
CAVS_API const char *cavs_strerror(cavs_result result);

#ifdef __cplusplus
}
#endif
#endif
