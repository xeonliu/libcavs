# libcavs API Design Analysis and Improvement Proposals

## 1. Current API Overview

libcavs exposes a C API for decoding Chinese AVS (GB/T 20090.2) and AVS+
(GB/T 20090.16) video.  Callers are responsible for bitstream parsing, NAL extraction,
and explicit two-phase initialization.  All state is carried in an opaque
`void *` decoder handle plus a caller-managed `cavs_param` struct.

---

## 2. Problems with the Current Design

### 2.1 Hidden ordering constraint — the probe/alloc race

**Root cause:**  
`cavs_decoder_process()` calls `cavs_alloc_resource()` internally, which
reads `dec->param.b_interlaced` to decide between two fundamentally different
image-buffer layouts:

| Mode | Y stride | Chroma stride |
|------|----------|---------------|
| Frame (progressive) | `width + 32` | `width/2 + 16` |
| Field (interlaced) | `2 × (width + 32)` | `2 × (width/2 + 16)` |

`b_interlaced` can only be determined from the **picture header** (`0x1B3` /
`0x1B6`), which appears *after* the sequence header (`0x1B0`) in the
bitstream.  This creates a chicken-and-egg situation:

```
Sequence header arrives → process() → alloc_resource() reads b_interlaced
                                       ↑ not set yet!  ← picture header not seen
```

**Consequence:**  For an interlaced stream, the buffers are allocated with
frame-mode strides (half the required size).  The decoder writes field-mode
data anyway, silently overwriting memory beyond the buffer.  The output is
visually corrupted and the process may crash.  No error is returned.

**Current workaround (probe phase):**  
The caller must do a pre-scan of the bitstream to find the first I-picture
header, call `cavs_decoder_pic_header()` + `cavs_decoder_set_format_type()`
to push `b_interlaced` into the decoder *before* `cavs_decoder_process()` is
ever called.  This is not documented and is invisible from the API surface.

---

### 2.2 Split responsibility for `b_interlaced`

`b_interlaced` is determined and set in three different places:

| Where | API | Purpose |
|-------|-----|---------|
| Probe: picture header | `cavs_decoder_pic_header()` + `cavs_decoder_set_format_type()` | pre-alloc |
| Decode: sequence header | `cavs_decoder_seq_init()` | per-seq reset |
| Decode: `cavs_param.seqsize.b_interlaced` | `cavs_decoder_get_seq()` | read-back |

The sequence header carries `b_progressive_sequence` which is the complement
of `b_interlaced`.  `cavs_alloc_resource()` should read that field directly
from `dec->vsh` instead of from `dec->param.b_interlaced`, eliminating the
dependency on the probe phase entirely.

---

### 2.3 `cavs_param` is overloaded: config + output + internal state

`cavs_param` simultaneously acts as:

1. **Input config** (`b_accelerate`, `i_thread_num`)
2. **Output buffer** (`p_out_yuv`, `output_type`)
3. **Internal decoder state** (`fld_mb_end`, `b_interlaced`, `seq_header_flag`)
4. **Sequence metadata** (`seqsize`)

The caller is expected to reset `i_thread_num` and `output_type` on every
sequence header but must *not* reset `seq_header_flag` or `p_out_yuv`.
There is no mechanism to enforce these rules.

---

### 2.4 Caller owns output memory (inverted responsibility)

`cavs_decoder_buffer_init()` and `cavs_decoder_buffer_end()` are called by
the **caller**, but the allocated pointer is written into `param->p_out_yuv`
and then used internally by `cavs_out_image_yuv420()`.  This means:
- The library writes into memory it does not own.
- The caller must not free this memory until after `cavs_decoder_destroy()`.
- There is no ownership contract in the API.

---

### 2.5 Display-order reordering is implicit and partially external

With `b_accelerate=1` and `b_low_delay=0` (B-frames present), decoded frames
arrive in coding order but must be output in display order.  The library
handles the one-frame P/I delay internally, but the `ref.c` reference
implementation maintains an additional external DPB (Decoded Picture Buffer)
with `DPB[0]` / `DPB[1]` for its own reordering logic.  The reason two
separate reordering mechanisms coexist is not documented.

---

### 2.6 Error recovery is caller-managed with no helper

After `CAVS_ERROR`, the caller must:
1. Set `got_keyframe = 0` and `last_frame_error = 1`
2. Skip all subsequent PB-frames
3. On the next sequence header, call `cavs_decoder_seq_header_reset_pipeline()`
4. Clear both flags

None of this is encapsulated.  A multi-use caller will likely get it wrong.

---

### 2.7 Ambiguous / incomplete function names

| Current name | Problem |
|---|---|
| `cavs_decoder_create` | Does not mention that `b_interlaced` must be pre-set before first `process()` |
| `cavs_decoder_process` | Misleading: processes *only* seq/ext/end NALs, not picture data |
| `cavs_decoder_set_format_type` | Opaque name; "format type" = "interlaced flag" |
| `cavs_decoder_probe_seq` | "probe" implies lightweight; actually permanently modifies decoder state |
| `CAVS_I_PICUTRE_START_CODE` | Typo (`PICUTRE` → `PICTURE`); cannot fix without breaking ABI |

---

## 3. Proposed Improvements

### Proposal A: Fix the root cause — read `b_interlaced` from `vsh` in `cavs_alloc_resource()`

**Change:** In `cavs_alloc_resource()` (libcavs.c line ~8011), replace:

```c
// Current (reads from param, which may not be set yet)
int b_interlaced = p->param.b_interlaced;
```

with:

```c
// Proposed (reads directly from the just-parsed sequence header)
int b_interlaced = !p->vsh.b_progressive_sequence;
```

**Impact:**
- `cavs_decoder_probe_seq()` + `cavs_decoder_pic_header()` + `cavs_decoder_set_format_type()` are no longer required before `cavs_decoder_process()`.
- The probe pass in callers becomes optional (only needed for early format detection e.g. for `AVCodecContext` setup).
- Zero ABI change; zero behavior change for progressive streams.

This is a one-line fix with the highest impact.

---

### Proposal B: Split `cavs_param` into config and output structs

```c
/* Caller sets these once before cavs_decoder_create() */
typedef struct {
    int b_accelerate;   /* 1 = pipelined (recommended) */
    int i_thread_num;   /* max threads; 0 = auto */
} cavs_config;

/* Decoder writes these; caller reads them */
typedef struct {
    unsigned char *p_out_yuv[3]; /* decoded frame (Y/U/V planar) */
    int            output_type;  /* 0=I 1=P 2=B -1=none */
    cavs_seq_info  seqinfo;      /* resolution, frame rate, … */
} cavs_output;
```

Internal state (`fld_mb_end`, `b_interlaced`, `seq_header_flag`) moves fully
into the opaque decoder handle.  The caller no longer touches it.

---

### Proposal C: Merge the initialization sequence into two calls

Replace the current six-step first-sequence-header ceremony:

```c
// Current (6 steps, undocumented order)
cavs_decoder_process(dec, buf, len);
cavs_decoder_get_seq(dec, &param.seqsize);
param.i_thread_num = 64;
param.output_type  = -1;
cavs_decoder_seq_init(dec, &param);
cavs_decoder_buffer_init(&param);
param.seq_header_flag = 1;
```

With a single call:

```c
// Proposed
int cavs_decoder_open_sequence(void *p_decoder, cavs_config *cfg,
                                cavs_seq_info *info_out);
// Internally: process → get_seq → seq_init → buffer_init
// Returns CAVS_SEQ_HEADER on success.
// Caller only reads info_out; owns nothing.
```

Output buffer ownership moves into the library:  

```c
// Library allocates and owns p_out_yuv; caller gets a const view
const cavs_output *cavs_decode_one_frame_v2(void *p_decoder, int startcode,
                                             unsigned char *buf, int len);
// Returns NULL if no frame ready, pointer to internal output if CAVS_FRAME_OUT.
```

---

### Proposal D: Add explicit error-recovery API

```c
/**
 * cavs_decoder_recover – reset decoder state after CAVS_ERROR.
 * Call when a new I-frame arrives after an error.
 * Replaces the manual got_keyframe/last_frame_error flag management.
 */
void cavs_decoder_recover(void *p_decoder);
```

Internally wraps `cavs_decoder_seq_header_reset_pipeline()` + flag resets.

---

### Proposal E: Deprecate probe-phase functions (after Proposal A)

Once `cavs_alloc_resource()` reads `b_interlaced` from `vsh` directly, the
following functions become no-ops for the common case and should be marked
deprecated:

- `cavs_decoder_probe_seq()` → use `cavs_decoder_process()` directly
- `cavs_decoder_pic_header()` → probe only; not needed for alloc safety
- `cavs_decoder_set_format_type()` → internal detail, remove from public API

---

## 4. Priority Summary

| # | Proposal | Risk | Benefit |
|---|----------|------|---------|
| A | Fix `b_interlaced` source in `cavs_alloc_resource` | Very low (1 line) | Eliminates silent memory corruption |
| D | `cavs_decoder_recover()` | Low | Removes error-recovery footgun |
| B | Split `cavs_param` | Medium (ABI break) | Clean ownership contract |
| C | Merge init calls | Medium (API break) | Reduces integration complexity |
| E | Deprecate probe API | Low (after A) | Reduces API surface |

Proposal A should be applied immediately.  Proposals B and C are best
addressed in a major version bump alongside fixing the `PICUTRE` typo in the
start-code constants.
