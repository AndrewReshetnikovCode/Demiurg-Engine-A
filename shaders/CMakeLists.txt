# -----------------------------------------------------------------------------
# Shader precompilation (§2.6)
# -----------------------------------------------------------------------------
# All shaders ship as SPIR-V. Runtime pipeline cache is primed on first launch.
# This file compiles every *.vert / *.frag / *.comp under shaders/src/ into
# shaders/spirv/*.spv and makes them an install dependency of demen_core.
# -----------------------------------------------------------------------------

find_program(GLSLANG_VALIDATOR glslangValidator HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "glslangValidator not found. Install the Vulkan SDK 1.3+ "
        "and ensure it is on PATH.")
endif()

file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.comp
)

set(SPIRV_OUTPUTS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_FILE ${CMAKE_CURRENT_SOURCE_DIR}/spirv/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT  ${SPIRV_FILE}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -o ${SPIRV_FILE} ${SHADER}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME} -> SPIR-V"
        VERBATIM
    )
    list(APPEND SPIRV_OUTPUTS ${SPIRV_FILE})
endforeach()

add_custom_target(demen_shaders ALL DEPENDS ${SPIRV_OUTPUTS})

# demen_core depends on the shader build completing.
if(TARGET demen_core)
    add_dependencies(demen_core demen_shaders)
endif()
