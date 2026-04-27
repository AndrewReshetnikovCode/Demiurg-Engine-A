// =============================================================================
// atlas.cpp — load, recompose, slot-stable reload.
// Phase 2 shape: background only (§2.10; §F.3). Overlay + filter refused.
// =============================================================================
#include "atlas.hpp"
#include "demen/texture_composition.hpp"

#include <algorithm>
#include <cstring>

namespace demen::texture_composition {

namespace {

// Find every .texraw stem in `dir`, alphabetically.
std::vector<std::string> enumerate_stems(const std::filesystem::path& dir) {
    std::vector<std::string> stems;
    std::error_code ec;
    auto it = std::filesystem::directory_iterator(dir, ec);
    if (ec) return stems;
    for (auto& entry : it) {
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        if (p.extension() != ".texraw") continue;
        stems.push_back(p.stem().string());
    }
    std::sort(stems.begin(), stems.end());
    return stems;
}

// Widen 3-channel RGB source to the atlas's 4-channel RGBA, with alpha=255.
// Source is tile-sized; dst is the atlas tile slice. Phase 2 ignores
// overlays + filters (§F.4 short-circuit).
void widen_rgb_to_rgba_tile(const uint8_t* src, uint32_t src_ch,
                            uint8_t* dst) noexcept {
    const size_t n = static_cast<size_t>(kTileSize) * kTileSize;
    if (src_ch == 4) {
        std::memcpy(dst, src, n * 4);
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        dst[i * 4 + 0] = src[i * 3 + 0];
        dst[i * 4 + 1] = src[i * 3 + 1];
        dst[i * 4 + 2] = src[i * 3 + 2];
        dst[i * 4 + 3] = 0xFF;
    }
}

} // namespace

int Atlas::load(const std::filesystem::path& assets_dir) {
    dir_ = assets_dir;
    slots_.clear();
    name_to_slot_.clear();
    pixels_.clear();
    n_tiles_allocated_ = 0;
    n_slots_live_      = 0;
    return scan_and_compose();
}

int Atlas::reload() {
    if (dir_.empty()) return DEMEN_TC_ERR_IO;
    return scan_and_compose();
}

int Atlas::scan_and_compose() {
    const auto stems = enumerate_stems(dir_);
    if (stems.empty()) return DEMEN_TC_ERR_NO_MATERIALS;

    // Slot identity rule (§F.5): materials that kept their name keep their
    // slot. New names get the next free slot (which may be a previously-
    // retired dead slot, to keep the bindless table dense but stable as to
    // existing names). Removed names leave dead slots.

    // Step 1: mark every existing slot as tentatively dead; we'll flip those
    // that still have a matching .texraw back to live.
    for (auto& s : slots_) s.live = false;

    std::vector<std::pair<std::string, uint32_t>> to_fill; // stem, slot
    to_fill.reserve(stems.size());

    for (const auto& stem : stems) {
        auto it = name_to_slot_.find(stem);
        if (it != name_to_slot_.end()) {
            slots_[it->second].live = true;
            to_fill.push_back({stem, it->second});
        } else {
            // Find a free (dead) slot or append.
            uint32_t slot = UINT32_MAX;
            for (uint32_t i = 0; i < slots_.size(); ++i) {
                if (!slots_[i].live && slots_[i].name.empty()) {
                    slot = i; break;
                }
            }
            if (slot == UINT32_MAX) {
                if (slots_.size() >= kMaxMaterials) return DEMEN_TC_ERR_BUFFER_SIZE;
                slot = static_cast<uint32_t>(slots_.size());
                slots_.push_back({stem, true});
            } else {
                slots_[slot] = {stem, true};
            }
            name_to_slot_.emplace(stem, slot);
            to_fill.push_back({stem, slot});
        }
    }

    // Step 2: purge truly-dead slots' names from the lookup table so the
    // free-slot scan above can find them next reload.
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].live && !slots_[i].name.empty()) {
            name_to_slot_.erase(slots_[i].name);
            slots_[i].name.clear();
        }
    }

    // Step 3: size the pixel buffer.
    n_tiles_allocated_ = static_cast<uint32_t>(slots_.size());
    const size_t tile_bytes = static_cast<size_t>(kTileSize) * kTileSize * kChannels;
    pixels_.assign(tile_bytes * n_tiles_allocated_, 0);  // zeroed (dead slots stay black)

    // Step 4: load + compose live slots.
    for (auto& [stem, slot] : to_fill) {
        TexRaw raw;
        if (!load_texraw(dir_ / (stem + ".texraw"), raw)) {
            return DEMEN_TC_ERR_CORRUPT;
        }
        if (raw.width != kTileSize || raw.height != kTileSize) {
            return DEMEN_TC_ERR_CORRUPT;  // Phase 2 is tile-size-locked
        }
        widen_rgb_to_rgba_tile(raw.pixels.data(), raw.channels,
                               pixels_.data() + static_cast<size_t>(slot) * tile_bytes);
    }

    n_slots_live_ = 0;
    for (auto& s : slots_) if (s.live) ++n_slots_live_;
    return DEMEN_TC_OK;
}

bool Atlas::material_slot(const std::string& name, uint32_t& out) const noexcept {
    auto it = name_to_slot_.find(name);
    if (it == name_to_slot_.end()) return false;
    if (!slots_[it->second].live) return false;  // dead slot: refuse
    out = it->second;
    return true;
}

bool Atlas::material_name(uint32_t slot, std::string& out) const noexcept {
    if (slot >= slots_.size()) return false;
    if (!slots_[slot].live)    return false;
    out = slots_[slot].name;
    return true;
}

} // namespace demen::texture_composition
