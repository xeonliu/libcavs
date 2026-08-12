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

## Normative Reference Index

Source comments beside functions, structures, and constant tables are the
authoritative implementation references. This index makes those references
discoverable after the source split; it does not replace the local comments.

| Implementation | Normative source |
| --- | --- |
| `codec/unit`, `codec/syntax`, `codec/pseudo_start_code`, `bitreader` | GB/T 20090.2-2013 7.1 Table 12, 7.2.2 Table 15, 7.3 Tables 21-22, 7.1.3.6 Table 26, 8.2 Tables 42-43, and normative Annexes A-B; corresponding GB/T 20090.16-2016 Tables 14, 21, 22, and 25 and normative Annex A |
| `codec/advanced_entropy`, `codec/broadcast_macroblock` | GB/T 20090.16-2016 7.4-7.6, 8.3-8.4, 9.2-9.8, Tables 44-53, and informative Annex E |
| `codec/baseline_macroblock`, `codec/baseline_decode` | GB/T 20090.2-2013 7.1.3.7 Table 27, 9.2 Tables 59, 61, and 62, 9.4.4, and normative Annex D Tables D.1-D.20 |
| `codec/motion`, `codec/broadcast_motion` | GB/T 20090.2-2013 9.4.6.2-9.4.6.3 and Figures 18 and 32; GB/T 20090.16-2016 9.4.3, 9.4.6, 9.9.1, Tables 55-58, and Figures 11-12 and 24-29 |
| `codec/coefficients`, `dsp/transform`, `dsp/reconstruction`, `codec/broadcast_reconstruction` | GB/T 20090.2-2013 9.5.3, 9.7.1-9.7.2, 9.8.2-9.8.3, 9.11, Figures 33-34, and Tables 70-71; GB/T 20090.16-2016 9.5-9.10, Figures 22-23, and Table 62 |
| `dsp/prediction` | GB/T 20090.2-2013 9.9.1-9.9.4, Tables 65-66, and Figure 20 |
| `dsp/motion_compensation` | GB/T 20090.2-2013 9.10.1-9.10.2 and Figures 36-43; GB/T 20090.16-2016 9.9.2 and Table 63 |
| `codec/loop_filter`, `dsp/loop_filter` | GB/T 20090.16-2016 9.11, Figures 32-34, and Tables 64-65; GB/T 20090.2-2013 9.12, Figures 44-46, and Tables 73-74 |
| `picture`, `codec/slice_decode`, `dpb` | GB/T 20090.16-2016 6.2-6.5, 7.4, 9.1, 9.3, 9.4.5-9.4.6.1, 9.8.2, and Figures 14-20; corresponding GB/T 20090.2-2013 3.27, 6.2, 9.5, and 9.6.1 |

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
