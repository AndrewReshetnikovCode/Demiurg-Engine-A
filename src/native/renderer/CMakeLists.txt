# -----------------------------------------------------------------------------
# Subsystem: renderer (Vulkan 1.3, §2.6). Phase 3 render graph.
# -----------------------------------------------------------------------------

find_package(Vulkan 1.3 REQUIRED)

add_library(demen_renderer STATIC
    vk_instance.cpp
    vk_swapchain.cpp
    vk_allocator.cpp
    pipeline_cache.cpp
    render_graph.cpp
    vk_window.cpp
)
add_library(demen::renderer ALIAS demen_renderer)

target_link_libraries(demen_renderer
    PUBLIC  demen::public_headers Vulkan::Vulkan glfw
    PRIVATE demen::compile_options
)

target_include_directories(demen_renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
