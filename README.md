# libcavs

`libcavs` is a lightweight, open-source C library designed for decoding Chinese AVS (Audio Video Coding Standard) video streams. It specifically targets the **AVS1-P2 (JiZhun profile)** and **AVS1-P16 (Guangdian profile)**.

## Features

- **Standard Compliance**: Supports AVS1-P2 (JiZhun) and AVS1-P16 (Guangdian) video decoding.
- **Cross-Platform**: Compatible with both Linux and Windows operating systems.
- **Multi-threading**: Built-in support for multi-threaded decoding to leverage multi-core processors.
- **Pure C**: implemented in standard C for high portability and performance.
- **Low-Level Control**: Provides APIs for stream initialization, NAL unit extraction, and frame processing.

## Build

You can build the project using CMake or directly with GCC.

### Using CMake (Recommended)

1.  Create a build directory:
    ```bash
    mkdir build
    cd build
    ```

2.  Run CMake and compile:
    ```bash
    cmake ..
    make
    ```

This will generate the `cavs_decode` executable and the `libcavs` static library.

### Using GCC

You can also compile the simple decoder application directly:

```bash
gcc -o cavs_decode main.c libcavs.c -lpthread
```

*(Note: `-lpthread` is required on Linux/macOS for threading support)*

## Usage

### Command Line Tool

After building, you can use the `cavs_decode` tool to decode AVS files to raw YUV format.

**Syntax:**

```bash
./cavs_decode <input.avs> <output.yuv>
```

**Example:**

A sample video file is provided in the `sample` directory. To decode it:

```bash
# Assuming you are in the build directory
./cavs_decode ../sample/CCTV-9.avs output.yuv
```

The output `output.yuv` will be a raw YUV video file.

### Library Integration

To use `libcavs` in your own C/C++ project, include `libcavs.h` and link against `libcavs.c` (or the compiled library).

#### Basic API Workflow

1.  **Include the header:**
    ```c
    #include "libcavs.h"
    ```

2.  **Initialize Parameters:**
    Set up the `cavs_param` structure with desired configurations (e.g., thread number, acceleration).

3.  **Create Decoder:**
    ```c
    void *decoder;
    cavs_param param;
    // ... initialize param ...
    cavs_decoder_create(&decoder, &param);
    ```

4.  **Initialize Stream:**
    Feed the raw stream data to the decoder.
    ```c
    cavs_decoder_init_stream(decoder, raw_stream_buffer, stream_length);
    ```

5.  **Decode Loop:**
    Parse NAL units and process frames.
    ```c
    // Example pseudo-code
    while (more_data) {
        cavs_decoder_get_one_nal(decoder, buffer, &length);
        cavs_decoder_process(decoder, buffer, length);
        // Handle decoded output in param.p_out_yuv
    }
    ```

## File Structure

- `libcavs.h`: The main header file containing API definitions, data structures, and constants.
- `libcavs.c`: The implementation of the decoder core.
- `main.c`: A command-line tool example that demonstrates how to use the library.
- `CMakeLists.txt`: CMake build configuration file.
- `sample/`: Directory containing sample AVS video files.
- `LICENSE.txt`: The MIT License file.

## License

This project is licensed under the MIT License. See the [LICENSE.txt](LICENSE.txt) file for details.

## Testing

### MD5 Regression Test

A regression test is included to verify that the decoded YUV output matches the expected MD5 checksum for a given input file. This ensures the decoder produces consistent results.

#### Running the Test

1. Build the test executable:
    ```bash
    cd build
    cmake ..
    make test_md5
    ```

2. Run the test using CTest:
    ```bash
    ctest -V -R yuv_md5_match
    ```

3. Alternatively, run the test directly:
    ```bash
    ./test_md5 ../sample/CCTV-9.avs
    ```

#### Expected Output

The test will decode the sample file and compute the MD5 checksum of the output. It will compare the result to the expected value:

```
Decoded frames : 742
Output MD5     : 8fd8d5c4b9237f69cc45ff289d0c46b0
Expected MD5   : 8fd8d5c4b9237f69cc45ff289d0c46b0
Result         : PASS
```
