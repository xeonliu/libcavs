# Standard Coverage Matrix

Status values are `not-started`, `partial`, `implemented`, and `conformant`.
Only `conformant` means independently verified against authorized reference
outputs.

| Area | Baseline 0x20 | Broadcast 0x48 | Evidence |
| --- | --- | --- | --- |
| Annex-B unit boundary validation | partial | partial | API prefix validation |
| Safe bit reading and order-k signed/unsigned Exp-Golomb | implemented | partial | `cavs_unit` |
| Pseudo-start-code removal and bit repacking | implemented | implemented | `cavs_unit` |
| Sequence, extension, picture, slice syntax | not-started | not-started | - |
| Macroblock and block syntax | not-started | not-started | - |
| Basic/advanced entropy decoding | not-started | not-started | - |
| 4x4/8x8 inverse transform and quantization | not-started | not-started | - |
| Intra/inter prediction and compensation | not-started | not-started | - |
| Loop filtering | not-started | not-started | - |
| YUV420P8 progressive frame I/P/B | not-started | not-started | - |
| YUV422P8, interlace, field pictures | not-started | not-started | - |
| DPB, reordering, low delay, sequence switch | not-started | not-started | - |
| Metadata and unknown extension retention | not-started | not-started | - |
| BBV validation tooling | not-started | not-started | - |

No profile is currently advertised as decodable. Version 1.0 is blocked until
every applicable row is conformant and the release gates in `CONTRIBUTING.md`
and `THIRD_PARTY_NOTICES.md` are cleared.
