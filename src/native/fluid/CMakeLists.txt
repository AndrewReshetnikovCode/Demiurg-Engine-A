# -----------------------------------------------------------------------------
# Subsystem: fluid (§2.5). Atmosphere + water + weather macro-grid.
# Public interface: src/native/include/demen/fluid.hpp
# -----------------------------------------------------------------------------

add_library(demen_fluid STATIC
    water_ca.cpp
    air_solver.cpp
    wave_solver.cpp
    weather_cycle.cpp
    fluid_abi.cpp
)
add_library(demen::fluid ALIAS demen_fluid)

target_link_libraries(demen_fluid
    PUBLIC  demen::public_headers
    PRIVATE demen::compile_options
)

target_include_directories(demen_fluid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
