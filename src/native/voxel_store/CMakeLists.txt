# -----------------------------------------------------------------------------
# Subsystem: voxel_store  (DESIGN.md §2.3)
# -----------------------------------------------------------------------------
add_library(demen_voxel_store STATIC
    palette_chunk.cpp
    column.cpp
    region_io.cpp
    world.cpp
    voxel_store_abi.cpp
)
add_library(demen::voxel_store ALIAS demen_voxel_store)

target_link_libraries(demen_voxel_store
    PUBLIC  demen::public_headers
    PRIVATE demen::compile_options
)

target_include_directories(demen_voxel_store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(demen_voxel_grid_debug
    codex/arg_parser.cpp
    codex/console_render.cpp
    codex/file_ops.cpp
    codex/gas_generator.cpp
    codex/random_world_initializer.cpp
    codex/random_world_initializer_main.cpp
    codex/world_fill.cpp
    codex/world_init.cpp
)
set_target_properties(demen_voxel_grid_debug PROPERTIES
    OUTPUT_NAME "voxel_grid_debug"
)
target_link_libraries(demen_voxel_grid_debug
    PRIVATE
        demen::voxel_store
        demen::compile_options
)
