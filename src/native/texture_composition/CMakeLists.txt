# -----------------------------------------------------------------------------
# Subsystem: texture_composition
# Scope lockout: files in this folder are owned by the "texture_composition"
# specialist. Public interface: src/native/include/demen/texture_composition.hpp
# -----------------------------------------------------------------------------

add_library(demen_texture_composition STATIC
    texraw.cpp
    atlas.cpp
    texture_composition_abi.cpp
)
add_library(demen::texture_composition ALIAS demen_texture_composition)

target_link_libraries(demen_texture_composition
    PUBLIC  demen::public_headers
    PRIVATE demen::compile_options
)

target_include_directories(demen_texture_composition PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
