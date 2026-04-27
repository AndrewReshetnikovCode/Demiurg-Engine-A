// =============================================================================
// vk_instance.hpp — one-shot Vulkan instance + physical/logical device.
// Split out of the Phase 0 vk_window monolith so Phase 3's render graph can
// share it without dragging the swapchain + render loop along.
// =============================================================================
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace demen::renderer {

struct VkContext {
    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice         device   = VK_NULL_HANDLE;
    VkQueue          graphics_queue = VK_NULL_HANDLE;
    uint32_t         graphics_family = UINT32_MAX;
    VkPhysicalDeviceProperties2 props{};
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
};

// Create an instance, pick a device, create a logical device. GLFW must
// already be initialised; caller supplies the required instance extensions
// via the GLFW surface helpers.
bool create_context(VkContext& ctx,
                    const std::vector<const char*>& required_extensions,
                    std::string* out_error) noexcept;

void destroy_context(VkContext& ctx) noexcept;

} // namespace demen::renderer
