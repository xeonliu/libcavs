/*****************************************************************************
 * libcavs.h: public API for the AVS/AVS+ video decoder
 *
 * Supports:
 *   AVS1-P2  (JiZhun profile,   GB/T 20090.2)
 *   AVS1-P16 (Guangdian profile, GB/T 20090.16)
 *
 * --------------------------------------------------------------------------
 * MANDATORY CALLING SEQUENCE
 * --------------------------------------------------------------------------
 *
 * The API has a strict two-phase initialization protocol that is NOT
 * enforced by the library itself.  Violating the order causes silent
 * memory corruption (wrong image-buffer strides) with no error returned.
 *
 * PHASE 1 – PROBE  (scan forward until the first I-picture header)
 * ----------------------------------------------------------------
 * Purpose: determine frame/field coding format BEFORE any memory is
 * allocated inside cavs_decoder_process().
 *
 *   for each NAL unit (00 00 01 XX) from the start of the bitstream:
 *
 *     case CAVS_VIDEO_SEQUENCE_START_CODE:
 *       cavs_decoder_init_stream(dec, nal_start, remaining);
 *       cavs_decoder_get_one_nal(dec, buf, &len);
 *       cavs_decoder_probe_seq(dec, buf, len);        // parses vsh, sets b_get_video_sequence_header
 *
 *     case CAVS_EXTENSION_START_CODE / CAVS_USER_DATA_CODE:
 *       cavs_decoder_init_stream(dec, nal_start, remaining);
 *       cavs_decoder_get_one_nal(dec, buf, &len);
 *       cavs_decoder_probe_seq(dec, buf, len);        // parses display/color extension
 *
 *     case CAVS_I_PICUTRE_START_CODE:
 *       cavs_decoder_init_stream(dec, nal_start, remaining);
 *       cavs_decoder_get_one_nal(dec, buf, &len);
 *       cavs_decoder_pic_header(dec, buf, len, param, CAVS_I_PICUTRE_START_CODE);
 *                                                     // sets param->b_interlaced
 *       cavs_decoder_set_format_type(dec, param);     // copies b_interlaced → dec->param
 *       // PROBE COMPLETE – break out of probe loop
 *
 * WHY this matters: cavs_decoder_process() calls cavs_alloc_resource()
 * internally.  cavs_alloc_resource() reads dec->param.b_interlaced to
 * choose between frame-mode strides (width+32) and field-mode strides
 * (2*(width+32)).  If b_interlaced is wrong at that point, every image
 * buffer is the wrong size and all decoded pixel data is misaligned.
 *
 * PHASE 2 – DECODE LOOP
 * ----------------------------------------------------------------
 *
 *   // Per sequence header:
 *   cavs_decoder_init_stream(dec, nal_start, remaining);
 *   cavs_decoder_get_one_nal(dec, buf, &len);
 *   cavs_decoder_process(dec, buf, len);              // allocs resources, inits threadpool
 *   cavs_decoder_get_seq(dec, &param->seqsize);       // copy width/height/framerate out
 *   param->i_thread_num = 64;                         // reset after each seq header
 *   param->output_type  = -1;                         // reset after each seq header
 *   if (first sequence) {
 *     cavs_decoder_seq_init(dec, param);              // sets fld_mb_end, propagates b_interlaced
 *     cavs_decoder_buffer_init(param);                // allocs param->p_out_yuv[3]
 *     param->seq_header_flag = 1;
 *   } else if (b_accelerate && last_frame_error) {
 *     cavs_decoder_seq_header_reset_pipeline(dec);
 *   }
 *
 *   // Per extension / user-data NAL:
 *   cavs_decoder_init_stream / get_one_nal / process   (keeps dec internal state current)
 *
 *   // Per picture (I or PB):
 *   cavs_decoder_init_stream(dec, nal_start, remaining);
 *   cavs_decoder_get_one_nal(dec, buf, &len);
 *   ret = cavs_decode_one_frame(dec, startcode, param, buf, len);
 *   // ret == CAVS_FRAME_OUT → param->p_out_yuv contains a decoded frame in display order
 *   // ret == CAVS_ERROR     → mark error, skip until next I-frame
 *
 * PHASE 3 – FLUSH (end of stream)
 * ----------------------------------------------------------------
 *   // b_accelerate mode keeps one reference frame buffered in the pipeline:
 *   ret = cavs_decode_one_frame_delay(dec, param);
 *   if (ret == CAVS_FRAME_OUT) → output param->p_out_yuv
 *
 *   // non-low-delay streams have an additional reordered frame in the output buffer:
 *   if (cavs_out_delay_frame_end(dec, param->p_out_yuv)) → output param->p_out_yuv
 *
 * TEARDOWN
 * ----------------------------------------------------------------
 *   cavs_decoder_destroy(dec);      // frees decoder handle and internal image buffers
 *   cavs_decoder_buffer_end(param); // frees param->p_out_yuv[0..2]
 *
 *****************************************************************************
 */

#ifndef _CAVS_DECODER_H
#define _CAVS_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef struct tagcavs_decoder cavs_decoder;

/* -------------------------------------------------------------------------
 * Start-code constants
 * These are the 32-bit values formed by 0x000001XX as found in the bitstream.
 * The short aliases (CAVS_START_CODE etc.) are kept for backward compat.
 * -------------------------------------------------------------------------*/
#define SLICE_MAX_START_CODE    0x000001af  /* last slice start code */
#define EXT_START_CODE          0x000001b5  /* alias: CAVS_EXTENSION_START_CODE */
#define USER_START_CODE         0x000001b2  /* alias: CAVS_USER_DATA_CODE */
#define CAVS_START_CODE         0x000001b0  /* alias: CAVS_VIDEO_SEQUENCE_START_CODE */
#define PIC_I_START_CODE        0x000001b3  /* alias: CAVS_I_PICUTRE_START_CODE */
#define PIC_PB_START_CODE       0x000001b6  /* alias: CAVS_PB_PICUTRE_START_CODE */

/* Return-value flags from cavs_decoder_process / cavs_decode_one_frame */
#define CAVS_SEQ_HEADER  1   /* a sequence header was successfully parsed */
#define CAVS_FRAME_OUT   2   /* a decoded frame is ready in param->p_out_yuv */
#define CAVS_SEQ_END     4   /* sequence end code encountered */
#define CAVS_ERROR       8   /* unrecoverable parse/decode error */
#define CAVS_USER_DATA   16  /* user-data NAL processed */

/* Color-space identifier (only YUV 4:2:0 is currently supported) */
#define CAVS_CS_YUV420 0

/* Extension IDs returned inside CAVS_EXTENSION_START_CODE NALs */
#define CAVS_SEQUENCE_DISPLAY_EXTENTION     0x00000002
#define CAVS_COPYRIGHT_EXTENTION            0x00000004
#define CAVS_PICTURE_DISPLAY_EXTENTION      0x00000007
#define CAVS_CAMERA_PARAMETERS_EXTENTION    0x0000000B

/* Start-code range for slice data (0x100..0x1AF) */
#define CAVS_SLICE_MIN_START_CODE           0x00000100
#define CAVS_SLICE_MAX_START_CODE           0x000001AF

/* Full set of stream-layer start codes */
#define CAVS_VIDEO_SEQUENCE_START_CODE      0x000001B0  /* sequence header */
#define CAVS_VIDEO_SEQUENCE_END_CODE        0x000001B1  /* sequence end */
#define CAVS_USER_DATA_CODE                 0x000001B2  /* user data */
#define CAVS_I_PICUTRE_START_CODE           0x000001B3  /* I-picture header  (typo preserved for ABI compat) */
#define CAVS_EXTENSION_START_CODE           0x000001B5  /* extension data */
#define CAVS_PB_PICUTRE_START_CODE          0x000001B6  /* P/B-picture header (typo preserved) */
#define CAVS_VIDEO_EDIT_CODE                0x000001B7  /* video edit code */
#define CAVS_VIDEO_TIME_CODE                0x000001E0  /* time code */

/* Maximum size of a single encoded frame/field in bytes */
#define MAX_CODED_FRAME_SIZE 4000000

/* -------------------------------------------------------------------------
 * cavs_seq_info – sequence-level metadata
 * Filled by cavs_decoder_get_seq() after a sequence header is processed.
 * -------------------------------------------------------------------------*/
typedef struct tagcavs_seq_info
{
    long lWidth;            /* picture width  in luma samples */
    long lHeight;           /* picture height in luma samples */
    int  i_frame_rate_den;  /* frame-rate denominator (e.g. 1) */
    int  i_frame_rate_num;  /* frame-rate numerator   (e.g. 25) */
    int  b_interlaced;      /* 0 = progressive sequence, 1 = interlaced */
    int  profile;           /* profile_id from sequence header */
    int  level;             /* level_id   from sequence header */
    int  aspect_ratio;      /* aspect_ratio_information */
    int  low_delay;         /* b_low_delay: 1 = no B-frames, output without reorder */
    int  mb_width;          /* width  in 16×16 macroblocks */
    int  mb_height;         /* height in 16×16 macroblocks */
    int  frame_rate_code;   /* raw frame_rate_code from bitstream (index into table) */
} cavs_seq_info;

/* -------------------------------------------------------------------------
 * cavs_param – decoder configuration and output state shared with caller
 *
 * Fields the CALLER must set before cavs_decoder_create():
 *   b_accelerate  – set to 1 to enable the pipelined multi-thread path.
 *                   This is the recommended mode and matches the reference
 *                   implementation.  Set to 0 only for debugging.
 *   i_thread_num  – desired thread count (set to 64; overwritten by
 *                   cavs_decoder_thread_param_init() to actual CPU count).
 *   output_type   – initialise to -1.
 *
 * Fields the CALLER must reset after EACH sequence header in the decode loop:
 *   i_thread_num  = 64
 *   output_type   = -1
 *
 * Fields written BY THE LIBRARY (read-only for the caller after decode):
 *   p_out_yuv[3]  – pointers to tightly-packed planar YUV 4:2:0 output.
 *                   Allocated by cavs_decoder_buffer_init(), freed by
 *                   cavs_decoder_buffer_end().  Valid when CAVS_FRAME_OUT
 *                   is returned; layout is: Y(W×H), U(W/2×H/2), V(W/2×H/2).
 *   seqsize        – populated by cavs_decoder_get_seq().
 *   output_type   – set on each CAVS_FRAME_OUT: 0=I, 1=P, 2=B, -1=none.
 *   b_interlaced  – set by cavs_decoder_set_format_type() during probe and
 *                   by cavs_decoder_seq_init() during decode.
 *   fld_mb_end[2] – set by cavs_decoder_seq_init(); do not modify.
 *
 * Fields that must NOT be modified by the caller after init:
 *   seq_header_flag – managed by caller as a one-shot flag (0→1 on first
 *                     sequence header); do not reset between frames.
 * -------------------------------------------------------------------------*/
typedef struct tagcavs_param
{
    unsigned int   i_color_space;   /* color space; only CAVS_CS_YUV420 supported */
    unsigned char *p_out_yuv[3];    /* [0]=Y [1]=U [2]=V; allocated by cavs_decoder_buffer_init */
    unsigned int   seq_header_flag; /* 0 = no sequence header seen yet; set to 1 after first init */

    int i_thread_num;               /* thread count; set to 64 before create, reset after each seq hdr */

    cavs_seq_info seqsize;          /* filled by cavs_decoder_get_seq() */

    int cpu;                        /* CPU capability flags; leave as 0 (auto-detect internally) */
    int fld_mb_end[2];              /* [0]=top-field last MB row, [1]=bottom; set by seq_init */
    int b_interlaced;               /* 0=frame-coded, 1=field-coded; set by probe + seq_init */
    int b_accelerate;               /* 0=single-thread reference path, 1=pipelined (recommended) */

    int output_type;                /* -1=init, 0=I, 1=P, 2=B; updated on each CAVS_FRAME_OUT */
} cavs_param;


/* =========================================================================
 * LIFECYCLE
 * =========================================================================*/

/**
 * cavs_decoder_create – allocate and initialise a decoder instance.
 *
 * @param pp_decoder  output: receives the opaque decoder handle.
 * @param param       caller-provided config; reads b_accelerate, i_thread_num.
 *                    The library copies relevant fields into its own internal
 *                    param; the caller's copy is NOT kept by reference.
 * @return 0 on success, non-zero on failure.
 *
 * NOTE: call cavs_decoder_thread_param_init() immediately after to overwrite
 * i_thread_num with the actual CPU-count-based value.
 */
int cavs_decoder_create( void **pp_decoder, cavs_param *param );

/**
 * cavs_decoder_thread_param_init – set thread count to number of logical CPUs.
 * Must be called once immediately after cavs_decoder_create().
 */
int cavs_decoder_thread_param_init( void *p_decoder );

/**
 * cavs_decoder_destroy – free the decoder handle and all internal image buffers.
 * Does NOT free param->p_out_yuv; call cavs_decoder_buffer_end() for that.
 */
void cavs_decoder_destroy( void *p_decoder );

/**
 * cavs_decoder_buffer_init – allocate the output YUV frame buffer.
 * Must be called ONCE after the first sequence header is processed and
 * param->seqsize has been filled by cavs_decoder_get_seq().
 * Sets param->p_out_yuv[0..2] to tightly-packed planar buffers sized W*H*3/2.
 * @return 0 on success, -1 if resolution exceeds 1920x1080 or alloc fails.
 */
int cavs_decoder_buffer_init( cavs_param *param );

/**
 * cavs_decoder_buffer_end – free the output YUV frame buffer.
 * Call during teardown after cavs_decoder_destroy().
 */
int cavs_decoder_buffer_end( cavs_param *param );


/* =========================================================================
 * BITSTREAM I/O
 * =========================================================================*/

/**
 * cavs_decoder_init_stream – point the decoder's internal bitreader at a
 * raw byte buffer starting at a start-code boundary (00 00 01 XX).
 *
 * @param rawstream  pointer to the 00 00 01 XX start-code prefix.
 * @param i_len      number of bytes from rawstream to end-of-buffer.
 *                   Passing remaining_bytes_to_EOF is safe; the parser will
 *                   stop at the next start code.
 *
 * Must be called before every cavs_decoder_get_one_nal() call.
 */
int cavs_decoder_init_stream( void *p_decoder, unsigned char *rawstream, unsigned int i_len );

/**
 * cavs_decoder_get_one_nal – extract one NAL unit from the current stream.
 *
 * Reads from the position set by cavs_decoder_init_stream() and copies
 * bytes up to (but not including) the next start code into buf.
 *
 * @param buf     caller-supplied buffer of at least MAX_CODED_FRAME_SIZE bytes.
 * @param length  output: number of bytes written to buf (includes the leading
 *                4-byte start code 00 00 01 XX).
 * @return 0 on success.
 *
 * Typical usage:
 *   cavs_decoder_init_stream(dec, ptr_to_nal, bytes_remaining_in_file);
 *   cavs_decoder_get_one_nal(dec, buf, &len);
 *   // buf[0..3] == 00 00 01 XX, buf[4..len-1] == payload
 */
int cavs_decoder_get_one_nal( void *p_decoder, unsigned char *buf, int *length );


/* =========================================================================
 * PROBE PHASE  (must complete before the decode loop)
 * =========================================================================*/

/**
 * cavs_decoder_probe_seq – lightweight sequence/extension parser used
 * exclusively during the probe phase.
 *
 * Unlike cavs_decoder_process(), this function does NOT allocate any
 * internal image buffers.  It parses the sequence header into dec->vsh and
 * sets dec->b_get_video_sequence_header = 1, making the decoder ready to
 * accept picture headers.
 *
 * Must be called for:
 *   CAVS_VIDEO_SEQUENCE_START_CODE  – to populate dec->vsh
 *   CAVS_EXTENSION_START_CODE       – to process display extension etc.
 *   CAVS_USER_DATA_CODE             – to process user-data
 *
 * @param p_in        NAL buffer returned by cavs_decoder_get_one_nal().
 * @param i_in_length length returned by cavs_decoder_get_one_nal().
 * @return CAVS_SEQ_HEADER on success for sequence NAL, 0 for others,
 *         CAVS_ERROR on parse failure.
 */
int cavs_decoder_probe_seq( void *p_decoder, unsigned char *p_in, int i_in_length );

/**
 * cavs_decoder_pic_header – parse a picture header NAL during probe.
 *
 * Reads the I- or PB-picture header and stores result in private decoder
 * state AND writes param->b_interlaced (0=frame, 1=field).
 *
 * IMPORTANT: must be called BEFORE cavs_decoder_process() so that
 * dec->param.b_interlaced is set correctly before cavs_alloc_resource()
 * allocates image buffers sized for frame- vs. field-mode strides.
 *
 * @param p_buf          NAL buffer from cavs_decoder_get_one_nal().
 * @param i_len          NAL length.
 * @param param          param->b_interlaced is written as output.
 * @param cur_startcode  CAVS_I_PICUTRE_START_CODE or CAVS_PB_PICUTRE_START_CODE.
 * @return 0 on success, CAVS_ERROR on parse failure.
 */
int cavs_decoder_pic_header( void *p_decoder, unsigned char *p_buf, int i_len,
                              cavs_param *param, unsigned int cur_startcode );

/**
 * cavs_decoder_set_format_type – copy param->b_interlaced into the decoder's
 * internal param so that cavs_alloc_resource() uses the correct stride.
 *
 * Must be called immediately after cavs_decoder_pic_header() during probe,
 * and before cavs_decoder_process() is ever called.
 */
int cavs_decoder_set_format_type( void *p_decoder, cavs_param *param );


/* =========================================================================
 * DECODE LOOP – sequence header processing
 * =========================================================================*/

/**
 * cavs_decoder_process – process a sequence header, extension, or end NAL.
 *
 * For CAVS_VIDEO_SEQUENCE_START_CODE: parses the sequence header, calls
 * cavs_alloc_resource() to allocate internal image buffers (using
 * dec->param.b_interlaced which MUST already be set via the probe phase),
 * and initialises the thread pool.
 *
 * For CAVS_EXTENSION_START_CODE / CAVS_USER_DATA_CODE: updates decoder
 * internal state (color/display metadata).  Must be called for these NALs
 * in the decode loop as well as during probe.
 *
 * @param p_in      NAL buffer from cavs_decoder_get_one_nal().
 * @param i_in_len  NAL length.
 * @return CAVS_SEQ_HEADER, CAVS_SEQ_END, CAVS_USER_DATA, or CAVS_ERROR.
 *
 * PRECONDITION: dec->param.b_interlaced must be correctly set via probe
 * phase before this is called the FIRST time per sequence.
 */
int cavs_decoder_process( void *p_decoder, unsigned char *p_in, int i_in_len );

/**
 * cavs_decoder_get_seq – copy sequence metadata into caller's cavs_seq_info.
 * Call immediately after cavs_decoder_process() returns CAVS_SEQ_HEADER.
 * @return 0 on success, -1 if no sequence header has been parsed yet.
 */
int cavs_decoder_get_seq( void *p_decoder, cavs_seq_info *p_si );

/**
 * cavs_decoder_seq_init – complete one-time frame-level initialisation.
 *
 * Computes fld_mb_end[] from param->seqsize.lHeight, propagates b_interlaced
 * from seqsize into param and dec->param, and allocates the frame-pack buffer.
 *
 * Must be called ONCE after the first cavs_decoder_process() per stream, and
 * before any cavs_decode_one_frame() calls.
 *
 * @param param  seqsize must be populated (by cavs_decoder_get_seq()) before
 *               this call.  Writes fld_mb_end[], b_interlaced.
 */
int cavs_decoder_seq_init( void *p_decoder, cavs_param *param );

/**
 * cavs_decoder_seq_end – release resources associated with the current sequence.
 * Called internally by cavs_decoder_destroy(); rarely needed directly.
 */
int cavs_decoder_seq_end( void *p_decoder );

/**
 * cavs_decoder_seq_header_reset_pipeline – reset the b_accelerate pipeline
 * after a decode error, so that the next I-frame starts cleanly.
 * Only meaningful when param->b_accelerate == 1.
 * Call when: previous sequence had an error AND a new sequence header arrives.
 */
int cavs_decoder_seq_header_reset_pipeline( void *p_decoder );

/**
 * cavs_decoder_seq_header_reset – lighter reset (non-pipeline path).
 */
int cavs_decoder_seq_header_reset( void *p_decoder );


/* =========================================================================
 * DECODE LOOP – per-frame decoding
 * =========================================================================*/

/**
 * cavs_decode_one_frame – decode one picture (I or PB) from a NAL buffer.
 *
 * Internally packs the picture header NAL, reads remaining slice NALs from
 * the stream initialised by cavs_decoder_init_stream(), decodes the frame,
 * and writes the result to param->p_out_yuv in DISPLAY order.
 *
 * Display ordering (non-low-delay):
 *   When an I- or P-frame finishes decoding, the PREVIOUSLY decoded I/P frame
 *   is output (one-frame delay).  B-frames are output immediately.
 *   This means the caller must also call cavs_decode_one_frame_delay() and
 *   cavs_out_delay_frame_end() at end-of-stream to flush the pipeline.
 *
 * @param i_startcode  CAVS_I_PICUTRE_START_CODE or CAVS_PB_PICUTRE_START_CODE.
 * @param param        must have seq_header_flag==1; p_out_yuv must be allocated.
 * @param buf          NAL buffer from cavs_decoder_get_one_nal() (picture header).
 * @param length       NAL length.
 * @return CAVS_FRAME_OUT if a frame is ready in param->p_out_yuv,
 *         0              if the frame was buffered (display-order delay),
 *         CAVS_ERROR     on decode failure.
 *
 * On CAVS_ERROR: treat subsequent PB-frames as undecodeable until the next
 * I-frame.  Reset got_keyframe=0 and last_frame_error=1.
 */
int cavs_decode_one_frame( void *p_decoder, int i_startcode, cavs_param *param,
                            unsigned char *buf, int length );

/**
 * cavs_decoder_cur_frame_type – return the picture coding type of the frame
 * most recently submitted to cavs_decode_one_frame().
 * @return 0=I, 1=P, 2=B  (matches i_picture_coding_type in the standard).
 * Only meaningful when b_accelerate==1; use the PB start-code to distinguish
 * otherwise.
 */
int cavs_decoder_cur_frame_type( void *p_decoder );


/* =========================================================================
 * END-OF-STREAM FLUSH
 * =========================================================================*/

/**
 * cavs_decode_one_frame_delay – flush the one reference frame held in the
 * b_accelerate pipeline at end-of-stream.
 *
 * Must be called ONCE after all input NALs have been processed.
 * Only produces output when b_accelerate==1 and the stream is not low-delay.
 *
 * @return CAVS_FRAME_OUT if a frame was flushed into param->p_out_yuv, 0 otherwise.
 */
int cavs_decode_one_frame_delay( void *p_decoder, cavs_param *param );

/**
 * cavs_out_delay_frame_end – flush the final reordered frame from the
 * display-order output buffer (the last P-frame held waiting for a
 * subsequent frame to confirm its display position).
 *
 * Must be called ONCE after cavs_decode_one_frame_delay().
 * Applicable to non-low-delay streams regardless of b_accelerate.
 *
 * @param p_out_yuv  param->p_out_yuv; the flushed frame is written here.
 * @return non-zero if a frame was written, 0 if nothing to flush.
 */
int cavs_out_delay_frame_end( void *p_decoder, unsigned char *p_out_yuv[3] );

/**
 * cavs_out_delay_frame – flush mid-stream delayed frame (non-accelerate path).
 * Used internally; prefer cavs_out_delay_frame_end() for end-of-stream flush.
 */
int cavs_out_delay_frame( void *p_decoder, unsigned char *p_out_yuv[3] );

/**
 * cavs_set_last_delay_frame – mark the pipeline's last delayed-frame state.
 * Internal use; not needed by typical callers.
 */
int cavs_set_last_delay_frame( void *p_decoder );


/* =========================================================================
 * MISCELLANEOUS / ADVANCED
 * =========================================================================*/

/**
 * cavs_decoder_low_delay_value – return the b_low_delay flag from the
 * sequence header (1 = no B-frames, frames output without reordering delay).
 */
int cavs_decoder_low_delay_value( void *p_decoder );

/**
 * cavs_decoder_slice_destroy – release per-slice resources when crossing a
 * sequence boundary.  Called internally; exposed for advanced use.
 */
void cavs_decoder_slice_destroy( void *p_decoder );

/**
 * cavs_decoder_slice_num_probe – probe the number of slices in a frame.
 * Used internally to determine thread partitioning; not needed by callers.
 */
int cavs_decoder_slice_num_probe( void *p_decoder, int i_startcode, cavs_param *param,
                                   unsigned char *buf, int length );


#ifdef __cplusplus
}
#endif

#endif /* _CAVS_DECODER_H */
