# libcavs

libcavs is an independent C99 reimplementation of the video decoders defined
by GB/T 20090.2-2013 baseline profile `0x20` and GB/T 20090.16-2016 broadcast
profile `0x48`. The rewrite does not provide compatibility with any earlier
libcavs API or source tree.

The current `0.1.0-dev` tree includes a working scalar decoder for the
broadcast-profile subset used by the local CCTV-9 regression stream. Profile
support remains partial until the remaining standard features and independent
conformance tests are complete. The local CCTV-9 file ends with an incomplete
Broadcast picture; the strict pipeline rejects that trailing picture during
flush under GB/T 20090.16-2016 7.4 and 9.3 after producing the preceding
complete frames. The `/tmp/libcavs-reference` result is not a conformance
oracle for this truncated tail.

```sh
cmake -S . -B build -DLIBCAVS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`cavsdec [--frames N] [--log LEVEL] [--output FILE] input.avs` reads Annex-B
input and writes decoded planar YUV frames. Use `-` for standard input or
output. The implemented broadcast subset currently covers profile `0x48`,
YUV420, successive interlaced field pictures, advanced and basic entropy
dispatch, I/P/B pictures, default and selected weighted quantization, motion
compensation, loop filtering, reference management, and display reordering.
These are partial standard paths; YUV422, progressive/frame structures,
4x4/VBS broadcast syntax, complete low-delay rules, and full-picture
conformance remain pending.

## Broadcast Roadmap

This checklist follows GB/T 20090.16-2016. `Implemented` means that the rule
is present in the decoder path and has focused local tests; it does not mean
independent conformance. `Partial` means that at least one normative branch is
implemented, but the complete clause is not. `Not applicable` marks
definitions, notation, encoder-only requirements, and informative text that do
not require a separate decoder procedure.

### Preliminary clauses

- **Not applicable -- Foreword and Introduction.** They define the document
  scope and describe the broadcast additions; they contain no independent
  decoder procedure.
- **Partial -- Clause 1, Scope.** The decoder targets broadcast television
  video, but it does not yet accept every bitstream covered by the scope.
- **Partial -- Clause 2, Normative references.** Applicable rules from
  GB/T 20090.2-2013 are reused, but not every referenced profile combination is
  implemented.
- **Not applicable -- Clause 3, Terms and definitions.** This clause has no
  independent decoder procedure.
- **Not applicable -- 3.1, Reserved.**
- **Not applicable -- 3.2, Variable-length coding.**
- **Not applicable -- 3.3, Transform coefficient.**
- **Not applicable -- 3.4, Coded representation.**
- **Not applicable -- 3.5, Encoding process.**
- **Not applicable -- 3.6, Encoder.**
- **Not applicable -- 3.7, Coded picture.**
- **Not applicable -- 3.8, Flag.**
- **Not applicable -- 3.9, Compensation.**
- **Not applicable -- 3.10, Residual.**
- **Not applicable -- 3.11, Reference index.**
- **Not applicable -- 3.12, Reference picture.**
- **Not applicable -- 3.13, Layer.**
- **Not applicable -- 3.14, Field.**
- **Not applicable -- 3.15, Profile.**
- **Not applicable -- 3.16, Binary bin.**
- **Not applicable -- 3.17, Binary bin string.**
- **Not applicable -- 3.18, Component.**
- **Not applicable -- 3.19, Inverse transform.**
- **Not applicable -- 3.20, Dequantization.**
- **Not applicable -- 3.21, Raster scan.**
- **Not applicable -- 3.22, Macroblock.**
- **Not applicable -- 3.23, Macroblock address.**
- **Not applicable -- 3.24, Macroblock row.**
- **Not applicable -- 3.25, Macroblock position.**
- **Not applicable -- 3.26, Backward prediction.**
- **Not applicable -- 3.27, Partitioning.**
- **Not applicable -- 3.28, Level.**
- **Not applicable -- 3.29, AC coefficient.**
- **Not applicable -- 3.30, Decode processing.**
- **Not applicable -- 3.31, Decoding process.**
- **Not applicable -- 3.32, Decoder.**
- **Not applicable -- 3.33, Decoding order.**
- **Not applicable -- 3.34, Decoded picture.**
- **Not applicable -- 3.35, Decoded picture buffer.**
- **Not applicable -- 3.36, Parsing process.**
- **Not applicable -- 3.37, Forbidden.**
- **Not applicable -- 3.38, Block.**
- **Not applicable -- 3.39, Block scan.**
- **Not applicable -- 3.40, Luma.**
- **Not applicable -- 3.41, Quantization parameter.**
- **Not applicable -- 3.42, Quantized coefficient.**
- **Not applicable -- 3.43, X-profile decoder.**
- **Not applicable -- 3.44, Start code.**
- **Not applicable -- 3.45, Forward prediction.**
- **Not applicable -- 3.46, Forward inter decoded picture (P picture).**
- **Not applicable -- 3.47, Chroma.**
- **Not applicable -- 3.48, Video sequence.**
- **Not applicable -- 3.49, Output reordering delay.**
- **Not applicable -- 3.50, Output process.**
- **Not applicable -- 3.51, Output order.**
- **Not applicable -- 3.52, Bidirectional prediction.**
- **Not applicable -- 3.53, Bidirectional inter decoded picture (B picture).**
- **Not applicable -- 3.54, Random access.**
- **Not applicable -- 3.55, Random access point.**
- **Not applicable -- 3.56, Stuffing bits.**
- **Not applicable -- 3.57, Slice.**
- **Not applicable -- 3.58, Slice header.**
- **Not applicable -- 3.59, Skipped macroblock.**
- **Not applicable -- 3.60, Picture reordering.**
- **Not applicable -- 3.61, Bit string.**
- **Not applicable -- 3.62, Bitstream.**
- **Not applicable -- 3.63, Bitstream buffer.**
- **Not applicable -- 3.64, Bitstream order.**
- **Not applicable -- 3.65, Display order.**
- **Not applicable -- 3.66, Sample.**
- **Not applicable -- 3.67, Sample aspect ratio.**
- **Not applicable -- 3.68, Sample value.**
- **Not applicable -- 3.69, Run.**
- **Not applicable -- 3.70, Prediction.**
- **Not applicable -- 3.71, Prediction process.**
- **Not applicable -- 3.72, Prediction value.**
- **Not applicable -- 3.73, Syntax element.**
- **Not applicable -- 3.74, Source.**
- **Not applicable -- 3.75, Motion vector.**
- **Not applicable -- 3.76, Frame.**
- **Not applicable -- 3.77, Inter coding.**
- **Not applicable -- 3.78, Inter prediction.**
- **Not applicable -- 3.79, Intra coding.**
- **Not applicable -- 3.80, Intra decoded picture (I picture).**
- **Not applicable -- 3.81, Intra prediction.**
- **Not applicable -- 3.82, DC coefficient.**
- **Not applicable -- 3.83, Byte.**
- **Not applicable -- 3.84, Byte alignment.**
- **Not applicable -- Clause 4, Abbreviations.** This is terminology only.

### Clause 5 -- Conventions

- **Not applicable -- 5.1, Overview.** This defines notation.
- **Not applicable -- 5.2, Arithmetic operators.**
- **Not applicable -- Table 1, Arithmetic operators.**
- **Not applicable -- 5.3, Logical operators.**
- **Not applicable -- Table 2, Logical operators.**
- **Not applicable -- 5.4, Relational operators.**
- **Not applicable -- Table 3, Relational operators.**
- **Not applicable -- 5.5, Bitwise operators.**
- **Not applicable -- Table 4, Bitwise operators.**
- **Not applicable -- 5.6, Assignment operators.**
- **Not applicable -- Table 5, Assignment operators.**
- **Not applicable -- 5.7, Mathematical functions.**
- **Not applicable -- 5.8, Structure member operator.**
- **Not applicable -- Table 6, Structure member operator.**
- **Not applicable -- 5.9, Description method.**
- **Not applicable -- 5.9.1, Bitstream syntax notation.**
- **Not applicable -- Table 7, Syntax pseudocode.**
- **Partial -- 5.9.2, Functions.** Equivalent helpers cover implemented units.
- **Not applicable -- 5.9.2.1, Function conventions.**
- **Implemented -- 5.9.2.2, `byte_aligned()`.**
- **Partial -- 5.9.2.3, `next_bits(n)`.** Look-ahead is implemented only where
  the current unit and slice parsers require it.
- **Partial -- 5.9.2.4, `byte_aligned_next_bits(n)`.** Equivalent slice
  termination checks exist for supported entropy paths.
- **Partial -- 5.9.2.5, `next_start_code()`.** Annex-B scanning exists, but all
  video-edit and extension placements are not supported.
- **Implemented -- Table 8, `next_start_code()` definition.**
- **Implemented -- 5.9.2.6, `is_end_of_slice()`.**
- **Implemented -- Table 9, `is_end_of_slice()` definition.**
- **Implemented -- 5.9.2.7, `is_stuffing_pattern()`.**
- **Implemented -- Table 10, `is_stuffing_pattern()` definition.**
- **Implemented -- 5.9.2.8, `read_bits(n)`.**
- **Partial -- 5.9.3, Descriptors.** Fixed, signed, unsigned, Exp-Golomb,
  basic-entropy, and advanced-entropy descriptors needed by the current
  YUV420 path are implemented; unsupported syntax branches remain.
- **Partial -- Table 11, Descriptors.** YUV422-only uses are not integrated.
- **Partial -- 5.9.4.** Marker and forbidden values are validated in parsed
  headers; unparsed extension payloads are retained as raw data.

### Clause 6 -- Coded bitstream structure

- **Partial -- 6.1, Video sequence.**
- **Partial -- 6.1.1, Video-sequence overview.** Sequence boundaries and
  coded-picture ordering exist; arbitrary mid-picture sequence changes do not.
- **Partial -- 6.1.2, Progressive and interlaced sequences.** Successive
  interlaced fields are decoded; progressive sequences are parsed but rejected
  by the broadcast reconstruction path.
- **Partial -- 6.1.3, Sequence headers.** Initial and repeated headers are
  parsed and reported, but random access and every parameter-switch case are
  not complete.
- **Partial -- 6.2, Pictures.**
- **Partial -- 6.2.1, Picture overview.** I/P/B pictures and successive field
  pictures are assembled; frame pictures are not.
- **Partial -- 6.2.2, Picture formats.** Only 4:2:0 is decoded.
- **Implemented -- 6.2.2.1, 4:2:0 format.** The broadcast path allocates,
  predicts, reconstructs, filters, and outputs YUV420P8.
- **Not implemented -- 6.2.2.2, 4:2:2 format.** Headers recognize it, but no
  broadcast picture is reconstructed in it.
- **Implemented -- Figure 1, 4:2:0 luma/chroma sample positions.**
- **Not implemented -- Figure 2, 4:2:2 luma/chroma sample positions.**
- **Implemented -- 6.2.3, Picture types.** I, P, and B picture types are
  decoded on the supported field-picture path.
- **Partial -- 6.2.4, Picture order.** Normal I/P/B display reordering and
  final flush exist; the complete low-delay output rules do not.
- **Partial -- 6.2.5, Reference pictures.** Two-reference field selection,
  edge replacement, and current-picture second-field references exist; frame,
  progressive, and all sequence-switch cases remain.
- **Implemented -- 6.3, Slices.** Multiple row-bounded slices,
  slice-local neighbors, field boundaries, and deferred last-slice completion
  are integrated for the supported YUV420 field path.
- **Implemented -- Figure 3, Slice structure.**
- **Partial -- 6.4, Macroblocks.** Motion partitions are implemented for
  YUV420 field pictures; other picture formats are not integrated.
- **Partial -- Figure 4, Macroblock partitions.** The shown partitions are
  implemented only on the supported picture path.
- **Partial -- 6.5, 8x8 blocks.** Six-block YUV420 ordering is implemented;
  eight-block YUV422 ordering is not.
- **Implemented -- Figure 5, YUV420 block numbering.** Its dispatch order is
  used by macroblock reconstruction.
- **Not implemented -- Figure 6, YUV422 block numbering.** It is not connected
  to broadcast reconstruction.

### Clause 7 -- Syntax and semantics

- **Partial -- 7.1, Syntax description.** Supported unit syntax is parsed.
- **Partial -- 7.1.1, Start codes.** Video-edit behavior remains incomplete.
- **Partial -- Table 12, Start-code values.** Every value is classified, but
  not every unit type has semantics.
- **Partial -- 7.1.2, Video-sequence syntax.**
- **Partial -- 7.1.2.1, Video-sequence definition.** The normal sequence loop
  exists; video-edit and every extension placement do not.
- **Partial -- Table 13, Video-sequence definition.**
- **Implemented -- 7.1.2.2, Sequence-header definition.**
- **Implemented -- Table 14, Sequence-header definition.** All fields are
  bounded and parsed; full level enforcement is tracked under Annex B.
- **Partial -- 7.1.2.3, Extension and user-data definition.** Payloads are
  retained, but extensions are not decoded into typed metadata.
- **Partial -- Table 15, Extension and user-data definition.**
- **Partial -- Table 16, Extension-data definition.** Raw bytes are retained.
- **Implemented -- Table 17, User-data definition.** Bytes are delivered by
  the metadata event.
- **Not implemented -- 7.1.2.4, Sequence-display extension definition.**
- **Not implemented -- Table 18, Sequence-display extension definition.**
- **Not implemented -- 7.1.2.5, Copyright extension definition.**
- **Not implemented -- Table 19, Copyright extension definition.**
- **Not implemented -- 7.1.2.6, Camera-parameters extension definition.**
- **Not implemented -- Table 20, Camera-parameters extension definition.**
- **Partial -- 7.1.3, Picture definition.** Only the supported YUV420
  successive-field path is decoded end to end.
- **Implemented -- 7.1.3.1, I-picture-header definition.**
- **Implemented -- Table 21, I-picture-header definition.**
- **Implemented -- 7.1.3.2, PB-picture-header definition.**
- **Implemented -- Table 22, PB-picture-header definition.**
- **Not implemented -- 7.1.3.3, Picture-display extension definition.**
- **Not implemented -- Table 23, Picture-display extension definition.**
- **Partial -- 7.1.3.4, Picture-data definition.** Progressive and combined
  frame pictures are not reconstructed.
- **Partial -- Table 24, Picture-data definition.**
- **Implemented -- 7.1.3.5, Slice definition.**
- **Implemented -- Table 25, Slice definition.** Header fields, weighted
  prediction, entropy termination, and multi-row continuation are connected.
- **Partial -- 7.1.3.6, Macroblock definition.** YUV422 fields are absent.
- **Partial -- Table 26, Macroblock definition.**
- **Partial -- 7.1.3.7, Block definition.** YUV420 8x8 blocks are parsed.
- **Partial -- Table 27, Block definition.** YUV422 block dispatch is absent.
- **Partial -- 7.2, Semantic description.** Unsupported extension and picture
  structures have no complete semantics.
- **Not implemented -- 7.2.1, Video extensions.** Extension payloads are raw.
- **Not implemented -- Table 28, Video-extension identifiers.** Identifiers
  inside extension payloads are not parsed.
- **Partial -- 7.2.2, Video-sequence semantics.**
- **Partial -- 7.2.2.1, Video sequence.**
- **Partial -- 7.2.2.1.1, Video-edit code.** It is classified without edit or
  random-access state transitions.
- **Implemented -- 7.2.2.1.2, Sequence-end code.** It flushes reordered output.
- **Implemented -- 7.2.2.2, Sequence header.**
- **Implemented -- 7.2.2.2.1, Sequence-start code.**
- **Implemented -- 7.2.2.2.2, Profile identifier.** Profile `0x48` is required.
- **Partial -- 7.2.2.2.3, Level identifier.** Identifiers are recognized;
  resource maxima are not enforced.
- **Partial -- 7.2.2.2.4, Progressive sequence.** Parsed, but not decoded by the
  broadcast reconstruction path.
- **Implemented -- 7.2.2.2.5, Horizontal size.**
- **Implemented -- 7.2.2.2.6, Vertical size.**
- **Implemented -- Figure 7, Picture-boundary geometry.**
- **Partial -- 7.2.2.2.7, Chroma format.** Only 4:2:0 reaches reconstruction.
- **Partial -- Table 29, Chroma formats.** 4:2:2 is recognized but not decoded.
- **Implemented -- 7.2.2.2.8, Sample precision.** Eight-bit precision is
  validated.
- **Implemented -- Table 30, Sample precision values.**
- **Implemented -- 7.2.2.2.9, Aspect ratio.**
- **Implemented -- Table 31, Aspect-ratio identifiers.**
- **Partial -- 7.2.2.2.10, Frame-rate code.** Parsed without complete output
  timing or BBV use.
- **Partial -- Table 32, Frame-rate identifiers.**
- **Partial -- 7.2.2.2.11, Bit-rate low bits.** Parsed without level checks.
- **Partial -- 7.2.2.2.12, Bit-rate high bits.** Combined with the low bits;
  level and BBV limits are not enforced.
- **Partial -- 7.2.2.2.13, Low delay.** Header syntax is constrained, but the
  complete low-delay schedule is absent.
- **Partial -- 7.2.2.2.14, BBV buffer size.** Parsed without BBV simulation.
- **Partial -- 7.2.2.3, Extension and user data.**
- **Partial -- 7.2.2.3.1, Extension data.**
- **Partial -- 7.2.2.3.1.1, Extension start code.** Recognized as a raw unit.
- **Partial -- 7.2.2.3.1.2, Reserved extension byte.** Retained, not typed.
- **Implemented -- 7.2.2.3.2, User data.**
- **Implemented -- 7.2.2.3.2.1, User-data start code.**
- **Implemented -- 7.2.2.3.2.2, User-data byte.** Delivered losslessly.
- **Not implemented -- 7.2.2.4, Sequence-display extension.**
- **Not implemented -- 7.2.2.4.1, Extension identifier.**
- **Not implemented -- 7.2.2.4.2, Video format.**
- **Not implemented -- Table 33, Video-format identifiers.**
- **Not implemented -- 7.2.2.4.3, Sample range.**
- **Not implemented -- 7.2.2.4.4, Colour-description flag.**
- **Not implemented -- 7.2.2.4.5, Colour primaries.**
- **Not implemented -- Table 34, Colour primaries.**
- **Not implemented -- 7.2.2.4.6, Transfer characteristics.**
- **Not implemented -- Table 35, Transfer characteristics.**
- **Not implemented -- 7.2.2.4.7, Matrix coefficients.**
- **Not implemented -- Table 36, Colour conversion matrices.**
- **Not implemented -- 7.2.2.4.8, Horizontal display size.**
- **Not implemented -- 7.2.2.4.9, Vertical display size.**
- **Not implemented -- 7.2.2.4.10, Stereo packing mode.**
- **Not implemented -- Table 37, Stereo packing modes.**
- **Not implemented -- 7.2.2.5, Copyright extension.**
- **Not implemented -- 7.2.2.5.1, Extension identifier.**
- **Not implemented -- 7.2.2.5.2, Copyright flag.**
- **Not implemented -- 7.2.2.5.3, Copyright identifier.**
- **Not implemented -- 7.2.2.5.4, Original-or-copy flag.**
- **Not implemented -- 7.2.2.5.5, Copyright number 1.**
- **Not implemented -- 7.2.2.5.6, Copyright number 2.**
- **Not implemented -- 7.2.2.5.7, Copyright number 3.**
- **Not implemented -- 7.2.2.6, Camera-parameters extension.**
- **Not implemented -- 7.2.2.6.1, Extension identifier.**
- **Not implemented -- 7.2.2.6.2, Camera identifier.**
- **Not implemented -- 7.2.2.6.3, Image-device height.**
- **Not implemented -- 7.2.2.6.4, Focal length.**
- **Not implemented -- 7.2.2.6.5, F-number.**
- **Not implemented -- 7.2.2.6.6, Vertical field of view.**
- **Not implemented -- 7.2.2.6.7, Camera-position high words.**
- **Not implemented -- 7.2.2.6.8, Camera-position low words.**
- **Not implemented -- 7.2.2.6.9, Camera-direction vector.**
- **Not implemented -- 7.2.2.6.10, Image-plane vertical vector.**
- **Not implemented -- Figure 8, Camera model.**
- **Not implemented -- Figure 9, Camera coordinate system.**
- **Partial -- 7.2.3, Picture semantics.**
- **Implemented -- 7.2.3.1, I-picture header.**
- **Implemented -- 7.2.3.1.1, I-picture start code.**
- **Partial -- 7.2.3.1.2, BBV delay.** Parsed without timing evaluation.
- **Partial -- 7.2.3.1.3, BBV-delay extension.** Parsed without timing use.
- **Implemented -- 7.2.3.1.4, Time-code flag.**
- **Implemented -- 7.2.3.1.5, Time code.** Range-checked when present.
- **Implemented -- Table 38, Time-code fields.**
- **Implemented -- 7.2.3.1.6, Picture distance.** Used by motion, DPB, and
  display ordering.
- **Partial -- 7.2.3.1.7, BBV check count.** Parsed without buffer checks.
- **Partial -- 7.2.3.1.8, Progressive frame.** Parsed but not reconstructed.
- **Partial -- 7.2.3.1.9, Picture structure.** Successive fields work; combined
  frame pictures do not.
- **Partial -- 7.2.3.1.10, Top field first.** Supported for successive fields;
  progressive output cases remain.
- **Partial -- 7.2.3.1.11, Repeat first field.** Parsed without all repeat and
  output cases.
- **Implemented -- 7.2.3.1.12, Fixed picture QP.**
- **Implemented -- 7.2.3.1.13, Picture QP.**
- **Implemented -- 7.2.3.1.14, Macroblock-skip mode.**
- **Implemented -- 7.2.3.1.15, Loop-filter disable.**
- **Implemented -- 7.2.3.1.16, Loop-filter parameter flag.**
- **Implemented -- 7.2.3.1.17, Alpha/C index offset.**
- **Implemented -- 7.2.3.1.18, Beta index offset.**
- **Implemented -- 7.2.3.1.19, Weighted-quantization flag.**
- **Implemented -- 7.2.3.1.20, Chroma-QP disable.**
- **Implemented -- 7.2.3.1.21, Cb/Cr chroma-QP deltas.**
- **Implemented -- 7.2.3.1.22, Weighted-quantization parameter index.**
- **Implemented -- 7.2.3.1.23, Weighted-quantization matrix model.**
- **Implemented -- 7.2.3.1.24, Weighted-quantization parameter deltas.**
- **Implemented -- 7.2.3.1.25, Advanced-entropy enable.**
- **Implemented -- 7.2.3.2, PB-picture header.**
- **Implemented -- 7.2.3.2.1, PB-picture start code.**
- **Implemented -- 7.2.3.2.2, Picture coding type.**
- **Implemented -- Table 39, Picture coding types.**
- **Implemented -- 7.2.3.2.3, Advanced-prediction-mode disable.**
- **Implemented -- 7.2.3.2.4, Picture-reference flag.**
- **Implemented -- 7.2.3.2.5, No-forward-reference flag.**
- **Implemented -- 7.2.3.2.6, PB field-enhanced prediction flag.**
- **Not implemented -- 7.2.3.3, Picture-display extension.**
- **Not implemented -- 7.2.3.3.1, Extension identifier.**
- **Not implemented -- 7.2.3.3.2, Horizontal frame-centre offset.**
- **Not implemented -- 7.2.3.3.3, Vertical frame-centre offset.**
- **Not implemented -- Figure 10, Frame-centre offset geometry.**
- **Implemented -- 7.2.4, Slice semantics.**
- **Implemented -- 7.2.4.1, Slice start code.**
- **Implemented -- 7.2.4.2, Slice vertical position.**
- **Implemented -- 7.2.4.3, Slice vertical-position extension.**
- **Implemented -- 7.2.4.4, Fixed slice QP.**
- **Implemented -- 7.2.4.5, Slice QP.**
- **Implemented -- 7.2.4.6, Slice weighted-prediction flag.**
- **Implemented -- 7.2.4.7, Luma scale.**
- **Implemented -- 7.2.4.8, Luma shift.**
- **Implemented -- 7.2.4.9, Chroma scale.**
- **Implemented -- 7.2.4.10, Chroma shift.**
- **Implemented -- 7.2.4.11, Macroblock weighted-prediction flag.**
- **Implemented -- 7.2.4.12, AEC byte-alignment stuffing bit.**
- **Implemented -- 7.2.4.13, Skipped-macroblock run.**
- **Implemented -- 7.2.4.14, AEC macroblock stuffing bit.**
- **Partial -- 7.2.5, Macroblock semantics.** YUV422 is not integrated.
- **Implemented -- 7.2.5.1, Macroblock type.**
- **Implemented -- 7.2.5.2, Macroblock partition type.**
- **Implemented -- 7.2.5.3, Prediction-mode flag.**
- **Implemented -- 7.2.5.4, Intra-luma prediction mode.**
- **Implemented -- 7.2.5.5, Intra-chroma prediction mode.**
- **Not implemented -- 7.2.5.6, Extra YUV422 intra-chroma mode.**
- **Implemented -- 7.2.5.7, Macroblock reference index.**
- **Implemented -- 7.2.5.8, Motion-vector differences.**
- **Implemented -- 7.2.5.9, Weighted-prediction flag.**
- **Implemented -- 7.2.5.10, Coded block pattern.**
- **Not implemented -- 7.2.5.11, YUV422 coded block pattern.**
- **Implemented -- 7.2.5.12, Macroblock QP delta.**
- **Partial -- 7.2.6, Block semantics.** YUV420 8x8 blocks are integrated.
- **Implemented -- 7.2.6.1, Transform coefficient.**
- **Implemented -- 7.2.6.2, Escape-level difference.**

### Clause 8 -- Parsing process

- **Implemented -- 8.1, Order-k Exp-Golomb parsing.** Bounded readers cover
  arbitrary supported orders.
- **Implemented -- Table 40, Order-k Exp-Golomb code structure.**
- **Partial -- 8.2, `ue(v)`, `se(v)`, and `me(v)` parsing.** YUV422 `me(v)` is
  not connected to a complete macroblock path.
- **Implemented -- Table 41, Signed value to `CodeNum` mapping.**
- **Implemented -- Table 42, YUV420 `MbCBP` mapping.**
- **Not implemented -- Table 43, YUV422 `MbCBP422` mapping.** The mapping is
  not connected to broadcast macroblock reconstruction.
- **Implemented -- 8.3, `ce(v)`.** Basic-entropy macroblock and coefficient VLC
  parsing is integrated for supported YUV420 field pictures.
- **Implemented -- 8.4, `ae(v)` parsing.**
- **Implemented -- 8.4.1, AEC overview.** Syntax values are assembled from
  bounded bin strings.
- **Implemented -- 8.4.2, Initialization.**
- **Implemented -- 8.4.2.1, Context-model initialization.**
- **Implemented -- 8.4.2.2, Arithmetic-decoder initialization.**
- **Partial -- 8.4.3, Binarization.** YUV422-only syntax is not integrated.
- **Implemented -- Table 44, Skip/type and intra-chroma binarization.**
- **Implemented -- Table 45, B-picture macroblock-type binarization.**
- **Implemented -- Table 46, Macroblock partition-type binarization.**
- **Implemented -- Table 47, YUV420 intra-chroma-mode binarization.**
- **Not implemented -- Table 48, Extra YUV422 intra-chroma binarization.**
- **Implemented -- Table 49, Motion-vector-difference binarization.**
- **Implemented -- Table 50, QP-delta binarization.**
- **Implemented -- 8.4.4, Binary-bin-string parsing.**
- **Implemented -- 8.4.4.1, Overview.** Regular and bypass
  bins are selected and accumulated with bounds.
- **Partial -- 8.4.4.2, Context-index selection.** All contexts
  needed by the YUV420 I/P/B path are implemented; YUV422-only syntax is not.
- **Partial -- Table 51, Syntax-element context start indices.** Entries used
  by YUV420 are implemented; YUV422-only entries are not integrated.
- **Implemented -- Table 52, `priIdx` to `lMax` mapping.**
- **Implemented -- Table 53, `secIdx` coefficient context mapping.**
- **Implemented -- 8.4.4.3, Binary-bin parsing.**
- **Implemented -- 8.4.4.3.1, Parsing process.**
- **Implemented -- 8.4.4.3.2, Regular decision process.**
- **Implemented -- 8.4.4.3.3, Bypass process.**
- **Implemented -- 8.4.4.3.4, AEC stuffing-bit process.**
- **Implemented -- 8.4.4.3.5, Context update.**

### Clause 9 -- Decoding process

- **Partial -- 9.1, High-level syntax structures.** Normal sequence, picture,
  slice, extension/user-data events, and end handling exist; video-edit,
  progressive/frame pictures, and complete sequence switching remain.
- **Partial -- 9.2, Picture-header decoding.** I/P/B headers initialize the
  supported successive-field path; other picture structures stop before
  reconstruction.
- **Implemented -- 9.3, Slice decoding.** Multiple slices per field are
  row-bounded, use slice-local neighbors, and commit completed fields.
- **Partial -- 9.4, Macroblock decoding.** YUV422 is not integrated.
- **Implemented -- 9.4.1, Initialization.** Address, position,
  entropy state, QP, prediction, and per-slice neighbor state are initialized.
- **Implemented -- 9.4.2, Macroblock types.** Current YUV420
  I/P/B types, subtypes, partitions, skip, direct, and symmetric modes map to
  the internal macroblock contract.
- **Implemented -- Table 54, `mb_type` to `MbTypeIndex` mapping.**
- **Implemented -- Table 55, P-picture macroblock types.**
- **Implemented -- Table 56, B-picture macroblock types.**
- **Implemented -- Table 57, B_8x8 subtypes.**
- **Implemented -- 9.4.3, Neighbor blocks.** Geometry, availability, slice
  boundaries, and field rows are represented.
- **Implemented -- Figure 11, Current and neighboring block positions.**
- **Implemented -- Figure 12, Current and neighboring macroblock positions.**
- **Implemented -- Table 58, Neighbor-block positions.**
- **Implemented -- 9.4.4, Intra-prediction modes.**
- **Implemented -- 9.4.4.1, Overview.**
- **Implemented -- 9.4.4.2, 8x8 intra-mode derivation.**
- **Implemented -- Table 59, 8x8 luma intra modes.**
- **Implemented -- Table 60, 8x8 chroma intra modes.**
- **Implemented -- Figure 13, 8x8 luma intra modes.**
- **Partial -- 9.4.5, Reference-picture selection.** Supported field-picture
  topologies, default references, and
  enhanced/no-forward branches are implemented; frame/progressive topologies
  are not integrated.
- **Partial -- Figure 14, Reference-index marking method 1.**
- **Partial -- Figure 15, Reference-index marking method 2.**
- **Partial -- Figure 16, Reference-index marking method 3.**
- **Partial -- Figure 17, Reference-index marking method 4.**
- **Partial -- Figure 18, Reference-index marking method 5.**
- **Partial -- Figure 19, Reference-index marking method 6.**
- **Partial -- Figure 20, Reference-index marking method 7.**
- **Implemented -- 9.4.6, Motion vectors.**
- **Implemented -- 9.4.6.1, Overview.**
- **Implemented -- 9.4.6.2, Luma motion-vector prediction.** Distance
  normalization, spatial prediction, partition shortcuts, scaling, and MVD
  addition are used by supported pictures.
- **Implemented -- Figure 21, 8x16 and 16x8 motion prediction.**
- **Implemented -- 9.4.6.3, Luma motion-vector decoding.**
- **Partial -- 9.4.7, Macroblock coding pattern.** Six YUV420 blocks are
  dispatched; the two extra YUV422 blocks are not.
- **Implemented -- 9.4.8, Quantization parameters.** Picture, slice, predicted
  macroblock, luma, and chroma QPs are range-checked and propagated.
- **Implemented -- 9.4.9, Weighted-quantization matrix.** Default and signaled
  parameter models produce the 8x8 matrix used by reconstruction.
- **Implemented -- 9.5, Block decoding.**
- **Implemented -- 9.5.1, Basic-entropy block decoding.** Annex D VLC tables,
  EOB, escape, run, level, and reverse-run rules feed inverse scan.
- **Implemented -- 9.5.2, Advanced-entropy block decoding.** Context-coded
  run/level data feeds the same bounded coefficient contract.
- **Implemented -- 9.5.3, Inverse scan.** Frame and field 8x8
  scan orders are selected by picture structure.
- **Implemented -- Figure 22, 8x8 inverse scan method 1.**
- **Implemented -- Figure 23, 8x8 inverse scan method 2.**
- **Implemented -- 9.6, Inverse quantization.**
- **Implemented -- 9.6.1, QP determination.** Luma and YUV420
  chroma QPs are selected with the normative chroma mapping.
- **Implemented -- Table 61, Chroma QP mapping.**
- **Implemented -- 9.6.2, Inverse-quantization process.** Default and
  weighted 8x8 inverse quantization are in the reconstruction path.
- **Implemented -- Table 62, QP inverse-quantization parameters.**
- **Implemented -- 9.7, Inverse transform.** The scalar normative 8x8 integer
  inverse transform is applied to decoded blocks.
- **Implemented -- 9.8, Intra prediction.**
- **Implemented -- 9.8.1, Overview.**
- **Implemented -- 9.8.2, 8x8 reference-sample acquisition.**
- **Implemented -- 9.8.3, 8x8 luma intra prediction.**
- **Implemented -- 9.8.4, 8x8 chroma intra prediction.**
- **Partial -- 9.9, Inter prediction.** Frame-picture and YUV422 integration
  remain.
- **Partial -- 9.9.1, Luma motion-vector derivation.** P_Skip, B_Direct,
  symmetric, co-located, same-polarity skip,
  and enhanced-field math are implemented; frame-picture variants are not
  integrated end to end.
- **Partial -- Figure 24, Direct-mode derivation 1.**
- **Partial -- Figure 25, Direct-mode derivation 2.**
- **Partial -- Figure 26, Direct-mode derivation 3.**
- **Partial -- Figure 27, Direct-mode derivation 4.**
- **Partial -- Figure 28, Symmetric-mode derivation 1.**
- **Partial -- Figure 29, Symmetric-mode derivation 2.**
- **Partial -- 9.9.2, Reference-sample derivation.** YUV422 is not integrated.
- **Implemented -- 9.9.2.1, Overview.**
- **Implemented -- 9.9.2.2, Luma interpolation.** Edge replacement and every
  quarter-sample phase are used by motion compensation.
- **Implemented -- Figure 30, Integer, half-, and quarter-sample positions.**
- **Implemented -- Table 63, Predicted luma sample selection.**
- **Partial -- 9.9.2.3, Chroma interpolation.** Every
  eighth-sample phase and edge replacement are implemented for YUV420; YUV422
  is not integrated.
- **Partial -- Figure 31, Chroma interpolation.** Implemented for YUV420 only.
- **Implemented -- 9.9.3, Weighted prediction.** Slice/macroblock flag
  selection, scale/shift, clipping, and P/B combination are connected.
- **Implemented -- 9.10, Reconstruction.** Intra, single-reference, and
  bidirectional prediction are combined with residuals using eight-bit
  clipping.
- **Partial -- 9.11, Loop filtering.** YUV420 field filtering is integrated;
  YUV422 is not reached by broadcast decoding.
- **Partial -- 9.11.1, Overview and traversal.** YUV420 vertical/horizontal
  traversal and slice-boundary exclusion are integrated.
- **Implemented -- Figure 32, YUV420 boundaries to filter.**
- **Not implemented -- Figure 33, YUV422 boundaries to filter.** Generic
  geometry exists, but the broadcast path never reaches it.
- **Implemented -- 9.11.2, Boundary strength.** Intra, reference-index, and
  motion-vector rules derive `Bs` for P/B and predicted second fields.
- **Implemented -- 9.11.3, Boundary thresholds.**
- **Implemented -- Figure 34, Samples around an 8x8 boundary.**
- **Implemented -- Table 64, Alpha and beta thresholds.**
- **Implemented -- 9.11.4, `Bs == 2` filtering.** Strong luma/chroma kernels
  are applied in the field completion path.
- **Implemented -- 9.11.5, `Bs == 1` filtering.** Normal kernels use the
  normative clipping table and signed rounding rules.
- **Implemented -- Table 65, Filter clipping parameter.**

### Annexes

- **Implemented -- Annex A, Pseudo-start codes.** Removal and bit repacking are
  used for syntax payloads and have focused boundary tests. Annex A contains no
  numbered figure or table.
- **Not applicable -- B.1, Overview.** It defines the meaning of profiles and
  levels.
- **Partial -- B.2, Profiles.** Profile `0x48`, eight-bit sample
  precision, supported level identifiers, and current format restrictions are
  checked; the whole broadcast-profile subset is not yet decodable.
- **Partial -- Table B.1, Profiles.**
- **Partial -- B.3, Levels.** Identifiers are recognized; limits are not.
- **Partial -- B.3.1, Defined levels.** Identifiers are
  recognized, but recognition alone is not level conformance.
- **Partial -- Table B.2, Defined levels.**
- **Not implemented -- B.3.2, Level-independent and per-level limits.**
  Macroblock bits, dimensions,
  sample rate, bit rate, bin count, BBV size, motion range, and format maxima
  are not enforced.
- **Not implemented -- Table B.3, Maximum coded bits per macroblock.**
- **Not implemented -- Table B.4, Level 2 limits.**
- **Not implemented -- Table B.5, Level 4 limits.**
- **Not implemented -- Table B.6, Level 4.2 limits.**
- **Not implemented -- Table B.7, Level 6.0/6.1 limits.**
- **Not implemented -- Table B.8, Level 6.3/6.5/6.2 limits.**
- **Not implemented -- C.1, BBV overview.** No buffer verifier exists.
- **Not implemented -- C.2, Conventions.**
- **Not implemented -- C.2.1, Clock convention.**
- **Not implemented -- C.2.2, Buffer-size convention.**
- **Not implemented -- C.2.3, Maximum input-rate convention.**
- **Not implemented -- C.3, Basic operations.**
- **Not implemented -- C.3.1, Data input.**
- **Not implemented -- C.3.1.1, Input method one.**
- **Not implemented -- C.3.1.1.1, Input process.**
- **Not implemented -- Figure C.1, BBV occupancy model one.**
- **Not implemented -- C.3.1.1.2, Sequence-start ambiguity.**
- **Not implemented -- C.3.1.1.3, Sequence-end ambiguity.**
- **Not implemented -- C.3.1.2, Input method two.**
- **Not implemented -- Figure C.2, BBV occupancy model two.**
- **Not implemented -- C.3.2, Data removal.**
- **Not implemented -- C.3.2.1, Non-low-delay removal.**
- **Not implemented -- C.3.2.2, Low-delay removal.**
- **Not implemented -- C.4, Buffer check intervals.**
- **Not implemented -- C.4.1, Non-low-delay check intervals.**
- **Not implemented -- C.4.2, Low-delay check intervals.**
- **Implemented -- Annex D, Basic-entropy VLC tables.** Every entry, EOB,
  escape, and maximum-run mapping has exhaustive table tests.
- **Implemented -- Table D.1, `VLC0_Intra`.**
- **Implemented -- Table D.2, `VLC1_Intra`.**
- **Implemented -- Table D.3, `VLC2_Intra`.**
- **Implemented -- Table D.4, `VLC3_Intra`.**
- **Implemented -- Table D.5, `VLC4_Intra`.**
- **Implemented -- Table D.6, `VLC5_Intra`.**
- **Implemented -- Table D.7, `VLC6_Intra`.**
- **Implemented -- Table D.8, `VLC0_Inter`.**
- **Implemented -- Table D.9, `VLC1_Inter`.**
- **Implemented -- Table D.10, `VLC2_Inter`.**
- **Implemented -- Table D.11, `VLC3_Inter`.**
- **Implemented -- Table D.12, `VLC4_Inter`.**
- **Implemented -- Table D.13, `VLC5_Inter`.**
- **Implemented -- Table D.14, `VLC6_Inter`.**
- **Implemented -- Table D.15, `VLC0_Chroma`.**
- **Implemented -- Table D.16, `VLC1_Chroma`.**
- **Implemented -- Table D.17, `VLC2_Chroma`.**
- **Implemented -- Table D.18, `VLC3_Chroma`.**
- **Implemented -- Table D.19, `VLC4_Chroma`.**
- **Implemented -- Table D.20, `CurrentLevel` to `MaxRun` mapping.**
- **Not applicable -- E.1, Overview.** Annex E is an informative alternative
  description of Clause 8.4, not an additional decoder requirement.
- **Implemented -- E.2, AEC initialization reference method.** It is
  cross-checked through the normative Clause 8.4 path.
- **Implemented -- E.3, Regular-decision reference method.**
- **Implemented -- E.4, Bypass reference method.**
- **Implemented -- E.5, Stuffing-bin reference method.**

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
