# Contributing

Do not consult or copy the former `libcavs.c`, its API, another decoder's
implementation, comments, data layout, constants, or implementation
expressions while contributing to this rewrite. This project is an independent
reimplementation; it does not claim a formal clean-room process.

Every nontrivial syntax element, algorithm, and constant table must be entered
in `docs/sources.csv` before implementation. Cite the standard edition,
clause/table, applicable profile, derivation, and an independent verification
case. Generate derivable values at build time or initialization time. Only
values explicitly enumerated by a standard and not derivable may be transcribed
independently.

Comments are written in English and retain the Chinese standard name where it
helps identification. New code must be portable C99, single-threaded, and free
of SIMD and unaligned type-punning. The library must never print directly.

All changes require focused unit tests. Public release additionally requires
the complete coverage matrix, authorized conformance streams, three-platform
CI, sanitizer-clean runs, license scanning, source-similarity review, media
redistribution review, and a separate standards-essential-patent review.

