# -----------------------------------------------------------------------------
# Benchmarker harness (Appendix E §E.7).
#
# Phase 8 shape: a thin C++ binary that runs the gate suites described in
# Appendix E §E.3 and writes a JSON decision record per §E.5. The orchestrator
# Python script (run.py) shells to this binary plus to `dotnet test` for
# B-ALLOC, aggregates results, and writes build/benchmarker/decisions/.
#
# This subdirectory is intentionally minimal — Layer 1 ships with one
# C++ binary that *can* run the suites, but the suites themselves live in
# tests/native/ (B-STREAM, B-MESH, B-TEXGEN); the benchmarker re-runs those
# binaries with timing wrappers rather than re-implementing them.
# -----------------------------------------------------------------------------

add_executable(demen_benchmarker main.cpp)
target_link_libraries(demen_benchmarker
    PRIVATE demen_core demen::compile_options
)
target_include_directories(demen_benchmarker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
