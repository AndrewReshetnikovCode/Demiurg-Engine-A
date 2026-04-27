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
