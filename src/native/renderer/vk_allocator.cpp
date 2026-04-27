// =============================================================================
// vk_allocator.cpp — raw vkAllocateMemory-per-resource. See header rationale.
// =============================================================================
#include "vk_allocator.hpp"

#include <cstring>

namespace demen::renderer {

namespace {

std::optional<uint32_t> find_memory_type(const VkContext& ctx,
                                         uint32_t type_bits,
                                         VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        const bool bit_ok = (type_bits & (1u << i)) != 0;
        const bool flag_ok = (mp.memoryTypes[i].propertyFlags & props) == props;
        if (bit_ok && flag_ok) return i;
    }
    return std::nullopt;
}

} // namespace

bool create_buffer(const VkContext& ctx,
                   VkDeviceSize size, VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags props, Buffer& out) noexcept {
    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.device, &bci, nullptr, &out.handle) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(ctx.device, out.handle, &mr);

    auto ti = find_memory_type(ctx, mr.memoryTypeBits, props);
    if (!ti) { vkDestroyBuffer(ctx.device, out.handle, nullptr); out.handle = VK_NULL_HANDLE; return false; }

    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = *ti;
    if (vkAllocateMemory(ctx.device, &mai, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx.device, out.handle, nullptr); out.handle = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(ctx.device, out.handle, out.memory, 0);
    out.size = size;
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(ctx.device, out.memory, 0, size, 0, &out.mapped);
    }
    return true;
}

void destroy_buffer(const VkContext& ctx, Buffer& b) noexcept {
    if (b.mapped) { vkUnmapMemory(ctx.device, b.memory); b.mapped = nullptr; }
    if (b.handle) { vkDestroyBuffer(ctx.device, b.handle, nullptr); b.handle = VK_NULL_HANDLE; }
    if (b.memory) { vkFreeMemory(ctx.device, b.memory, nullptr); b.memory = VK_NULL_HANDLE; }
    b.size = 0;
}

bool create_image_2d(const VkContext& ctx,
                     uint32_t width, uint32_t height,
                     VkFormat format, VkImageUsageFlags usage,
                     VkImageAspectFlags aspect, Image& out) noexcept {
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = { width, height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(ctx.device, &ici, nullptr, &out.handle) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(ctx.device, out.handle, &mr);

    auto ti = find_memory_type(ctx, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!ti) { vkDestroyImage(ctx.device, out.handle, nullptr); out.handle = VK_NULL_HANDLE; return false; }

    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = *ti;
    if (vkAllocateMemory(ctx.device, &mai, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyImage(ctx.device, out.handle, nullptr); out.handle = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(ctx.device, out.handle, out.memory, 0);

    VkImageViewCreateInfo ivci{};
    ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image    = out.handle;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format   = format;
    ivci.subresourceRange.aspectMask = aspect;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(ctx.device, &ivci, nullptr, &out.view) != VK_SUCCESS) {
        destroy_image(ctx, out);
        return false;
    }
    out.extent = { width, height, 1 };
    out.format = format;
    return true;
}

void destroy_image(const VkContext& ctx, Image& img) noexcept {
    if (img.view)   { vkDestroyImageView(ctx.device, img.view, nullptr);   img.view = VK_NULL_HANDLE; }
    if (img.handle) { vkDestroyImage(ctx.device, img.handle, nullptr);     img.handle = VK_NULL_HANDLE; }
    if (img.memory) { vkFreeMemory(ctx.device, img.memory, nullptr);       img.memory = VK_NULL_HANDLE; }
    img.extent = {};
    img.format = VK_FORMAT_UNDEFINED;
}

VkCommandBuffer begin_one_shot(const VkContext& ctx, VkCommandPool pool) noexcept {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(ctx.device, &ai, &cmd) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void end_one_shot(const VkContext& ctx, VkCommandPool pool, VkCommandBuffer cmd) noexcept {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx.graphics_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphics_queue);
    vkFreeCommandBuffers(ctx.device, pool, 1, &cmd);
}

bool upload_via_staging(const VkContext& ctx, VkCommandPool pool,
                        const void* src, VkDeviceSize bytes,
                        VkBuffer dst, VkDeviceSize dst_offset) noexcept {
    Buffer staging{};
    if (!create_buffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging))
        return false;
    std::memcpy(staging.mapped, src, bytes);

    VkCommandBuffer cmd = begin_one_shot(ctx, pool);
    if (!cmd) { destroy_buffer(ctx, staging); return false; }
    VkBufferCopy c{}; c.size = bytes; c.dstOffset = dst_offset;
    vkCmdCopyBuffer(cmd, staging.handle, dst, 1, &c);
    end_one_shot(ctx, pool, cmd);
    destroy_buffer(ctx, staging);
    return true;
}

bool upload_image_via_staging(const VkContext& ctx, VkCommandPool pool,
                              const void* src, VkDeviceSize bytes,
                              Image& img) noexcept {
    Buffer staging{};
    if (!create_buffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging))
        return false;
    std::memcpy(staging.mapped, src, bytes);

    VkCommandBuffer cmd = begin_one_shot(ctx, pool);
    if (!cmd) { destroy_buffer(ctx, staging); return false; }

    VkImageMemoryBarrier to_xfer{};
    to_xfer.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_xfer.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    to_xfer.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_xfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_xfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_xfer.image            = img.handle;
    to_xfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    to_xfer.srcAccessMask    = 0;
    to_xfer.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_xfer);

    VkBufferImageCopy bc{};
    bc.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    bc.imageExtent      = img.extent;
    vkCmdCopyBufferToImage(cmd, staging.handle, img.handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bc);

    VkImageMemoryBarrier to_shader = to_xfer;
    to_shader.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_shader);

    end_one_shot(ctx, pool, cmd);
    destroy_buffer(ctx, staging);
    return true;
}

} // namespace demen::renderer
