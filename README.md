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

- **Partial -- 7.1 and Table 12, Start codes.** Every listed value is
  classified, and sequence, picture, slice, extension, user-data, and end
  units are recognized. Video-edit and reserved-unit semantics are incomplete.
- **Partial -- 7.1.2.1 and Table 13, Video-sequence definition.** The normal
  sequence/header/picture/end loop exists; video-edit and every extension
  placement do not.
- **Implemented -- 7.1.2.2 and Table 14, Sequence-header definition.** All
  header fields are bounded and parsed; their full level semantics are tracked
  separately below.
- **Partial -- 7.1.2.3 and Tables 15-17, Extension and user data.** Payloads are
  copied into API events with stable ownership, but extensions are not decoded
  into typed metadata.
- **Not implemented -- 7.1.2.4 and Table 18, Sequence-display extension.** The
  unit is retained only as a raw extension.
- **Not implemented -- 7.1.2.5 and Table 19, Copyright extension.** The unit is
  retained only as a raw extension.
- **Not implemented -- 7.1.2.6 and Table 20, Camera-parameters extension.** The
  unit is retained only as a raw extension.
- **Implemented -- 7.1.3.1 and Table 21, I-picture header definition.** The
  broadcast fields, including weighted quantization and entropy selection, are
  parsed and validated.
- **Implemented -- 7.1.3.2 and Table 22, PB-picture header definition.** P/B,
  reference, enhanced-field, weighted-quantization, and entropy fields are
  parsed and validated.
- **Not implemented -- 7.1.3.3 and Table 23, Picture-display extension.** The
  unit is retained only as a raw extension.
- **Partial -- 7.1.3.4 and Table 24, Picture data.** Supported I/P/B successive
  fields are assembled; progressive and frame pictures are not.
- **Implemented -- 7.1.3.5 and Table 25, Slice definition.** Header fields,
  basic/AEC termination, weighted-prediction parameters, and multi-row slice
  continuation are connected for the supported path.
- **Partial -- 7.1.3.6 and Table 26, Macroblock definition.** Basic and
  advanced YUV420 I/P/B macroblocks are parsed; YUV422 fields are not.
- **Implemented -- 7.1.3.7 and Table 27, Block definition.** Basic and advanced
  8x8 coefficient syntax is parsed on the supported YUV420 path.
- **Partial -- 7.2.1 and Table 28, Video extensions.** Extension identifiers
  are recognized and raw bytes are retained; typed extension semantics are not.
- **Partial -- 7.2.2.1.1, Video-edit code.** The start code is classified, but
  edit/random-access state transitions are not implemented.
- **Implemented -- 7.2.2.1.2, Sequence-end code.** It flushes reordered output
  and emits the end event.
- **Implemented -- 7.2.2.2.1, Sequence-start code.** It dispatches the sequence
  header parser.
- **Implemented -- 7.2.2.2.2, Profile identifier.** Profile `0x48` is
  recognized and required by the broadcast path.
- **Partial -- 7.2.2.2.3, Level identifier.** Listed level identifiers are
  recognized, but Annex B resource maxima are not enforced.
- **Partial -- 7.2.2.2.4, Progressive sequence.** The flag is parsed and its
  header constraints are checked; progressive broadcast decoding is absent.
- **Implemented -- 7.2.2.2.5 and 7.2.2.2.6, Horizontal and vertical size.**
  Dimensions are validated and used to allocate coded/display geometry.
- **Implemented -- Figure 7.** Coded padding and display-boundary geometry are
  represented for the supported format.
- **Partial -- 7.2.2.2.7 and Table 29, Chroma format.** Values are validated;
  only 4:2:0 reaches broadcast reconstruction.
- **Implemented -- 7.2.2.2.8 and Table 30, Sample precision.** The required
  eight-bit value is validated and represented by YUV420P8/YUV422P8 formats.
- **Implemented -- 7.2.2.2.9 and Table 31, Aspect ratio.** Valid identifiers
  are parsed and exposed as sequence metadata.
- **Partial -- 7.2.2.2.10 and Table 32, Frame rate.** Valid identifiers are
  parsed; complete output timing and BBV use are absent.
- **Partial -- 7.2.2.2.11 and 7.2.2.2.12, Bit rate.** Both fields are combined
  and validated; level and BBV limits are not enforced.
- **Partial -- 7.2.2.2.13, Low delay.** The flag is parsed and constrains header
  syntax; complete low-delay scheduling is absent.
- **Partial -- 7.2.2.2.14, BBV buffer size.** The size is parsed and exposed;
  buffer conformance is not checked.
- **Partial -- 7.2.2.3.1.1-7.2.2.3.1.2, Extension data.** Start codes and raw
  reserved bytes are retained, without typed interpretation.
- **Implemented -- 7.2.2.3.2.1-7.2.2.3.2.2, User data.** The start code and
  following bytes are delivered losslessly through the metadata event.
- **Not implemented -- 7.2.2.4.1-7.2.2.4.10 and Tables 33-37,
  Sequence-display semantics.** Video format, range, colour description,
  display size, and stereo packing are not parsed.
- **Not implemented -- 7.2.2.5.1-7.2.2.5.7, Copyright semantics.** The payload
  is not interpreted.
- **Not implemented -- 7.2.2.6.1-7.2.2.6.10, Camera semantics.** Camera
  geometry is not interpreted.
- **Not implemented -- Figures 8 and 9.** Camera model and coordinate-system
  semantics have no typed representation.
- **Implemented -- 7.2.3.1.1, I-picture start code.** It begins an I picture.
- **Partial -- 7.2.3.1.2-7.2.3.1.3, BBV delay.** Both parts are parsed; BBV
  timing is not evaluated.
- **Implemented -- 7.2.3.1.4-7.2.3.1.5 and Table 38, Time code.** Optional time
  codes are parsed and range-checked.
- **Implemented -- 7.2.3.1.6, Picture distance.** It is used by motion
  derivation, reference management, and display ordering.
- **Partial -- 7.2.3.1.7, BBV check count.** It is parsed for low-delay syntax;
  no buffer checks are run.
- **Partial -- 7.2.3.1.8, Progressive frame.** It is parsed and constrained;
  progressive frames are not reconstructed by the broadcast path.
- **Partial -- 7.2.3.1.9, Picture structure.** Successive field pictures are
  reconstructed; combined frame pictures are not.
- **Partial -- 7.2.3.1.10-7.2.3.1.11, Field output controls.** Top-field-first
  and repeat-first-field are parsed; all progressive repeat/output cases are
  not implemented.
- **Implemented -- 7.2.3.1.12-7.2.3.1.14, Picture QP and skip controls.** Fixed
  QP, picture QP, and same-polarity field skip mode feed macroblock decoding.
- **Implemented -- 7.2.3.1.15-7.2.3.1.18, Loop-filter controls.** Disable,
  parameter, alpha/C, and beta fields feed the integrated field filter.
- **Implemented -- 7.2.3.1.19-7.2.3.1.24, Weighted-quantization controls.**
  Chroma deltas, parameter sets, models, and both delta arrays build the 8x8
  matrix used by inverse quantization.
- **Implemented -- 7.2.3.1.25, Advanced-entropy enable.** It selects the AEC or
  basic-entropy macroblock path.
- **Implemented -- 7.2.3.2.1-7.2.3.2.6 and Table 39, PB-picture semantics.**
  P/B type, mandatory advanced-mode disable, reference flags, no-forward
  reference, and enhanced-field prediction feed the supported field path.
- **Not implemented -- 7.2.3.3.1-7.2.3.3.3, Figure 10, Picture-display
  semantics.** Frame-centre offsets and pan-scan geometry are not parsed.
- **Implemented -- 7.2.4.1-7.2.4.5, Slice position and QP.** Start row,
  extended row, fixed QP, and slice QP are parsed and bound reconstruction.
- **Implemented -- 7.2.4.6-7.2.4.11, Weighted-prediction syntax.** Slice flags,
  luma/chroma scales and shifts, and macroblock selection feed P/B prediction.
- **Implemented -- 7.2.4.12-7.2.4.14, AEC alignment, skip run, and stuffing.**
  Both entropy paths terminate without consuming the next slice.
- **Implemented -- 7.2.5.1-7.2.5.5, YUV420 macroblock type and intra modes.**
  The fields are parsed in both basic and advanced entropy modes.
- **Not implemented -- 7.2.5.6, Extra YUV422 intra-chroma mode.** No YUV422
  macroblock reaches reconstruction.
- **Implemented -- 7.2.5.7-7.2.5.10, Reference, MVD, weighting, and CBP.** They
  feed motion derivation, weighted prediction, and six-block dispatch.
- **Not implemented -- 7.2.5.11, YUV422 CBP.** Table-driven parsing exists only
  as a lower-level mapping; the two extra blocks are not decoded end to end.
- **Implemented -- 7.2.5.12, Macroblock QP delta.** Prediction, range checking,
  and update state are connected in both entropy modes.
- **Implemented -- 7.2.6.1-7.2.6.2, Transform and escape coefficients.** Basic
  VLC and AEC coefficient forms are decoded with bounds and atomic failure.

### Clause 8 -- Parsing process

- **Implemented -- 8.1 and Table 40, Order-k Exp-Golomb.** Bounded unsigned and
  signed readers cover the orders used by broadcast syntax.
- **Partial -- 8.2 and Tables 41-43, `ue(v)`, `se(v)`, and `me(v)`.** Signed
  mapping and YUV420 Table 42 CBP are implemented; Table 43 YUV422 CBP is not
  connected to a complete macroblock path.
- **Implemented -- 8.3, `ce(v)`.** Basic-entropy macroblock and coefficient VLC
  parsing is integrated for supported YUV420 field pictures.
- **Implemented -- 8.4.1, AEC overview.** Syntax values are assembled from
  bounded bin strings.
- **Implemented -- 8.4.2.1-8.4.2.2, Initialization.** All contexts and the
  arithmetic decoder are initialized per slice without global state.
- **Partial -- 8.4.3 and Tables 44-50, Binarization.** Tables 44-47 and 49-50
  are implemented; Table 48's extra YUV422 chroma mode is not integrated.
- **Implemented -- 8.4.4.1, Bin-string parsing overview.** Regular and bypass
  bins are selected and accumulated with bounds.
- **Partial -- 8.4.4.2 and Table 51, Context-index selection.** All contexts
  needed by the YUV420 I/P/B path are implemented; YUV422-only syntax is not.
- **Implemented -- Tables 52 and 53.** Coefficient position and context
  selection drive advanced-entropy coefficient parsing.
- **Implemented -- 8.4.4.3.1-8.4.4.3.5.** Regular decision, bypass, AEC
  stuffing, and context update processes are implemented and tested.

### Clause 9 -- Decoding process

- **Partial -- 9.1, High-level syntax structures.** Normal sequence, picture,
  slice, extension/user-data events, and end handling exist; video-edit,
  progressive/frame pictures, and complete sequence switching remain.
- **Partial -- 9.2, Picture-header decoding.** I/P/B headers initialize the
  supported successive-field path; other picture structures stop before
  reconstruction.
- **Implemented -- 9.3, Slice decoding.** Multiple slices per field are
  row-bounded, use slice-local neighbors, and commit completed fields.
- **Implemented -- 9.4.1, Macroblock initialization.** Address, position,
  entropy state, QP, prediction, and per-slice neighbor state are initialized.
- **Implemented -- 9.4.2 and Tables 54-57, Macroblock types.** Current YUV420
  I/P/B types, subtypes, partitions, skip, direct, and symmetric modes map to
  the internal macroblock contract.
- **Implemented -- 9.4.3, Figures 11-12, and Table 58, Neighbor blocks.**
  Geometry, availability, slice boundaries, and field rows are represented.
- **Implemented -- 9.4.4.1-9.4.4.2, Tables 59-60, and Figure 13, Intra-mode
  derivation.** All specified 8x8 luma and YUV420 chroma modes are derived.
- **Partial -- 9.4.5 and Figures 14, 15, 16, 17, 18, 19, and 20, Reference
  selection.** Supported field-picture topologies, default references, and
  enhanced/no-forward branches are implemented; frame/progressive topologies
  are not integrated.
- **Implemented -- 9.4.6.1-9.4.6.3 and Figure 21, Motion vectors.** Distance
  normalization, spatial prediction, partition shortcuts, scaling, and MVD
  addition are used by supported pictures.
- **Partial -- 9.4.7, Macroblock coding pattern.** Six YUV420 blocks are
  dispatched; the two extra YUV422 blocks are not.
- **Implemented -- 9.4.8, Quantization parameters.** Picture, slice, predicted
  macroblock, luma, and chroma QPs are range-checked and propagated.
- **Implemented -- 9.4.9, Weighted-quantization matrix.** Default and signaled
  parameter models produce the 8x8 matrix used by reconstruction.
- **Implemented -- 9.5.1, Basic-entropy block decoding.** Annex D VLC tables,
  EOB, escape, run, level, and reverse-run rules feed inverse scan.
- **Implemented -- 9.5.2, Advanced-entropy block decoding.** Context-coded
  run/level data feeds the same bounded coefficient contract.
- **Implemented -- 9.5.3 and Figures 22-23, Inverse scan.** Frame and field 8x8
  scan orders are selected by picture structure.
- **Implemented -- 9.6.1 and Table 61, QP determination.** Luma and YUV420
  chroma QPs are selected with the normative chroma mapping.
- **Implemented -- 9.6.2 and Table 62, Inverse quantization.** Default and
  weighted 8x8 inverse quantization are in the reconstruction path.
- **Implemented -- 9.7, Inverse transform.** The scalar normative 8x8 integer
  inverse transform is applied to decoded blocks.
- **Implemented -- 9.8.1-9.8.4, Intra prediction.** Reference acquisition,
  unavailable-sample rules, five luma modes, and four YUV420 chroma modes feed
  reconstruction.
- **Partial -- 9.9.1 and Figures 24, 25, 26, 27, 28, and 29, Inter motion
  derivation.** P_Skip, B_Direct, symmetric, co-located, same-polarity skip,
  and enhanced-field math are implemented; frame-picture variants are not
  integrated end to end.
- **Implemented -- 9.9.2.1-9.9.2.2, Figure 30, and Table 63, Luma reference
  samples.** Edge replacement and every quarter-sample luma phase are used by
  motion compensation.
- **Partial -- 9.9.2.3 and Figure 31, Chroma reference samples.** Every
  eighth-sample phase and edge replacement are implemented for YUV420; YUV422
  is not integrated.
- **Implemented -- 9.9.3, Weighted prediction.** Slice/macroblock flag
  selection, scale/shift, clipping, and P/B combination are connected.
- **Implemented -- 9.10, Reconstruction.** Intra, single-reference, and
  bidirectional prediction are combined with residuals using eight-bit
  clipping.
- **Partial -- 9.11.1 and Figures 32-33, Filter traversal.** Figure 32 YUV420
  vertical/horizontal traversal and slice-boundary exclusion are integrated;
  Figure 33 YUV422 traversal is present only as generic lower-level geometry
  and is not reached by broadcast decoding.
- **Implemented -- 9.11.2, Boundary strength.** Intra, reference-index, and
  motion-vector rules derive `Bs` for P/B and predicted second fields.
- **Implemented -- 9.11.3, Figure 34, and Table 64, Thresholds.** Sample
  geometry and normative alpha/beta lookup tables are used by filtering.
- **Implemented -- 9.11.4, `Bs == 2` filtering.** Strong luma/chroma kernels
  are applied in the field completion path.
- **Implemented -- 9.11.5 and Table 65, `Bs == 1` filtering.** Normal kernels
  use the normative clipping table and signed rounding rules.

### Annexes

- **Implemented -- Annex A, Pseudo-start codes.** Removal and bit repacking are
  used for syntax payloads and have focused boundary tests. Annex A contains no
  numbered figure or table.
- **Not applicable -- B.1, Overview.** It defines the meaning of profiles and
  levels.
- **Partial -- B.2 and Table B.1, Profiles.** Profile `0x48`, eight-bit sample
  precision, supported level identifiers, and current format restrictions are
  checked; the whole broadcast-profile subset is not yet decodable.
- **Partial -- B.3.1 and Table B.2, Defined levels.** Identifiers are
  recognized, but recognition alone is not level conformance.
- **Not implemented -- B.3.2 and Tables B.3, B.4, B.5, B.6, B.7, and B.8,
  Level-independent and per-level limits.** Macroblock bits, dimensions,
  sample rate, bit rate, bin count, BBV size, motion range, and format maxima
  are not enforced.
- **Not implemented -- C.1; C.2.1-C.2.3; C.3.1.1.1-C.3.1.1.3; C.3.1.2;
  C.3.2.1-C.3.2.2; and C.4.1-C.4.2, Bitstream buffer verifier.** No BBV input,
  removal, occupancy, underflow/overflow, large-picture, or check-interval
  simulation exists.
- **Not implemented -- Figures C.1 and C.2.** Neither BBV occupancy model is
  represented.
- **Implemented -- Annex D and Tables D.1, D.2, D.3, D.4, D.5, D.6, D.7,
  D.8, D.9, D.10, D.11, D.12, D.13, D.14, D.15, D.16, D.17, D.18, D.19, and
  D.20.** Every basic-entropy VLC entry, EOB, escape, and maximum-run mapping is
  present and exercised by exhaustive table tests.
- **Not applicable -- E.1, Overview.** Annex E is an informative alternative
  description of Clause 8.4, not an additional decoder requirement.
- **Implemented -- E.2-E.5.** The AEC initializer, regular decision, bypass,
  and stuffing-bin behavior are implemented through the normative Clause 8.4
  path and cross-checked against these informative procedures.

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
