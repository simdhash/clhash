cmake_minimum_required(VERSION 3.16)

# Default to a Release build when the user hasn't picked one. For multi-config
# generators (Visual Studio, Xcode), set the default config instead. This must
# go before project() so it influences the initial CMAKE_BUILD_TYPE cache.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

project(clhash LANGUAGES C CXX)

option(CLHASH_ENABLE_BITMIX "Enable CLHASH_BITMIX compile definition" OFF)
include(cmake/CPM.cmake)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(clhash src/clhash.c)
add_library(clhash::clhash ALIAS clhash)

# Generator expressions so the include path is correct in both the build
# tree and after `cmake --install`.
target_include_directories(clhash PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

# Per-architecture intrinsic flags. GCC/Clang require the user to opt in to
# PCLMULQDQ/SSE4.2 on x86 and to the crypto extension on ARM; MSVC and clang-cl
# on x64 enable the equivalent intrinsics by default.
if(NOT MSVC)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    target_compile_options(clhash PRIVATE -msse4.2 -mpclmul)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    target_compile_options(clhash PRIVATE -march=armv8-a+crypto)
  endif()
endif()

if(CLHASH_ENABLE_BITMIX)
  target_compile_definitions(clhash PUBLIC CLHASH_BITMIX)
endif()

# Install rules. Disable with -DCLHASH_INSTALL=OFF when consuming clhash via
# add_subdirectory() from another project.
option(CLHASH_INSTALL "Generate install rules for clhash" ON)
if(CLHASH_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS clhash
    EXPORT clhashTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(FILES include/clhash.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT clhashTargets
    FILE clhashTargets.cmake
    NAMESPACE clhash::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clhash)

  # Minimal package config so `find_package(clhash)` works for downstream
  # CMake users. SameMajorVersion is a sensible default for a 1.x library.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/clhashConfig.cmake"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/clhashTargets.cmake\")\n")
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/clhashConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion)

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/clhashConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/clhashConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clhash)
endif()


add_executable(unit tests/unit.c)
target_link_libraries(unit PRIVATE clhash)
add_executable(cppunit tests/cppunit.cpp)
target_link_libraries(cppunit PRIVATE clhash)

add_executable(benchmark benchmarks/benchmark.c)
target_link_libraries(benchmark PRIVATE clhash)
add_executable(example examples/example.c)
target_link_libraries(example PRIVATE clhash)
add_executable(cppexample examples/cppexample.cpp)
target_link_libraries(cppexample PRIVATE clhash)

enable_testing()
add_test(NAME clhash_unit COMMAND unit)
add_test(NAME clhash_cppunit COMMAND cppunit)
