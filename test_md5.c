/*
 * test_md5.c – Decode sample/CCTV-9.avs and verify the raw YUV output
 * produces the expected MD5 checksum.
 *
 * Expected MD5: 8fd8d5c4b9237f69cc45ff289d0c46b0
 *
 * Usage: test_md5 <input.avs>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#  include <CommonCrypto/CommonDigest.h>
#  define MD5_CTX_T   CC_MD5_CTX
#  define MD5_INIT(c) CC_MD5_Init(c)
#  define MD5_UPDATE(c,d,l) CC_MD5_Update(c,d,(CC_LONG)(l))
#  define MD5_FINAL(d,c)    CC_MD5_Final(d,c)
#  define MD5_DIGEST_LEN    CC_MD5_DIGEST_LENGTH
#else
#  include <openssl/md5.h>
#  define MD5_CTX_T   MD5_CTX
#  define MD5_INIT(c) MD5_Init(c)
#  define MD5_UPDATE(c,d,l) MD5_Update(c,d,l)
#  define MD5_FINAL(d,c)    MD5_Final(d,c)
#  define MD5_DIGEST_LEN    MD5_DIGEST_LENGTH
#endif

#include "libcavs.h"

#define BUF_SIZE 4000000

/* Expected MD5 of the decoded YUV for sample/CCTV-9.avs */
static const char *EXPECTED_MD5 = "8fd8d5c4b9237f69cc45ff289d0c46b0";

/* ------------------------------------------------------------------ */
static uint8_t *find_start_code(uint8_t *p, uint8_t *end, uint32_t *sc)
{
    for (; p < end - 3; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            *sc = 0x00000100u | p[3];
            return p;
        }
    }
    return NULL;
}

/* Feed one YUV frame into the running MD5 context */
static void md5_yuv(MD5_CTX_T *ctx, cavs_param *param)
{
    int w = param->seqsize.lWidth;
    int h = param->seqsize.lHeight;
    int i;

    for (i = 0; i < h; i++)
        MD5_UPDATE(ctx, param->p_out_yuv[0] + i * w, w);
    for (i = 0; i < h / 2; i++)
        MD5_UPDATE(ctx, param->p_out_yuv[1] + i * (w / 2), w / 2);
    for (i = 0; i < h / 2; i++)
        MD5_UPDATE(ctx, param->p_out_yuv[2] + i * (w / 2), w / 2);
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.avs>\n", argv[0]);
        return 2;
    }

    /* ---- load input ---- */
    FILE *fin = fopen(argv[1], "rb");
    if (!fin) { perror("fopen input"); return 2; }
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *buffer = malloc(file_size);
    if (!buffer) { perror("malloc"); fclose(fin); return 2; }
    fread(buffer, 1, file_size, fin);
    fclose(fin);

    /* ---- decoder setup (mirrors main.c exactly) ---- */
    void *decoder = NULL;
    cavs_param param;
    memset(&param, 0, sizeof(cavs_param));
    param.b_accelerate = 1;
    param.i_thread_num = 64;
    param.output_type  = -1;

    if (cavs_decoder_create(&decoder, &param) != 0) {
        fprintf(stderr, "cavs_decoder_create failed\n");
        free(buffer);
        return 2;
    }
    cavs_decoder_thread_param_init(decoder);

    uint8_t *end = buffer + file_size;
    uint8_t nal_buf[BUF_SIZE];

    /* ---- probe pass ---- */
    {
        int b_seq = 0, b_done = 0;
        uint32_t pc;
        uint8_t *ps = find_start_code(buffer, end, &pc);
        while (ps && !b_done) {
            long rem = end - ps;
            int plen;
            if (pc == CAVS_VIDEO_SEQUENCE_START_CODE) {
                cavs_decoder_init_stream(decoder, ps, rem);
                cavs_decoder_get_one_nal(decoder, nal_buf, &plen);
                cavs_decoder_probe_seq(decoder, nal_buf, plen);
                b_seq = 1;
            } else if ((pc == CAVS_EXTENSION_START_CODE || pc == CAVS_USER_DATA_CODE) && b_seq) {
                cavs_decoder_init_stream(decoder, ps, rem);
                cavs_decoder_get_one_nal(decoder, nal_buf, &plen);
                cavs_decoder_probe_seq(decoder, nal_buf, plen);
            } else if (pc == CAVS_I_PICUTRE_START_CODE && b_seq) {
                cavs_decoder_init_stream(decoder, ps, rem);
                cavs_decoder_get_one_nal(decoder, nal_buf, &plen);
                int r = cavs_decoder_pic_header(decoder, nal_buf, plen, &param, pc);
                if (r != CAVS_ERROR) cavs_decoder_set_format_type(decoder, &param);
                b_done = 1;
            }
            uint32_t nc; uint8_t *ns = find_start_code(ps + 4, end, &nc);
            ps = ns; pc = nc;
        }
    }

    /* ---- decode loop ---- */
    MD5_CTX_T ctx;
    MD5_INIT(&ctx);

    int last_err = 0, got_key = 0, frame_count = 0;
    uint32_t current_code;
    uint8_t *frame_start = find_start_code(buffer, end, &current_code);

    while (frame_start) {
        long rem = end - frame_start;
        int len;

        if (current_code == CAVS_VIDEO_SEQUENCE_START_CODE) {
            cavs_decoder_init_stream(decoder, frame_start, rem);
            cavs_decoder_get_one_nal(decoder, nal_buf, &len);
            cavs_decoder_process(decoder, nal_buf, len);
            cavs_decoder_get_seq(decoder, &param.seqsize);
            param.i_thread_num = 64;
            param.output_type  = -1;
            if (param.seq_header_flag == 0) {
                cavs_decoder_seq_init(decoder, &param);
                if (cavs_decoder_buffer_init(&param) < 0) {
                    fprintf(stderr, "buffer init failed\n");
                    goto cleanup;
                }
                param.seq_header_flag = 1;
            } else if (param.b_accelerate && last_err) {
                cavs_decoder_seq_header_reset_pipeline(decoder);
            }
            if (last_err) last_err = 0;

        } else if (current_code == CAVS_EXTENSION_START_CODE ||
                   current_code == CAVS_USER_DATA_CODE) {
            cavs_decoder_init_stream(decoder, frame_start, rem);
            cavs_decoder_get_one_nal(decoder, nal_buf, &len);
            cavs_decoder_process(decoder, nal_buf, len);

        } else if (current_code == CAVS_I_PICUTRE_START_CODE ||
                   current_code == CAVS_PB_PICUTRE_START_CODE) {
            cavs_decoder_init_stream(decoder, frame_start, rem);
            cavs_decoder_get_one_nal(decoder, nal_buf, &len);

            if (current_code == CAVS_I_PICUTRE_START_CODE) {
                got_key = 1; last_err = 0;
            } else {
                if (last_err || !got_key) goto next_frame;
            }

            int ret = cavs_decode_one_frame(decoder, current_code, &param, nal_buf, len);
            if (ret == CAVS_ERROR) {
                last_err = 1; got_key = 0;
            } else if (ret == CAVS_FRAME_OUT) {
                md5_yuv(&ctx, &param);
                frame_count++;
            }
        }

    next_frame:;
        uint32_t nc;
        uint8_t *ns = find_start_code(frame_start + 4, end, &nc);
        frame_start = ns;
        current_code = nc;
    }

    /* ---- flush ---- */
    if (param.seq_header_flag) {
        int ret = cavs_decode_one_frame_delay(decoder, &param);
        if (ret == CAVS_FRAME_OUT) { md5_yuv(&ctx, &param); frame_count++; }

        if (cavs_out_delay_frame_end(decoder, param.p_out_yuv)) {
            md5_yuv(&ctx, &param); frame_count++;
        }
    }

    /* ---- compute final digest ---- */
    unsigned char digest[MD5_DIGEST_LEN];
    MD5_FINAL(digest, &ctx);

    char hex[MD5_DIGEST_LEN * 2 + 1];
    for (int i = 0; i < MD5_DIGEST_LEN; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);

    printf("Decoded frames : %d\n", frame_count);
    printf("Output MD5     : %s\n", hex);
    printf("Expected MD5   : %s\n", EXPECTED_MD5);

    int pass = (strcmp(hex, EXPECTED_MD5) == 0);
    printf("Result         : %s\n", pass ? "PASS" : "FAIL");

cleanup:
    cavs_decoder_destroy(decoder);
    cavs_decoder_buffer_end(&param);
    free(buffer);
    return pass ? 0 : 1;
}
