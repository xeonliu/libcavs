cmake_minimum_required(VERSION 3.16)
project(libcavs VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(LIBCAVS_BUILD_SHARED "Build the shared library" ON)
option(LIBCAVS_BUILD_TESTS "Build unit tests" ON)
option(LIBCAVS_BUILD_FUZZER "Build the Clang libFuzzer decoder target" OFF)
option(LIBCAVS_BUILD_AFL_FUZZER "Build the standalone AFL decoder target" OFF)
option(LIBCAVS_BUILD_TOOL "Build the cavsdec inspection tool" ON)

function(libcavs_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
endfunction()

set(LIBCAVS_CORE_SOURCES
  src/decoder.c
  src/picture_pipeline.c
  src/frame.c
  src/picture.c
  src/dpb.c
  src/bitreader.c
  src/safe.c)

set(LIBCAVS_CODEC_SOURCES
  src/codec/advanced_entropy.c
  src/codec/baseline_decode.c
  src/codec/unit.c
  src/codec/baseline_macroblock.c
  src/codec/broadcast_macroblock.c
  src/codec/broadcast_basic_macroblock.c
  src/codec/broadcast_quant.c
  src/codec/broadcast_weighting.c
  src/codec/broadcast_decode.c
  src/codec/broadcast_motion.c
  src/codec/broadcast_reconstruction.c
  src/codec/coefficients.c
  src/codec/loop_filter.c
  src/codec/motion.c
  src/codec/pseudo_start_code.c
  src/codec/slice_decode.c
  src/codec/syntax.c)

set(LIBCAVS_DSP_SOURCES
  src/dsp/loop_filter.c
  src/dsp/motion_compensation.c
  src/dsp/prediction.c
  src/dsp/reconstruction.c
  src/dsp/transform.c)

set(LIBCAVS_SOURCES
  ${LIBCAVS_CORE_SOURCES}
  ${LIBCAVS_CODEC_SOURCES}
  ${LIBCAVS_DSP_SOURCES})

add_library(cavs_static STATIC ${LIBCAVS_SOURCES})
add_library(cavs::static ALIAS cavs_static)
set_target_properties(cavs_static PROPERTIES
  OUTPUT_NAME cavs
  EXPORT_NAME static
  C_VISIBILITY_PRESET hidden)
target_include_directories(cavs_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(cavs_static PRIVATE src)
target_compile_features(cavs_static PUBLIC c_std_99)
libcavs_enable_warnings(cavs_static)

if(LIBCAVS_BUILD_SHARED)
  add_library(cavs_shared SHARED ${LIBCAVS_SOURCES})
  add_library(cavs::shared ALIAS cavs_shared)
  set_target_properties(cavs_shared PROPERTIES
    OUTPUT_NAME cavs
    EXPORT_NAME shared
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)
  target_compile_definitions(cavs_shared PRIVATE CAVS_BUILDING_DLL)
  target_include_directories(cavs_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_include_directories(cavs_shared PRIVATE src)
  target_compile_features(cavs_shared PUBLIC c_std_99)
  libcavs_enable_warnings(cavs_shared)
endif()

if(LIBCAVS_BUILD_TESTS)
  enable_testing()
  add_library(cavs_test_internal STATIC ${LIBCAVS_SOURCES})
  target_include_directories(cavs_test_internal PUBLIC include src)
  target_compile_definitions(cavs_test_internal PRIVATE CAVS_TESTING)
  target_compile_features(cavs_test_internal PUBLIC c_std_99)
  libcavs_enable_warnings(cavs_test_internal)

  add_executable(cavs_unit
    tests/unit/test_bitreader.c
    tests/unit/test_syntax.c
    tests/unit/test_macroblock.c
    tests/unit/test_coefficients.c
    tests/unit/test_reconstruction.c
    tests/unit/test_prediction.c
    tests/unit/test_motion.c
    tests/unit/test_frame.c
    tests/unit/test_dpb.c
    tests/unit/test_slice_decode.c
    tests/unit/test_broadcast_pipeline.c
    tests/unit/test_broadcast_quant.c
    tests/unit/test_broadcast_weighting.c
    tests/unit/test_broadcast_basic_macroblock.c
    tests/unit/test_loop_filter.c)
  target_include_directories(cavs_unit PRIVATE src include tests)
  target_link_libraries(cavs_unit PRIVATE cavs_test_internal)
  target_compile_definitions(cavs_unit PRIVATE CAVS_TESTING)
  target_compile_features(cavs_unit PRIVATE c_std_99)
  libcavs_enable_warnings(cavs_unit)
  add_test(NAME cavs_unit COMMAND cavs_unit)
  add_executable(cavs_api_test tests/integration/test_api.c)
  target_include_directories(cavs_api_test PRIVATE tests)
  target_link_libraries(cavs_api_test PRIVATE cavs_static)
  target_compile_features(cavs_api_test PRIVATE c_std_99)
  libcavs_enable_warnings(cavs_api_test)
  add_test(NAME cavs_api_test COMMAND cavs_api_test)
endif()

if(LIBCAVS_BUILD_TOOL)
  add_executable(cavsdec tools/cavsdec.c)
  target_link_libraries(cavsdec PRIVATE cavs_static)
  target_compile_features(cavsdec PRIVATE c_std_99)
  libcavs_enable_warnings(cavsdec)
  if(LIBCAVS_BUILD_TESTS)
    add_test(NAME cavsdec_help COMMAND cavsdec --help)
    add_test(NAME cavsdec_rejects_negative_limit COMMAND cavsdec --frames -1 missing.avs)
    set_tests_properties(cavsdec_rejects_negative_limit PROPERTIES WILL_FAIL TRUE)
  endif()
  install(TARGETS cavsdec RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(LIBCAVS_BUILD_FUZZER)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "LIBCAVS_BUILD_FUZZER requires Clang with libFuzzer")
  endif()
  add_executable(cavs_fuzz_decoder tests/fuzz/fuzz_decoder.c)
  target_link_libraries(cavs_fuzz_decoder PRIVATE cavs_static)
  target_include_directories(cavs_fuzz_decoder PRIVATE src)
  target_compile_features(cavs_fuzz_decoder PRIVATE c_std_99)
  libcavs_enable_warnings(cavs_fuzz_decoder)
  target_compile_options(cavs_static PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
  target_compile_options(cavs_fuzz_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(cavs_fuzz_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if(LIBCAVS_BUILD_AFL_FUZZER)
  add_executable(cavs_afl_decoder tests/fuzz/fuzz_decoder.c)
  target_link_libraries(cavs_afl_decoder PRIVATE cavs_static)
  target_include_directories(cavs_afl_decoder PRIVATE src)
  target_compile_features(cavs_afl_decoder PRIVATE c_std_99)
  libcavs_enable_warnings(cavs_afl_decoder)
  target_compile_definitions(cavs_afl_decoder PRIVATE CAVS_AFL_STANDALONE)
endif()

install(FILES include/cavs/cavs.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cavs)
install(TARGETS cavs_static EXPORT cavsTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(TARGET cavs_shared)
  install(TARGETS cavs_shared EXPORT cavsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(EXPORT cavsTargets NAMESPACE cavs:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cavs)

configure_package_config_file(cmake/cavsConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/cavsConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cavs)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/cavsConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
configure_file(cmake/cavs.pc.in ${CMAKE_CURRENT_BINARY_DIR}/cavs.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cavsConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/cavsConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cavs)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cavs.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
