# -----------------------------------------------------------------------------
# Subsystem: meshing
# Scope lockout: this folder is owned by the meshing specialist.
# Public interface: src/native/include/demen/meshing.hpp
# -----------------------------------------------------------------------------

add_library(demen_meshing STATIC
    chunk_view.cpp
    greedy_mesher.cpp
    meshing_abi.cpp
)
add_library(demen::meshing ALIAS demen_meshing)

target_link_libraries(demen_meshing
    PUBLIC  demen::public_headers
    PRIVATE demen::compile_options
)

target_include_directories(demen_meshing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
