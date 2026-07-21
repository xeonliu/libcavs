cmake_minimum_required(VERSION 3.16)
project(libcavs VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(LIBCAVS_BUILD_SHARED "Build the shared library" ON)
option(LIBCAVS_BUILD_TESTS "Build unit tests" ON)

set(LIBCAVS_SOURCES src/decoder.c src/bitreader.c src/safe.c src/pseudo_start_code.c src/syntax.c)

add_library(cavs_static STATIC ${LIBCAVS_SOURCES})
add_library(cavs::static ALIAS cavs_static)
set_target_properties(cavs_static PROPERTIES OUTPUT_NAME cavs C_VISIBILITY_PRESET hidden)
target_include_directories(cavs_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(cavs_static PUBLIC c_std_99)

if(LIBCAVS_BUILD_SHARED)
  add_library(cavs_shared SHARED ${LIBCAVS_SOURCES})
  add_library(cavs::shared ALIAS cavs_shared)
  set_target_properties(cavs_shared PROPERTIES OUTPUT_NAME cavs C_VISIBILITY_PRESET hidden VERSION ${PROJECT_VERSION} SOVERSION 0)
  target_compile_definitions(cavs_shared PRIVATE CAVS_BUILDING_DLL)
  target_include_directories(cavs_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(cavs_shared PUBLIC c_std_99)
endif()

if(MSVC)
  target_compile_options(cavs_static PRIVATE /W4)
  if(TARGET cavs_shared)
    target_compile_options(cavs_shared PRIVATE /W4)
  endif()
else()
  target_compile_options(cavs_static PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  if(TARGET cavs_shared)
    target_compile_options(cavs_shared PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
endif()

if(LIBCAVS_BUILD_TESTS)
  enable_testing()
  add_executable(cavs_unit tests/test_bitreader.c tests/test_syntax.c
    src/bitreader.c src/safe.c src/pseudo_start_code.c src/syntax.c)
  target_include_directories(cavs_unit PRIVATE src include)
  target_compile_features(cavs_unit PRIVATE c_std_99)
  add_test(NAME cavs_unit COMMAND cavs_unit)
  add_executable(cavs_api_test tests/test_api.c)
  target_link_libraries(cavs_api_test PRIVATE cavs_static)
  target_compile_features(cavs_api_test PRIVATE c_std_99)
  add_test(NAME cavs_api_test COMMAND cavs_api_test)
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
