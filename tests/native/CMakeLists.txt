# -----------------------------------------------------------------------------
# Native tests. Framework: CTest + plain assert-based runners for Phase 0;
# upgraded to doctest/Catch2 when the first subsystem lands real code.
# -----------------------------------------------------------------------------

add_executable(test_abi_smoke test_abi_smoke.cpp)
target_link_libraries(test_abi_smoke
    PRIVATE demen_core demen::compile_options)
add_test(NAME abi_smoke COMMAND test_abi_smoke)

# -----------------------------------------------------------------------------
# Phase 1 acceptance tests. Skeletons committed pre-dispatch (see
# docs/agent_pipeline/phase_gates.md); test bodies land with the Phase 1
# Specialist's implementation of voxel_store. Both tests compile today
# against demen/voxel_store.hpp and will link once the specialist provides
# the symbols — that's deliberate, so the specialist can iterate against the
# test output from the first commit of Phase 1.
# -----------------------------------------------------------------------------
add_executable(test_region_header test_region_header.cpp)
target_link_libraries(test_region_header
    PRIVATE demen_core demen::compile_options)
add_test(NAME region_header COMMAND test_region_header)

add_executable(test_round_trip_10k test_round_trip_10k.cpp)
target_link_libraries(test_round_trip_10k
    PRIVATE demen_core demen::compile_options)
add_test(NAME round_trip_10k COMMAND test_round_trip_10k)
# B-STREAM (Appendix E §E.3) points at the round_trip_10k test binary.

# -----------------------------------------------------------------------------
# Phase 2 acceptance tests. Skeletons committed pre-dispatch (see
# docs/agent_pipeline/phase_gates.md); test bodies land with the Phase 2
# Specialist's implementation of meshing + texture_composition. Both tests
# compile today against demen/meshing.hpp and demen/texture_composition.hpp
# and will link once the specialist provides the symbols — same pattern as
# Phase 1's test_region_header and test_round_trip_10k.
# -----------------------------------------------------------------------------
add_executable(test_greedy_mesh test_greedy_mesh.cpp)
target_link_libraries(test_greedy_mesh
    PRIVATE demen_core demen::compile_options)
add_test(NAME greedy_mesh COMMAND test_greedy_mesh)
# B-MESH (Appendix E §E.3) points at the greedy_mesh test binary.

add_executable(test_atlas_hot_reload test_atlas_hot_reload.cpp)
target_link_libraries(test_atlas_hot_reload
    PRIVATE demen_core demen::compile_options)
add_test(NAME atlas_hot_reload COMMAND test_atlas_hot_reload)
# B-TEXGEN (Appendix E §E.3) points at the atlas_hot_reload test binary.
