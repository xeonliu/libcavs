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
| Extension, picture, slice syntax | partial | partial | I/PB picture and target-profile slice header parsers plus selected macroblock dispatch; extension semantics and full profile conformance remain pending |
| Macroblock and block syntax | partial | partial | broadcast basic YUV420 macroblock type/partition, intra mode, reference/MVD, Table 42 CBP, QP, and six-block dispatch are implemented; field-slice assembly now uses explicit next-row bounds, while full picture conformance remains pending |
| Basic/advanced entropy decoding | partial | partial | broadcast basic `ue(v)`/`se(v)`, Tables 42 and 54-57, Annex D coefficient dispatch, frame/field inverse scan, and atomic macroblock tests; AEC slice termination and Basic row-bounded termination are integrated, while full picture conformance remains pending |
| 4x4/8x8 inverse transform and quantization | partial | partial | baseline 8x8 frame/field inverse scan, Table 70/71 inverse quantization, T8 inverse transform, and basic macroblock integration; broadcast default and weighted 8x8 inverse quantization are implemented; 4x4/VBS pending |
| Intra/inter prediction and compensation | partial | partial | baseline 8x8 intra path and 9.4.4 macroblock mode assembly, common-profile neighbor prediction/difference decoding, P_Skip/symmetric/B_Direct vector derivation, all luma phases, chroma interpolation, edge replacement, broadcast 9.3 weighted P/B prediction, block reconstruction, and deferred multi-slice field submission; full frame-level conformance remains pending |
| Loop filtering | partial | partial | GB/T 20090.16-2016 9.11 field traversal, boundary strength, thresholds, and scalar kernels are implemented and unit-tested; complete picture integration coverage remains pending |
| YUV420P8 progressive frame I/P/B | not-started | not-started | - |
| YUV422P8, interlace, field pictures | not-started | partial | Broadcast YUV420 successive-field pictures, physical field mapping, and field reconstruction are implemented; YUV422 and other structures remain pending |
| DPB, reordering, low delay, sequence switch | partial | partial | DPB reference selection, modulo-512 distances, delayed anchor output, B output, and flush are implemented; complete low-delay and sequence-switch conformance remains pending |
| Metadata and unknown extension retention | partial | partial | copied user-data and raw-extension API events; known metadata parsing pending |
| BBV validation tooling | not-started | not-started | - |
| `cavsdec` input, output, frame limit, and logging | partial | partial | file/stdin Annex-B scan, planar output, frame events, and strict incomplete-picture rejection; full profile conformance remains pending |
| Random-input robustness | partial | partial | libFuzzer/AFL public API, baseline macroblock, coefficient, reconstruction-math, and intra-prediction harnesses; 1,000-run sanitizer smoke test |

No profile is currently advertised as decodable. Version 1.0 is blocked until
every applicable row is conformant and the release gates in `CONTRIBUTING.md`
and `THIRD_PARTY_NOTICES.md` are cleared.
