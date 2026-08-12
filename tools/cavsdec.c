/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Annex-B input and planar-frame output driver for the public decoder API.
 */
#include <cavs/cavs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

static int is_start_code(const unsigned char *data, size_t size, size_t offset) {
    return size >= 4U && offset <= size - 4U && data[offset] == 0U && data[offset + 1U] == 0U &&
           data[offset + 2U] == 1U;
}

static size_t next_start_code(const unsigned char *data, size_t size, size_t offset) {
    size_t cursor;
    if (size < 4U || offset > size - 4U) return size;
    for (cursor = offset; cursor <= size - 4U; ++cursor) {
        if (is_start_code(data, size, cursor)) return cursor;
    }
    return size;
}

static int read_input(FILE *input, unsigned char **data, size_t *size) {
    size_t capacity = 64U * 1024U;
    unsigned char *buffer = (unsigned char *)malloc(capacity);
    size_t used = 0U;
    if (buffer == NULL) return 0;
    for (;;) {
        size_t count = fread(buffer + used, 1U, capacity - used, input);
        used += count;
        if (used != capacity) {
            if (ferror(input)) { free(buffer); return 0; }
            break;
        }
        if (capacity > SIZE_MAX / 2U) { free(buffer); return 0; }
        capacity *= 2U;
        {
            unsigned char *grown = (unsigned char *)realloc(buffer, capacity);
            if (grown == NULL) { free(buffer); return 0; }
            buffer = grown;
        }
    }
    *data = buffer;
    *size = used;
    return 1;
}

static void set_binary_mode(FILE *stream) {
#if defined(_WIN32)
    (void)_setmode(_fileno(stream), _O_BINARY);
#else
    (void)stream;
#endif
}

static unsigned long parse_limit(const char *text, int *ok) {
    char *end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    *ok = errno == 0 && text[0] != '-' && end != text && *end == '\0';
    return value;
}

typedef struct tool_state {
    FILE *output;
    unsigned long frame_limit;
    unsigned long frames;
    unsigned long sequences;
    cavs_log_level log_level;
} tool_state;

static void print_usage(FILE *stream) {
    fprintf(stream, "usage: cavsdec [--frames N] [--log LEVEL] [--output FILE] input.avs\n");
}

static int parse_log_level(const char *text, cavs_log_level *level) {
    if (strcmp(text, "error") == 0) *level = CAVS_LOG_ERROR;
    else if (strcmp(text, "warning") == 0) *level = CAVS_LOG_WARNING;
    else if (strcmp(text, "info") == 0) *level = CAVS_LOG_INFO;
    else if (strcmp(text, "debug") == 0) *level = CAVS_LOG_DEBUG;
    else return 0;
    return 1;
}

static void log_message(void *opaque, cavs_log_level level, const char *message) {
    const tool_state *state = (const tool_state *)opaque;
    static const char *const names[] = { "error", "warning", "info", "debug" };
    if (level <= state->log_level)
        fprintf(stderr, "libcavs %s: %s\n", names[(unsigned)level], message);
}

static int write_plane(FILE *output, const uint8_t *plane, ptrdiff_t stride,
                       uint32_t width, uint32_t height) {
    uint32_t row;
    if (plane == NULL || stride < (ptrdiff_t)width) return 0;
    for (row = 0U; row < height; ++row) {
        if (fwrite(plane, 1U, width, output) != width) return 0;
        plane += stride;
    }
    return 1;
}

static int write_frame(FILE *output, const cavs_frame *frame) {
    if (frame == NULL) return 0;
    uint32_t chroma_width = (frame->coded_width + 1U) / 2U;
    uint32_t chroma_height = frame->format == CAVS_YUV420P8 ?
                             (frame->coded_height + 1U) / 2U : frame->coded_height;
    return write_plane(output, frame->plane[0], frame->stride[0],
                       frame->coded_width, frame->coded_height) &&
           write_plane(output, frame->plane[1], frame->stride[1],
                       chroma_width, chroma_height) &&
           write_plane(output, frame->plane[2], frame->stride[2],
                       chroma_width, chroma_height);
}

static int drain_events(cavs_decoder *decoder, tool_state *state) {
    cavs_event event;
    cavs_result result;
    while ((result = cavs_decoder_receive_event(decoder, &event)) == CAVS_OK) {
        if (event.type == CAVS_EVENT_SEQUENCE) {
            ++state->sequences;
            fprintf(stderr, "sequence: profile=0x%02x level=0x%02x %ux%u %s%s\n",
                    event.sequence.profile_id, event.sequence.level_id,
                    event.sequence.display_width, event.sequence.display_height,
                    event.sequence.format == CAVS_YUV422P8 ? "yuv422p8" : "yuv420p8",
                    event.sequence.progressive_sequence != 0U ? " progressive" : " interlaced");
        } else if (event.type == CAVS_EVENT_FRAME) {
            int written = state->output == NULL || write_frame(state->output, event.frame);
            cavs_frame_unref(&event.frame);
            if (!written) {
                fprintf(stderr, "cannot write decoded frame\n");
                return 1;
            }
            ++state->frames;
            if (state->frame_limit != 0U && state->frames >= state->frame_limit)
                return 2;
        }
    }
    return result == CAVS_AGAIN || result == CAVS_EOF ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    const char *output_path = NULL;
    unsigned long pictures = 0U, units = 0U, skipped = 0U;
    unsigned char *data = NULL;
    FILE *input;
    size_t size, start, next;
    int index, ok, has_sequence = 0, failed = 0;
    cavs_decoder *decoder = NULL;
    cavs_decoder_config config;
    tool_state state;

    memset(&state, 0, sizeof(state));
    state.log_level = CAVS_LOG_WARNING;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            state.frame_limit = parse_limit(argv[++index], &ok);
            if (!ok) { fprintf(stderr, "invalid --frames value\n"); return 2; }
        } else if (strcmp(argv[index], "--log") == 0 && index + 1 < argc) {
            if (!parse_log_level(argv[++index], &state.log_level)) {
                fprintf(stderr, "invalid --log level\n");
                return 2;
            }
        } else if (strcmp(argv[index], "--output") == 0 && index + 1 < argc) {
            output_path = argv[++index];
        } else if (strcmp(argv[index], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (argv[index][0] == '-' && strcmp(argv[index], "-") != 0) {
            print_usage(stderr);
            return 2;
        } else if (path == NULL) {
            path = argv[index];
        } else {
            fprintf(stderr, "only one input file is supported\n");
            return 2;
        }
    }
    if (path == NULL) { print_usage(stderr); return 2; }
    input = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (input == stdin) set_binary_mode(stdin);
    if (input == NULL || !read_input(input, &data, &size)) {
        fprintf(stderr, "cannot read %s\n", path);
        if (input != NULL && input != stdin) fclose(input);
        return 1;
    }
    if (input != stdin) fclose(input);
    if (output_path != NULL) {
        state.output = strcmp(output_path, "-") == 0 ? stdout : fopen(output_path, "wb");
        if (state.output == stdout) set_binary_mode(stdout);
        if (state.output == NULL) {
            fprintf(stderr, "cannot open output %s\n", output_path);
            free(data);
            return 1;
        }
    }
    memset(&config, 0, sizeof(config));
    config.log = log_message;
    config.log_opaque = &state;
    if (cavs_decoder_create(&config, &decoder) != CAVS_OK) {
        if (state.output != NULL && state.output != stdout) fclose(state.output);
        free(data);
        return 1;
    }

    start = next_start_code(data, size, 0U);
    while (start < size) {
        cavs_packet packet;
        cavs_result result;
        next = next_start_code(data, size, start + 4U);
        packet.data = data + start; packet.size = next - start;
        packet.pts = (int64_t)pictures; packet.dts = (int64_t)pictures; packet.opaque = NULL;
        result = cavs_decoder_send_nal(decoder, &packet);
        if (result == CAVS_OK) {
            ++units;
            if (packet.data[3] == 0xb0U) has_sequence = 1;
            if (packet.data[3] == 0xb3U || packet.data[3] == 0xb6U) ++pictures;
            int drain_result = drain_events(decoder, &state);
            if (drain_result == 2) break;
            if (drain_result != 0) {
                fprintf(stderr, "event error\n");
                failed = 1;
                break;
            }
        } else if (!has_sequence && result == CAVS_ERR_INVALID_STATE) {
            ++skipped;
        } else if (result == CAVS_AGAIN) {
            int drain_result = drain_events(decoder, &state);
            if (drain_result == 2) break;
            if (drain_result != 0) {
                failed = 1;
                break;
            }
            start = next;
            continue;
        } else {
            fprintf(stderr, "unit %lu (start code 0x%02x): %s\n", units + skipped + 1U,
                    packet.data[3], cavs_strerror(result));
            failed = 1;
            /*
             * A terminal truncated NAL has already been rejected by the
             * public API. Still reach EOF flush so a partial current picture
             * is discarded and previously completed delayed pictures drain.
             * A bad unit before another start code remains a hard stop.
             */
            if (next != size) {
                cavs_decoder_destroy(decoder);
                if (state.output != NULL && state.output != stdout)
                    fclose(state.output);
                free(data);
                return 1;
            }
            break;
        }
        start = next;
    }
    if (state.frame_limit == 0U || state.frames < state.frame_limit) {
        int drain_result;
        if (cavs_decoder_flush(decoder) != CAVS_OK) failed = 1;
        drain_result = drain_events(decoder, &state);
        if (drain_result == 1) failed = 1;
    }
    fprintf(stderr, "parsed units=%lu picture-headers=%lu sequences=%lu frames=%lu skipped-before-sequence=%lu\n",
            units, pictures, state.sequences, state.frames, skipped);
    cavs_decoder_destroy(decoder);
    if (state.output != NULL && state.output != stdout && fclose(state.output) != 0) {
        fprintf(stderr, "cannot close output %s\n", output_path);
        free(data);
        return 1;
    }
    free(data);
    return failed ? 1 : 0;
}
