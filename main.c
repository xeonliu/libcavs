#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcavs.h"

#define BUF_SIZE 4000000

// Helper to find start code 0x000001XX
// Returns the pointer to the start of the start code sequence (00 00 01 XX)
// Updates *start_code to the XX value.
static uint8_t* find_start_code(uint8_t *p, uint8_t *end, uint32_t *start_code) {
    uint8_t *ptr = p;
    while (ptr < end - 3) {
        if (ptr[0] == 0 && ptr[1] == 0 && ptr[2] == 1) {
            *start_code = 0x00000100 | ptr[3];
            return ptr;
        }
        ptr++;
    }
    return NULL;
}

static void write_yuv(FILE *fp, cavs_param *param) {
    int width = param->seqsize.lWidth;
    int height = param->seqsize.lHeight;
    int i;

    // Y
    for (i = 0; i < height; i++) {
        fwrite(param->p_out_yuv[0] + i * width, 1, width, fp);
    }
    // U
    for (i = 0; i < height / 2; i++) {
        fwrite(param->p_out_yuv[1] + i * (width / 2), 1, width / 2, fp);
    }
    // V
    for (i = 0; i < height / 2; i++) {
        fwrite(param->p_out_yuv[2] + i * (width / 2), 1, width / 2, fp);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <input.avs> <output.yuv>\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];

    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        perror("Failed to open input file");
        return 1;
    }

    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        perror("Failed to open output file");
        fclose(fin);
        return 1;
    }

    // Read entire file into buffer
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) {
        perror("Failed to allocate buffer");
        fclose(fin);
        fclose(fout);
        return 1;
    }
    fread(buffer, 1, file_size, fin);
    fclose(fin);

    void *decoder = NULL;
    cavs_param param;
    memset(&param, 0, sizeof(cavs_param));

    // Initialize decoder parameters
    param.b_accelerate = 0; // Disable acceleration (single-threaded mode is more stable)
    param.i_thread_num = 1; // Use 1 thread

    int last_frame_error = 0;
    int got_keyframe = 0;

    if (cavs_decoder_create(&decoder, &param) != 0) {
        fprintf(stderr, "Failed to create decoder\n");
        free(buffer);
        fclose(fout);
        return 1;
    }

    cavs_decoder_thread_param_init(decoder);

    uint8_t *ptr = buffer;
    uint8_t *end = buffer + file_size;
    uint32_t current_code;
    uint8_t *frame_start = find_start_code(ptr, end, &current_code);
    
    int frame_count = 0;
    int input_frame_count = 0;
    unsigned char nal_buf[BUF_SIZE]; // Temporary buffer for NAL payload

    while (frame_start) {
        // Calculate remaining size from this start code to end of file
        // We pass the rest of the buffer to the decoder, similar to ref.c
        // libcavs should handle parsing slices until the next start code.
        long remaining_len = end - frame_start;
        
        // Process the unit
        if (current_code == CAVS_VIDEO_SEQUENCE_START_CODE) {
            printf("Found SEQ Header at offset %ld\n", frame_start - buffer);
            cavs_decoder_init_stream(decoder, frame_start, remaining_len);
            int len;
            cavs_decoder_get_one_nal(decoder, nal_buf, &len);
            
            cavs_decoder_process(decoder, nal_buf, len);
            cavs_decoder_get_seq(decoder, &param.seqsize);
            
            if (param.seq_header_flag == 0) {
                cavs_decoder_seq_init(decoder, &param);
                if (cavs_decoder_buffer_init(&param) < 0) {
                    fprintf(stderr, "Failed to init buffer\n");
                    goto cleanup;
                }
                param.seq_header_flag = 1;
                printf("Sequence Initialized: %ldx%ld, Frame Rate: %d/%d, Interlaced: %d\n", 
                       param.seqsize.lWidth, param.seqsize.lHeight, 
                       param.seqsize.i_frame_rate_num, param.seqsize.i_frame_rate_den, 
                       param.seqsize.b_interlaced);
            } else {
                if (param.b_accelerate) {
                    if (last_frame_error) {
                        cavs_decoder_seq_header_reset_pipeline(decoder);
                    }
                }
            }

            if (last_frame_error) {
                last_frame_error = 0;
            }
        } 
        else if (current_code == CAVS_I_PICUTRE_START_CODE || current_code == CAVS_PB_PICUTRE_START_CODE) {
            
            // Initialize stream with the rest of the buffer
            cavs_decoder_init_stream(decoder, frame_start, remaining_len);
            
            int len;
            // Get just the Picture Header NAL to pass to decode_one_frame
            cavs_decoder_get_one_nal(decoder, nal_buf, &len);

            if (current_code == CAVS_I_PICUTRE_START_CODE) {
                if (!got_keyframe) {
                    got_keyframe = 1;
                }
                if (last_frame_error) {
                    last_frame_error = 0;
                }
            } else {
                if (last_frame_error) {
                    goto next_frame;
                }
                if (!got_keyframe) {
                    goto next_frame;
                }
            }

            int ret = cavs_decode_one_frame(decoder, current_code, &param, nal_buf, len);
            input_frame_count++;
            
            if (ret == CAVS_ERROR) {
                 // Log errors if any
                 printf("Input Frame %d Result: %d (Error)\n", input_frame_count, ret);
                 last_frame_error = 1;
                 got_keyframe = 0;
            } else if (ret == CAVS_FRAME_OUT) {
                write_yuv(fout, &param);
                frame_count++;
                if (frame_count % 25 == 0) {
                    printf("Frame %d decoded\n", frame_count);
                }
            }
        }
        else if (current_code == CAVS_VIDEO_SEQUENCE_END_CODE) {
            printf("Found SEQUENCE END code at offset %ld\n", frame_start - buffer);
        }
        
        uint8_t *next_start;
        next_frame:
        // Find next start code to advance loop
        // Note: find_start_code returns pointer to start of 00 00 01 XX
        // We search starting from current frame_start + 4
        next_start = find_start_code(frame_start + 4, end, &current_code);
        
        frame_start = next_start;
    }

    // Flush delayed frames (B-frames)
    if (param.seq_header_flag) {
        // Try to flush pipeline first
        if (param.b_accelerate) {
            int ret = cavs_decode_one_frame_delay(decoder, &param);
            if (ret == CAVS_FRAME_OUT) {
                write_yuv(fout, &param);
                frame_count++;
                printf("Delayed Frame (Pipeline) %d decoded\n", frame_count);
            }
        }

        // Flush the last reordered frame
        // Note: cavs_out_delay_frame_end does not clear the internal state, so we must call it only once
        if (cavs_out_delay_frame_end(decoder, param.p_out_yuv)) {
             write_yuv(fout, &param);
             frame_count++;
             printf("Delayed Frame (Buffer) %d decoded\n", frame_count);
        }
    }

cleanup:
    printf("Total frames decoded: %d\n", frame_count);
    
    cavs_decoder_destroy(decoder);
    cavs_decoder_buffer_end(&param);
    free(buffer);
    fclose(fout);
    
    return 0;
}
