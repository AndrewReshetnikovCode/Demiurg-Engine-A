# =============================================================================
# DemEn — top-level build
# Design doc: DESIGN.md  (Phase 0, §3.3)
# =============================================================================
#
# Layout:
#   src/native/**       — C++20 core libraries (one per subsystem)
#   src/native/include  — public headers (Planner-owned; invariant #4)
#   src/managed/**      — C# .NET 8 projects (built separately via dotnet)
#   tools/**            — build-time tooling (placeholder texgen, benchmarker)
#   shaders/src         — GLSL source, compiled to shaders/spirv at build time
#
# This file builds the native half only. The managed half is driven by
# `dotnet build DemEn.sln` and consumes the native DLL produced here.
# A top-level `scripts/build.ps1` / `scripts/build.sh` orchestrates both.
# =============================================================================

cmake_minimum_required(VERSION 3.26)

project(DemEn
    VERSION 0.0.1
    DESCRIPTION "Voxel simulation engine (Layer 1: physics substrate)"
    LANGUAGES CXX C
)

# -----------------------------------------------------------------------------
# Global settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile_commands.json for tooling / clangd / CI header-drift checks.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Hard-fail on out-of-source build into the source tree.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are disabled. Build from ./build or ./out.")
endif()

# -----------------------------------------------------------------------------
# Options (compile-time switches; see DESIGN.md §2.5.1 for FLUID_AIR_DOWNSAMPLE)
# -----------------------------------------------------------------------------
option(DEMEN_ENABLE_AVX2       "Enable AVX2 baseline for hot kernels"        ON)
option(DEMEN_ENABLE_AVX512     "Enable AVX-512 opt-in path"                  OFF)
option(DEMEN_BUILD_TESTS       "Build native unit + perf tests"              ON)
option(DEMEN_BUILD_BENCHMARKS  "Build benchmarker harness (Appendix E)"      ON)
option(DEMEN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in CI"    ON)

set(DEMEN_FLUID_AIR_DOWNSAMPLE "4" CACHE STRING
    "Atmospheric grid downsample factor (voxels per air cell edge). See §2.5.1.")
set_property(CACHE DEMEN_FLUID_AIR_DOWNSAMPLE PROPERTY STRINGS "2" "4" "8")

# -----------------------------------------------------------------------------
# Compiler flags — MSVC-first, Clang-cl/GCC tolerated for dev
# -----------------------------------------------------------------------------
add_library(demen_compile_options INTERFACE)
add_library(demen::compile_options ALIAS demen_compile_options)

if(MSVC)
    target_compile_options(demen_compile_options INTERFACE
        /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /MP
        $<$<BOOL:${DEMEN_WARNINGS_AS_ERRORS}>:/WX>
        $<$<BOOL:${DEMEN_ENABLE_AVX2}>:/arch:AVX2>
    )
    target_compile_definitions(demen_compile_options INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(demen_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        $<$<BOOL:${DEMEN_WARNINGS_AS_ERRORS}>:-Werror>
        $<$<BOOL:${DEMEN_ENABLE_AVX2}>:-mavx2 -mfma>
        $<$<BOOL:${DEMEN_ENABLE_AVX512}>:-mavx512f -mavx512dq>
    )
endif()

target_compile_definitions(demen_compile_options INTERFACE
    DEMEN_FLUID_AIR_DOWNSAMPLE=${DEMEN_FLUID_AIR_DOWNSAMPLE}
)

# -----------------------------------------------------------------------------
# External dependencies
# -----------------------------------------------------------------------------
# Vulkan SDK is mandatory (§2.6). We find it system-wide; the installer script
# in ci/ verifies version >= 1.3.
find_package(Vulkan 1.3 REQUIRED)

# -----------------------------------------------------------------------------
# GLFW (WSI — windowing & input abstraction).
# Fetched at configure time; no install-time dependency on users.
# -----------------------------------------------------------------------------
include(FetchContent)
set(GLFW_BUILD_DOCS      OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL         OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# LZ4 for region-file compression (§2.3, Appendix A).
# Third-party vendoring strategy: header+single-TU libs get dropped into
# third_party/ and built from source; anything heavier gets FetchContent.
# For Phase 0 we leave this as a stub; Phase 1 wires it up.
# find_package(lz4 CONFIG REQUIRED)  # TODO Phase 1

# -----------------------------------------------------------------------------
# Public header interface (Planner-owned; invariant #4)
# -----------------------------------------------------------------------------
# Every subsystem consumes this target to get at public headers. Editing any
# file under src/native/include triggers the CI "interface-drift" label.
add_library(demen_public_headers INTERFACE)
add_library(demen::public_headers ALIAS demen_public_headers)
target_include_directories(demen_public_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/include
)

# -----------------------------------------------------------------------------
# Subsystems — each builds a static lib, aggregated into demen_core shared lib
# -----------------------------------------------------------------------------
add_subdirectory(src/native/voxel_store)
add_subdirectory(src/native/meshing)
add_subdirectory(src/native/fluid)
add_subdirectory(src/native/renderer)
add_subdirectory(src/native/spatial)
add_subdirectory(src/native/texture_composition)

# Shared library consumed by C# via P/Invoke (§2.1.2).
# All symbols crossing the managed boundary live under `demen_abi.h`.
add_library(demen_core SHARED src/native/demen_abi.cpp)
target_link_libraries(demen_core
      PRIVATE
          demen::voxel_store
          demen::meshing
          demen::fluid
          demen::renderer
          demen::spatial
          demen::texture_composition
          demen::compile_options
      PUBLIC
          demen::public_headers
  )
  if(MSVC)
      target_link_options(demen_core PRIVATE
          /WHOLEARCHIVE:demen_voxel_store
          /WHOLEARCHIVE:demen_meshing
          /WHOLEARCHIVE:demen_fluid
          /WHOLEARCHIVE:demen_renderer
          /WHOLEARCHIVE:demen_spatial

  /WHOLEARCHIVE:demen_texture_composition
      )
  endif()
set_target_properties(demen_core PROPERTIES
    OUTPUT_NAME "demen"
    VERSION ${PROJECT_VERSION}
)

# -----------------------------------------------------------------------------
# Shader precompilation (§2.6 — all shaders shipped as SPIR-V)
# -----------------------------------------------------------------------------
add_subdirectory(shaders)

# -----------------------------------------------------------------------------
# Tooling: placeholder texture generator (Appendix H, Phase 0 deliverable)
# -----------------------------------------------------------------------------
add_subdirectory(tools/placeholder_texgen)

# -----------------------------------------------------------------------------
# Tests & benchmarks
# -----------------------------------------------------------------------------
if(DEMEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/native)
endif()

if(DEMEN_BUILD_BENCHMARKS)
    add_subdirectory(tools/benchmarker)
endif()

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "DemEn configuration:")
message(STATUS "  Version ................ ${PROJECT_VERSION}")
message(STATUS "  C++ standard ........... ${CMAKE_CXX_STANDARD}")
message(STATUS "  AVX2 ................... ${DEMEN_ENABLE_AVX2}")
message(STATUS "  AVX-512 ................ ${DEMEN_ENABLE_AVX512}")
message(STATUS "  FLUID_AIR_DOWNSAMPLE ... ${DEMEN_FLUID_AIR_DOWNSAMPLE}")
message(STATUS "  Vulkan ................. ${Vulkan_VERSION} (${Vulkan_LIBRARY})")
message(STATUS "  Tests .................. ${DEMEN_BUILD_TESTS}")
message(STATUS "  Benchmarks ............. ${DEMEN_BUILD_BENCHMARKS}")
message(STATUS "")

# Mark ABI TUs as exporting (vs. importing) DEMEN_API symbols.
  # The shared aggregator AND every static subsystem that compiles a
  # DEMEN_API definition needs the export flag — otherwise MSVC sees a
  # dllimport definition (C2491) when it compiles the .objs that will 
  # later be linked into demen_core. GCC/Clang's visibility attribute
  # model tolerates the imbalance silently; MSVC does not.
  target_compile_definitions(demen_core PRIVATE
  DEMEN_CORE_EXPORTS)
  foreach(_subsys
          demen_voxel_store
          demen_meshing
          demen_texture_composition
          demen_renderer
          demen_spatial
          demen_fluid)
      target_compile_definitions(${_subsys}
  PRIVATE DEMEN_CORE_EXPORTS)
  endforeach()
