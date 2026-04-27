# -----------------------------------------------------------------------------
# Placeholder texture generator — Appendix H, Phase 0.
# -----------------------------------------------------------------------------
# Runs the Python script at build configure+build time, emitting the 8–10
# natural-material PNGs into assets/textures/backgrounds/. Makes them a
# dependency of demen_core so `cmake --build` always has current placeholders.
# -----------------------------------------------------------------------------

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

set(PLACEHOLDER_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/assets/textures/backgrounds)
set(PLACEHOLDER_SCRIPT     ${CMAKE_CURRENT_SOURCE_DIR}/generate.py)
set(PLACEHOLDER_MANIFEST   ${CMAKE_CURRENT_SOURCE_DIR}/materials.json)

# One custom command; the script itself enumerates materials from the manifest
# and writes <material>.generated.png per the spec.
add_custom_command(
    OUTPUT  ${PLACEHOLDER_OUTPUT_DIR}/.stamp
    COMMAND ${Python3_EXECUTABLE} ${PLACEHOLDER_SCRIPT}
            --manifest ${PLACEHOLDER_MANIFEST}
            --out      ${PLACEHOLDER_OUTPUT_DIR}
            --stamp    ${PLACEHOLDER_OUTPUT_DIR}/.stamp
    DEPENDS ${PLACEHOLDER_SCRIPT} ${PLACEHOLDER_MANIFEST}
    COMMENT "Generating placeholder textures (Appendix H)"
    VERBATIM
)
add_custom_target(demen_placeholder_textures ALL
    DEPENDS ${PLACEHOLDER_OUTPUT_DIR}/.stamp
)

if(TARGET demen_core)
    add_dependencies(demen_core demen_placeholder_textures)
endif()
