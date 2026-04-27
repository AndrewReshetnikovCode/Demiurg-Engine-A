// =============================================================================
// texture_composition_abi.cpp — C ABI surface (§texture_composition.hpp).
// Handle registry + delegation to Atlas. No exceptions escape.
// =============================================================================
#include "demen/texture_composition.hpp"

#include "atlas.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace demen::texture_composition;

namespace {

std::mutex g_mu;
std::unordered_map<uint64_t, std::unique_ptr<Atlas>> g_atlases;
std::atomic<uint64_t> g_next_id{1};

Atlas* atlas_from(demen_atlas_t h) noexcept {
    std::lock_guard lk(g_mu);
    auto it = g_atlases.find(h);
    return it == g_atlases.end() ? nullptr : it->second.get();
}

} // namespace

extern "C" {

DEMEN_API int demen_atlas_create(const char* assets_dir, demen_atlas_t* out_atlas) {
    if (!assets_dir || !out_atlas) return DEMEN_TC_ERR_IO;
    *out_atlas = 0;
    auto atlas = std::make_unique<Atlas>();
    const int rc = atlas->load(std::filesystem::path(assets_dir));
    if (rc != DEMEN_TC_OK) return rc;

    const uint64_t id = g_next_id.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lk(g_mu);
        g_atlases.emplace(id, std::move(atlas));
    }
    *out_atlas = id;
    return DEMEN_TC_OK;
}

DEMEN_API int demen_atlas_reload(demen_atlas_t atlas) {
    Atlas* a = atlas_from(atlas);
    if (!a) return DEMEN_TC_ERR_IO;
    return a->reload();
}

DEMEN_API int demen_atlas_info_get(demen_atlas_t atlas, demen_atlas_info* out) {
    if (!out) return DEMEN_TC_ERR_IO;
    Atlas* a = atlas_from(atlas);
    if (!a) return DEMEN_TC_ERR_IO;
    out->tile_size    = a->tile_size();
    out->n_tiles      = a->n_tiles();
    out->atlas_width  = a->atlas_width();
    out->atlas_height = a->atlas_height();
    out->n_channels   = a->n_channels();
    return DEMEN_TC_OK;
}

DEMEN_API int demen_atlas_copy_pixels(demen_atlas_t atlas,
                                      uint8_t* out_buffer,
                                      uint32_t buffer_len_bytes) {
    if (!out_buffer) return DEMEN_TC_ERR_IO;
    Atlas* a = atlas_from(atlas);
    if (!a) return DEMEN_TC_ERR_IO;
    const auto& px = a->pixels();
    if (buffer_len_bytes < px.size()) return DEMEN_TC_ERR_BUFFER_SIZE;
    std::memcpy(out_buffer, px.data(), px.size());
    return DEMEN_TC_OK;
}

DEMEN_API int demen_atlas_material_slot(demen_atlas_t atlas,
                                        const char* material_name,
                                        uint32_t* out_slot) {
    if (!material_name || !out_slot) return DEMEN_TC_ERR_IO;
    Atlas* a = atlas_from(atlas);
    if (!a) return DEMEN_TC_ERR_IO;
    uint32_t s = 0;
    if (!a->material_slot(material_name, s)) return DEMEN_TC_ERR_NOT_FOUND;
    *out_slot = s;
    return DEMEN_TC_OK;
}

DEMEN_API int demen_atlas_material_name(demen_atlas_t atlas,
                                        uint32_t slot,
                                        char* out_name) {
    if (!out_name) return DEMEN_TC_ERR_IO;
    Atlas* a = atlas_from(atlas);
    if (!a) return DEMEN_TC_ERR_IO;
    std::string n;
    if (!a->material_name(slot, n)) return DEMEN_TC_ERR_NOT_FOUND;
    // DEMEN_TC_MAX_NAME = 64; NUL-terminated.
    const size_t copy_n = std::min<size_t>(n.size(), DEMEN_TC_MAX_NAME - 1);
    std::memcpy(out_name, n.data(), copy_n);
    out_name[copy_n] = '\0';
    return DEMEN_TC_OK;
}

DEMEN_API int demen_atlas_release(demen_atlas_t atlas) {
    if (atlas == 0) return DEMEN_TC_OK;
    std::lock_guard lk(g_mu);
    g_atlases.erase(atlas);
    return DEMEN_TC_OK;
}

} // extern "C"
