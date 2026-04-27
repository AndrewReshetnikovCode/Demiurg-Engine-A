// =============================================================================
// vk_instance.cpp — create + tear down a minimal Vulkan 1.3 environment.
// =============================================================================
#include "vk_instance.hpp"

#include <cstdio>
#include <cstring>

namespace demen::renderer {

namespace {

#ifndef NDEBUG
VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/) {
    const char* tag = "INFO";
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   tag = "ERROR";
    else if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) tag = "WARN";
    std::fprintf(stderr, "[vk:%s] %s\n", tag, data ? data->pMessage : "(null)");
    return VK_FALSE;
}
#endif

} // namespace

bool create_context(VkContext& ctx,
                    const std::vector<const char*>& required_extensions,
                    std::string* out_error) noexcept {
    auto fail = [&](const char* msg) {
        if (out_error) *out_error = msg;
        return false;
    };

    // ---- Instance ------------------------------------------------------------
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "DemEn";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName        = "DemEn Core";
    app.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> exts = required_extensions;
    std::vector<const char*> layers;
#ifndef NDEBUG
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo        = &app;
    ici.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
    ici.enabledLayerCount       = static_cast<uint32_t>(layers.size());
    ici.ppEnabledLayerNames     = layers.data();

    if (vkCreateInstance(&ici, nullptr, &ctx.instance) != VK_SUCCESS)
        return fail("vkCreateInstance failed");

#ifndef NDEBUG
    {
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (fn) {
            VkDebugUtilsMessengerCreateInfoEXT dci{};
            dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            dci.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dci.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dci.pfnUserCallback = debug_cb;
            fn(ctx.instance, &dci, nullptr, &ctx.debug);
        }
    }
#endif

    // ---- Physical device -----------------------------------------------------
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(ctx.instance, &n, nullptr);
    if (n == 0) return fail("no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(ctx.instance, &n, devs.data());

    auto has_graphics_and_present = [](VkPhysicalDevice d, uint32_t* out_family) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
        for (uint32_t i = 0; i < qn; ++i) {
            if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                *out_family = i;
                return true;
            }
        }
        return false;
    };

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    uint32_t family = UINT32_MAX;
    // Prefer discrete.
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        uint32_t f = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            has_graphics_and_present(d, &f)) {
            chosen = d; family = f; break;
        }
    }
    if (!chosen) {
        for (auto d : devs) {
            uint32_t f = 0;
            if (has_graphics_and_present(d, &f)) { chosen = d; family = f; break; }
        }
    }
    if (!chosen) return fail("no suitable Vulkan device");

    ctx.physical        = chosen;
    ctx.graphics_family = family;
    ctx.props.sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2(chosen, &ctx.props);

    // ---- Logical device ------------------------------------------------------
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = family;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char* dev_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkPhysicalDeviceDynamicRenderingFeatures dyn_rendering{};
    dyn_rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dyn_rendering.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures indexing{};
    indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    indexing.runtimeDescriptorArray                    = VK_TRUE;
    indexing.descriptorBindingPartiallyBound           = VK_TRUE;
    indexing.descriptorBindingVariableDescriptorCount  = VK_TRUE;
    indexing.pNext = &dyn_rendering;

    VkPhysicalDeviceFeatures2 feats2{};
    feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feats2.pNext = &indexing;

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &feats2;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = sizeof(dev_exts) / sizeof(dev_exts[0]);
    dci.ppEnabledExtensionNames = dev_exts;

    if (vkCreateDevice(chosen, &dci, nullptr, &ctx.device) != VK_SUCCESS)
        return fail("vkCreateDevice failed");

    vkGetDeviceQueue(ctx.device, family, 0, &ctx.graphics_queue);
    return true;
}

void destroy_context(VkContext& ctx) noexcept {
    if (ctx.device) {
        vkDeviceWaitIdle(ctx.device);
        vkDestroyDevice(ctx.device, nullptr);
        ctx.device = VK_NULL_HANDLE;
    }
    if (ctx.instance) {
        if (ctx.debug) {
            auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (fn) fn(ctx.instance, ctx.debug, nullptr);
            ctx.debug = VK_NULL_HANDLE;
        }
        vkDestroyInstance(ctx.instance, nullptr);
        ctx.instance = VK_NULL_HANDLE;
    }
    ctx.physical        = VK_NULL_HANDLE;
    ctx.graphics_queue  = VK_NULL_HANDLE;
    ctx.graphics_family = UINT32_MAX;
}

} // namespace demen::renderer
