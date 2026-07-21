# libcavs

libcavs is an independent C99 reimplementation of the video decoders defined
by GB/T 20090.2-2013 baseline profile `0x20` and GB/T 20090.16-2016 broadcast
profile `0x48`. The rewrite does not provide compatibility with any earlier
libcavs API or source tree.

The current `0.1.0-dev` tree is a foundation release, not a working decoder.
It provides the public API, build/install rules, and initial safe bit-reading
code. Bitstream decoding remains gated as unsupported until the corresponding
standard coverage and conformance tests are complete.

```sh
cmake -S . -B build -DLIBCAVS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

See `docs/coverage.md` for implementation status and `CONTRIBUTING.md` for the
source-control rules that apply to this independent rewrite.

The public decoder API has opt-in fuzz targets. Use Clang for libFuzzer:

```sh
cmake -S . -B build-fuzz -DLIBCAVS_BUILD_TESTS=OFF \
  -DLIBCAVS_BUILD_SHARED=OFF -DLIBCAVS_BUILD_FUZZER=ON
cmake --build build-fuzz
```

For AFL, configure with an AFL compiler and
`-DLIBCAVS_BUILD_AFL_FUZZER=ON`; the `cavs_afl_decoder` target reads one test
case from standard input.
