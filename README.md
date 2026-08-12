# libcavs

libcavs is an independent C99 reimplementation of the video decoders defined
by GB/T 20090.2-2013 baseline profile `0x20` and GB/T 20090.16-2016 broadcast
profile `0x48`. The rewrite does not provide compatibility with any earlier
libcavs API or source tree.

The current `0.1.0-dev` tree includes a working scalar decoder for the
broadcast-profile subset used by the local CCTV-9 regression stream. Profile
support remains partial until the remaining standard features and independent
conformance tests are complete.

```sh
cmake -S . -B build -DLIBCAVS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`cavsdec [--frames N] [--log LEVEL] [--output FILE] input.avs` reads Annex-B
input and writes decoded planar YUV frames. Use `-` for standard input or
output. The implemented broadcast path currently covers profile `0x48`,
YUV420, successive interlaced field pictures, advanced entropy, I/P/B
pictures, default quantization, motion compensation, loop filtering, reference
management, and display reordering.

## Broadcast Roadmap

| 规范目录 | 状态 | 当前覆盖与缺口 |
| --- | --- | --- |
| 前言 | 无独立实现项 | 项目以其中声明的标准范围和引用关系为依据。 |
| 引言 | 无独立实现项 | 已覆盖样例使用的高级熵、同极性场跳过和增强场编码路径；引言本身不规定独立解码过程。 |
| 1 范围 | 无独立实现项 | 项目目标包含广播电视视频解码；尚未达到本章所覆盖位流的完整实现。 |
| 2 规范性引用文件 | 部分完成 | 已复用 GB/T 20090.2-2013 中适用的位流、变换、预测和插值规则；对应的全部档次组合尚未覆盖。 |
| 3 术语和定义 | 无独立实现项 | 内部图像、场、宏块、参考图像和距离索引数据结构采用这些定义。 |
| 4 缩略语 | 无独立实现项 | 仅作为代码和文档术语依据。 |
| 5 约定 | 部分完成 | 已实现当前路径所需的位序、算术、取整、裁剪、扫描和坐标约定；其他未实现语法分支尚未逐项验证。 |
| 6 编码位流的结构 | 部分完成 | 已实现 YUV420 隔行连续场、I/P/B 编码顺序、参考图像和显示重排。缺少渐进序列、帧图像、YUV422、低延迟完整规则、一般化参考结构和中途序列参数切换。 |
| 7 位流的语法和语义 | 部分完成 | 已解析起始码、序列头、I/PB 图像头、条带头及样例所需的高级熵宏块和 8x8 系数语法。缺少广播基本熵端到端路径、4x4 系数/变换选择、多条带接续，以及扩展、用户数据和视频编辑单元的完整语义处理。加权量化和加权预测字段可解析，但尚未形成完整解码语义。 |
| 8 解析过程 | 部分完成 | 已实现样例所需的高级熵上下文初始化、二值化、常规/旁路算术解码、系数解析和 stuffing bin。缺少广播基本熵解析接入、4x4 系数分支，以及未覆盖语法组合的独立验证。 |
| 9 解码过程 | 部分完成 | 已实现当前子集的图像/场初始化、宏块类型、帧内预测、P_Skip、B_Direct、对称模式、PB field enhanced、参考选择、运动补偿、默认加权 8x8 反量化/反变换、样值重建、环路滤波和 DPB 输出。缺少加权量化、加权预测、4x4 端到端路径、YUV422、渐进/帧图像、低延迟完整输出规则、所有参考列表分支和多条带边界处理。 |
| 附录 A（规范性附录）伪起始码 | 完成 | 已实现伪起始码移除和位重排，并有独立边界测试。 |
| 附录 B（规范性附录）档次和级别 | 部分完成 | 已校验支持的档次和级别标识；尚未执行各级别的尺寸、采样率、码率、缓冲和组合上限。 |
| 附录 C（规范性附录）位流虚拟参考解码器 | 未实现 | 尚未实现 BBV 时序、缓冲占用和符合性验证。 |
| 附录 D（规范性附录）基本熵编码码表 | 部分完成 | 已有共享基本熵 VLC 表和解码基础模块，但尚未接入广播档完整图像解码路径。 |
| 附录 E（资料性附录）高级熵编码解码器参考实现方法 | 资料性参考 | 当前高级熵实现遵循第 8 章规范过程，并用附录 E 交叉核对共同的区间操作；不另行复制一套资料性解码器。 |

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
