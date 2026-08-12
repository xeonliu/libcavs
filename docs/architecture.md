# Internal Architecture

libcavs uses a shallow, one-way internal dependency structure:

```text
public API and input state machine
              |
              v
picture lifecycle and DPB output
              |
              v
codec syntax and reconstruction semantics
              |
              v
scalar DSP kernels
```

## Core

`decoder.c` implements the public API, Annex-B unit state machine, event
priority, and backpressure. It does not decode macroblocks.

`picture_pipeline.[ch]` owns the current picture lifecycle: profile
selection, begin/finish, slice dispatch, field filtering, DPB submission,
display reordering, flush, reset, and the pending output frame. Pipeline here
describes the ordered picture lifecycle; it is not a threaded execution
framework and adds no data copy.

`frame.[ch]` owns frame allocation, private storage, public references, and
the lifetime adapters used by the DPB. `picture.[ch]` defines codec-neutral
picture storage and frame/field coordinate mapping. `dpb.[ch]` owns reference
retention and display reordering without interpreting codec macroblocks.

Core storage modules must not include codec implementation headers. The
forward declaration of `struct cavs_macroblock` in `picture.h` is the only
knowledge required by the storage contract.

## Codec

`codec/` contains start-code classification, syntax and entropy parsing,
profile support checks, motion-vector and reference semantics, macroblock
assembly, scan and QP mapping, reconstruction orchestration, boundary
strength, and loop-filter traversal.

`baseline_decode` and `broadcast_decode` receive explicit, profile-sized
contexts. They must not receive `cavs_decoder` or `cavs_picture_pipeline`.
Profile code may use core storage/DPB APIs and call DSP kernels.

Normative source references stay beside the implementation they describe.
When code moves between layers, its section, figure, and table references
move with it; file-level references must describe only the code still in that
file.

## DSP

`dsp/` contains pure scalar C leaf operations that can be replaced without
changing codec decisions: inverse transforms, intra-prediction formulas,
fractional-sample interpolation, residual composition, and pixel filtering.
Scalar entry points use the `cavs_dsp_*_c` naming convention and are called
directly.

DSP must not include codec, picture, frame, or DPB headers. It receives all
codec-selected modes, strengths, thresholds, and geometry as explicit input.
There is currently no function table, CPU detection, or runtime dispatch.

## Future Directories

Do not create empty architecture or threading directories. Add `src/x86/` or
`src/arm/` only with the first tested SIMD implementation and the dispatch
mechanism that selects it while preserving scalar output bit-for-bit. Add
`src/thread/` only with a real scheduling implementation and documented frame
ownership, ordering, backpressure, and error propagation contracts.

The public ABI remains defined solely by `include/cavs/cavs.h`. Internal test
hooks are compiled only into the non-installed `cavs_test_internal` target
with `CAVS_TESTING`.
