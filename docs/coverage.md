# Standard Coverage Matrix

Status values are `not-started`, `partial`, `implemented`, and `conformant`.
Only `conformant` means independently verified against authorized reference
outputs.

| Area | Baseline 0x20 | Broadcast 0x48 | Evidence |
| --- | --- | --- | --- |
| Annex-B unit boundary validation | partial | partial | API prefix validation; syntax payload unescaping |
| Safe bit reading and order-k signed/unsigned Exp-Golomb | implemented | partial | `cavs_unit` |
| Pseudo-start-code removal and bit repacking | implemented | implemented | `cavs_unit` |
| Sequence header syntax and change events | partial | partial | `cavs_unit`, `cavs_api_test` |
| Level identifier and profile-format constraints | partial | partial | identifier tests; level maxima pending |
| Extension, picture, slice syntax | partial | partial | I/PB picture and target-profile slice header parsers; macroblocks pending |
| Macroblock and block syntax | partial | not-started | baseline YUV420 basic-entropy 8x8 macroblock headers, full Table 45 mapping, continuous six-block coefficient dispatch, and atomic six-block residual/sample reconstruction |
| Basic/advanced entropy decoding | partial | not-started | baseline 8x8 basic entropy, Annex D Tables D.1-D.20, escape and inverse-run tests |
| 4x4/8x8 inverse transform and quantization | partial | not-started | baseline 8x8 frame/field inverse scan, Table 70/71 inverse quantization, T8 inverse transform, and basic macroblock integration; 4x4 and weighted modes pending |
| Intra/inter prediction and compensation | partial | partial | baseline 8x8 intra path, common-profile neighbor prediction/difference decoding, P_Skip/symmetric/B_Direct vector derivation, all luma phases, chroma interpolation, edge replacement, and block reconstruction; frame-level prediction/motion assembly pending |
| Loop filtering | not-started | not-started | - |
| YUV420P8 progressive frame I/P/B | not-started | not-started | - |
| YUV422P8, interlace, field pictures | not-started | not-started | - |
| DPB, reordering, low delay, sequence switch | not-started | not-started | - |
| Metadata and unknown extension retention | partial | partial | copied user-data and raw-extension API events; known metadata parsing pending |
| BBV validation tooling | not-started | not-started | - |
| `cavsdec` input, output, frame limit, and logging | partial | partial | file/stdin Annex-B scan and planar-output path; frame reconstruction pending |
| Random-input robustness | partial | partial | libFuzzer/AFL public API, baseline macroblock, coefficient, reconstruction-math, and intra-prediction harnesses; 1,000-run sanitizer smoke test |

No profile is currently advertised as decodable. Version 1.0 is blocked until
every applicable row is conformant and the release gates in `CONTRIBUTING.md`
and `THIRD_PARTY_NOTICES.md` are cleared.
