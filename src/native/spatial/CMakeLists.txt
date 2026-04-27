# -----------------------------------------------------------------------------
# Subsystem: spatial — collision, raycast, frustum.
# Public interface: src/native/include/demen/spatial.hpp
# -----------------------------------------------------------------------------

add_library(demen_spatial STATIC spatial.cpp)
add_library(demen::spatial ALIAS demen_spatial)

target_link_libraries(demen_spatial
    PUBLIC  demen::public_headers
    PRIVATE demen::compile_options
)

target_include_directories(demen_spatial PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
