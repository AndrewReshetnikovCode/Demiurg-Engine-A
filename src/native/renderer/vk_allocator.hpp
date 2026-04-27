// =============================================================================
// vk_allocator.hpp — ultra-minimal buffer + image allocator. Wraps plain
// VkDeviceMemory allocations, one per resource. This trades fragmentation for
// simplicity and does not claim to scale — §2.11 rung 1. Phase 3 targets pass
// with this; a VMA swap-in is a future Specialist escalation backed by a
// whole-system benchmark (invariant #8).
// =============================================================================
#pragma once

#include "vk_instance.hpp"
#include <optional>

namespace demen::renderer {

struct Buffer {
    VkBuffer       handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
    void*          mapped = nullptr;   // non-null iff HOST_VISIBLE mapped
};

struct Image {
    VkImage        handle = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkExtent3D     extent = { 0, 0, 0 };
    VkFormat       format = VK_FORMAT_UNDEFINED;
    uint32_t       layers = 1;
    uint32_t       mips   = 1;
};

bool create_buffer(const VkContext& ctx,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags props,
                   Buffer& out) noexcept;

void destroy_buffer(const VkContext& ctx, Buffer& b) noexcept;

bool create_image_2d(const VkContext& ctx,
                     uint32_t width, uint32_t height,
                     VkFormat format,
                     VkImageUsageFlags usage,
                     VkImageAspectFlags aspect,
                     Image& out) noexcept;

void destroy_image(const VkContext& ctx, Image& img) noexcept;

// One-shot command submission for uploads / layout transitions.
// Returns VK_NULL_HANDLE on failure.
VkCommandBuffer begin_one_shot(const VkContext& ctx, VkCommandPool pool) noexcept;
void end_one_shot(const VkContext& ctx, VkCommandPool pool, VkCommandBuffer cmd) noexcept;

// Host-visible staging buffer helper: create, memcpy in, return.
bool upload_via_staging(const VkContext& ctx, VkCommandPool pool,
                        const void* src, VkDeviceSize bytes,
                        VkBuffer dst, VkDeviceSize dst_offset) noexcept;

// Upload into an image (transitioning it UNDEFINED -> TRANSFER_DST -> SHADER_READ).
bool upload_image_via_staging(const VkContext& ctx, VkCommandPool pool,
                              const void* src, VkDeviceSize bytes,
                              Image& img) noexcept;

} // namespace demen::renderer
