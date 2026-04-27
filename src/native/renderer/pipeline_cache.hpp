// =============================================================================
// pipeline_cache.hpp — persist the Vulkan pipeline cache across runs.
// Feeds invariant #5 (out-of-the-box cold launch ≤ 10 s).
// =============================================================================
#pragma once

#include "vk_instance.hpp"
#include <filesystem>

namespace demen::renderer {

// Load the pipeline cache blob from disk (if present) and return a Vulkan
// handle. Empty cache if the file is missing or the header doesn't match
// the current device.
VkPipelineCache load_pipeline_cache(const VkContext& ctx,
                                    const std::filesystem::path& path) noexcept;

// Save the current cache blob back to disk. Called on clean shutdown.
void save_pipeline_cache(const VkContext& ctx, VkPipelineCache cache,
                         const std::filesystem::path& path) noexcept;

} // namespace demen::renderer
