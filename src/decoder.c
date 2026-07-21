#include <cavs/cavs.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct cavs_frame_storage {
    cavs_frame public_frame;
    unsigned references;
    cavs_free_fn free;
    void *allocator_opaque;
} cavs_frame_storage;

struct cavs_decoder {
    cavs_decoder_config config;
    int flushing;
    int end_pending;
};

static void *default_alloc(void *opaque, size_t size) { (void)opaque; return malloc(size); }
static void default_free(void *opaque, void *ptr) { (void)opaque; free(ptr); }

cavs_result cavs_decoder_create(const cavs_decoder_config *config, cavs_decoder **out) {
    cavs_decoder_config cfg;
    cavs_decoder *decoder;
    if (out == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    memset(&cfg, 0, sizeof(cfg));
    if (config != NULL) cfg = *config;
    if ((cfg.alloc == NULL) != (cfg.free == NULL)) return CAVS_ERR_INVALID_ARGUMENT;
    if (cfg.alloc == NULL) { cfg.alloc = default_alloc; cfg.free = default_free; }
    decoder = (cavs_decoder *)cfg.alloc(cfg.allocator_opaque, sizeof(*decoder));
    if (decoder == NULL) return CAVS_ERR_OUT_OF_MEMORY;
    memset(decoder, 0, sizeof(*decoder)); decoder->config = cfg; *out = decoder;
    return CAVS_OK;
}

cavs_result cavs_decoder_send_nal(cavs_decoder *decoder, const cavs_packet *packet) {
    if (decoder == NULL || packet == NULL || packet->data == NULL || packet->size < 4U) return CAVS_ERR_INVALID_ARGUMENT;
    if (decoder->flushing) return CAVS_ERR_INVALID_STATE;
    if (packet->data[0] != 0U || packet->data[1] != 0U || packet->data[2] != 1U) return CAVS_ERR_CORRUPT_BITSTREAM;
    return CAVS_ERR_UNSUPPORTED_PROFILE;
}

cavs_result cavs_decoder_receive_event(cavs_decoder *decoder, cavs_event *event) {
    if (decoder == NULL || event == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    memset(event, 0, sizeof(*event));
    if (decoder->end_pending) { decoder->end_pending = 0; event->type = CAVS_EVENT_END; return CAVS_OK; }
    return decoder->flushing ? CAVS_EOF : CAVS_AGAIN;
}

cavs_result cavs_decoder_flush(cavs_decoder *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    if (!decoder->flushing) { decoder->flushing = 1; decoder->end_pending = 1; }
    return CAVS_OK;
}

cavs_result cavs_decoder_reset(cavs_decoder *decoder) {
    if (decoder == NULL) return CAVS_ERR_INVALID_ARGUMENT;
    decoder->flushing = 0; decoder->end_pending = 0; return CAVS_OK;
}

void cavs_decoder_destroy(cavs_decoder *decoder) {
    if (decoder != NULL) decoder->config.free(decoder->config.allocator_opaque, decoder);
}

cavs_frame *cavs_frame_ref(cavs_frame *frame) {
    cavs_frame_storage *storage;
    if (frame == NULL) return NULL;
    storage = (cavs_frame_storage *)(void *)frame;
    if (storage->references == UINT_MAX) return NULL;
    ++storage->references;
    return frame;
}

void cavs_frame_unref(cavs_frame **frame) {
    cavs_frame_storage *storage;
    if (frame == NULL || *frame == NULL) return;
    storage = (cavs_frame_storage *)(void *)*frame;
    *frame = NULL;
    if (--storage->references == 0U) storage->free(storage->allocator_opaque, storage);
}
const char *cavs_version(void) { return "0.1.0-dev"; }

const char *cavs_strerror(cavs_result result) {
    switch (result) {
    case CAVS_OK: return "success"; case CAVS_AGAIN: return "try again"; case CAVS_EOF: return "end of stream";
    case CAVS_ERR_INVALID_ARGUMENT: return "invalid argument"; case CAVS_ERR_OUT_OF_MEMORY: return "out of memory";
    case CAVS_ERR_CORRUPT_BITSTREAM: return "corrupt bitstream"; case CAVS_ERR_UNSUPPORTED_PROFILE: return "unsupported profile";
    case CAVS_ERR_UNSUPPORTED_LEVEL: return "unsupported level"; case CAVS_ERR_INVALID_STATE: return "invalid state";
    case CAVS_ERR_MISSING_REFERENCE: return "missing reference picture"; default: return "unknown error";
    }
}
