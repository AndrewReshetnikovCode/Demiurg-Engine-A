// =============================================================================
// vk_swapchain.cpp — FIFO_RELAXED, triple-buffered if supported.
// =============================================================================
#include "vk_swapchain.hpp"

#include <algorithm>

namespace demen::renderer {

namespace {

VkSurfaceFormatKHR pick_format(VkPhysicalDevice d, VkSurfaceKHR s) {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(d, s, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fs(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(d, s, &n, fs.data());
    for (auto& f : fs) {
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) return f;
    }
    return fs.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB,
                                           VK_COLORSPACE_SRGB_NONLINEAR_KHR}
                      : fs.front();
}

VkPresentModeKHR pick_present_mode(VkPhysicalDevice d, VkSurfaceKHR s) {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(d, s, &n, nullptr);
    std::vector<VkPresentModeKHR> pm(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(d, s, &n, pm.data());
    for (auto m : pm) if (m == VK_PRESENT_MODE_FIFO_RELAXED_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;   // guaranteed by spec
}

} // namespace

bool create_swapchain(const VkContext& ctx, VkSurfaceKHR surface,
                      uint32_t window_w, uint32_t window_h,
                      Swapchain& out) noexcept {
    out.surface = surface;

    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical, surface, &caps) != VK_SUCCESS)
        return false;

    VkSurfaceFormatKHR fmt = pick_format(ctx.physical, surface);
    VkPresentModeKHR  pm  = pick_present_mode(ctx.physical, surface);

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX) {
        ext.width  = std::clamp(window_w, caps.minImageExtent.width,  caps.maxImageExtent.width);
        ext.height = std::clamp(window_h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0) return false;

    uint32_t desired = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount && desired > caps.maxImageCount) desired = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = surface;
    sci.minImageCount    = desired;
    sci.imageFormat      = fmt.format;
    sci.imageColorSpace  = fmt.colorSpace;
    sci.imageExtent      = ext;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = pm;
    sci.clipped          = VK_TRUE;
    sci.oldSwapchain     = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(ctx.device, &sci, nullptr, &out.handle) != VK_SUCCESS)
        return false;

    out.format = fmt.format;
    out.extent = ext;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(ctx.device, out.handle, &n, nullptr);
    out.images.resize(n);
    vkGetSwapchainImagesKHR(ctx.device, out.handle, &n, out.images.data());

    out.views.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image    = out.images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format   = out.format;
        ivci.components.r = ivci.components.g = ivci.components.b = ivci.components.a =
            VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(ctx.device, &ivci, nullptr, &out.views[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

void destroy_swapchain(const VkContext& ctx, Swapchain& sc) noexcept {
    for (auto v : sc.views) if (v) vkDestroyImageView(ctx.device, v, nullptr);
    sc.views.clear();
    sc.images.clear();
    if (sc.handle) { vkDestroySwapchainKHR(ctx.device, sc.handle, nullptr); sc.handle = VK_NULL_HANDLE; }
    if (sc.surface) { vkDestroySurfaceKHR(ctx.instance, sc.surface, nullptr); sc.surface = VK_NULL_HANDLE; }
}

bool recreate_swapchain(const VkContext& ctx, Swapchain& sc,
                        uint32_t window_w, uint32_t window_h) noexcept {
    VkSurfaceKHR s = sc.surface;
    for (auto v : sc.views) if (v) vkDestroyImageView(ctx.device, v, nullptr);
    sc.views.clear();
    sc.images.clear();
    if (sc.handle) vkDestroySwapchainKHR(ctx.device, sc.handle, nullptr);
    sc.handle = VK_NULL_HANDLE;
    sc.surface = VK_NULL_HANDLE;  // prevent destroy_swapchain from nuking the surface
    bool ok = create_swapchain(ctx, s, window_w, window_h, sc);
    return ok;
}

} // namespace demen::renderer
