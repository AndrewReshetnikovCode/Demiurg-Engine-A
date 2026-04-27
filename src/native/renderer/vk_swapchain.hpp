// =============================================================================
// vk_swapchain.hpp — FIFO_RELAXED triple-buffered swapchain (§2.6).
// =============================================================================
#pragma once

#include "vk_instance.hpp"
#include <vector>

namespace demen::renderer {

struct Swapchain {
    VkSurfaceKHR             surface = VK_NULL_HANDLE;
    VkSwapchainKHR           handle  = VK_NULL_HANDLE;
    VkFormat                 format  = VK_FORMAT_UNDEFINED;
    VkExtent2D               extent  = { 0, 0 };
    std::vector<VkImage>     images;
    std::vector<VkImageView> views;
};

bool create_swapchain(const VkContext& ctx, VkSurfaceKHR surface,
                      uint32_t window_w, uint32_t window_h,
                      Swapchain& out) noexcept;

void destroy_swapchain(const VkContext& ctx, Swapchain& sc) noexcept;

// Re-create on resize. Caller waits the device idle first.
bool recreate_swapchain(const VkContext& ctx, Swapchain& sc,
                        uint32_t window_w, uint32_t window_h) noexcept;

} // namespace demen::renderer
