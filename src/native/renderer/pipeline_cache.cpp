// =============================================================================
// pipeline_cache.cpp — plain file + VkPipelineCacheCreateInfo.
// =============================================================================
#include "pipeline_cache.hpp"

#include <fstream>
#include <vector>

namespace demen::renderer {

VkPipelineCache load_pipeline_cache(const VkContext& ctx,
                                    const std::filesystem::path& path) noexcept {
    std::vector<uint8_t> blob;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (f) {
            const size_t n = static_cast<size_t>(f.tellg());
            blob.resize(n);
            f.seekg(0);
            f.read(reinterpret_cast<char*>(blob.data()),
                   static_cast<std::streamsize>(n));
        }
    }
    VkPipelineCacheCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    ci.initialDataSize = blob.size();
    ci.pInitialData    = blob.empty() ? nullptr : blob.data();
    VkPipelineCache cache = VK_NULL_HANDLE;
    vkCreatePipelineCache(ctx.device, &ci, nullptr, &cache);
    return cache;
}

void save_pipeline_cache(const VkContext& ctx, VkPipelineCache cache,
                         const std::filesystem::path& path) noexcept {
    if (!cache) return;
    size_t n = 0;
    vkGetPipelineCacheData(ctx.device, cache, &n, nullptr);
    if (n == 0) return;
    std::vector<uint8_t> blob(n);
    vkGetPipelineCacheData(ctx.device, cache, &n, blob.data());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(blob.data()),
            static_cast<std::streamsize>(blob.size()));
}

} // namespace demen::renderer
