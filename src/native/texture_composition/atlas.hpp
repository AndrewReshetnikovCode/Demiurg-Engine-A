// =============================================================================
// atlas.hpp — in-memory atlas: alphabetical slot mapping, RGBA pixels,
// slot-stable reloads. Appendix F §F.3 + §F.5.
// =============================================================================
#pragma once

#include "texraw.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace demen::texture_composition {

constexpr uint32_t kTileSize = 128;   // Appendix H §H.4
constexpr uint32_t kChannels = 4;     // atlas is always RGBA

// kMaxMaterials caps how big a bindless atlas table we're willing to ship
// at Phase 2. 1024 is already 3× the Layer-5 "style-pool per culture × ~8
// cultures" worst case; well under any driver's descriptor-array limit.
constexpr uint32_t kMaxMaterials = 1024;

class Atlas {
public:
    Atlas() = default;

    // Scan assets_dir for *.texraw, assign alphabetical slot indices,
    // compose pixels into the flat RGBA buffer. Any corrupt .texraw fails
    // the whole load (invariant #7: refuse cleanly).
    // Returns one of DEMEN_TC_* result codes.
    int load(const std::filesystem::path& assets_dir);

    // Recompose preserving slot identity for materials that kept their name.
    // New materials get the next free slot; removed materials leave a dead
    // slot behind (zeroed pixels). Appendix F §F.5.
    int reload();

    // Accessors for the ABI.
    uint32_t tile_size()    const noexcept { return kTileSize; }
    uint32_t n_tiles()      const noexcept { return n_slots_live_; }
    uint32_t atlas_width()  const noexcept { return kTileSize; }
    uint32_t atlas_height() const noexcept { return kTileSize * n_tiles_allocated_; }
    uint32_t n_channels()   const noexcept { return kChannels; }

    const std::vector<uint8_t>& pixels() const noexcept { return pixels_; }

    // Material-slot lookups.
    bool material_slot(const std::string& name, uint32_t& out) const noexcept;
    bool material_name(uint32_t slot, std::string& out) const noexcept;

private:
    struct SlotRecord {
        std::string name;   // empty for dead slots
        bool        live;   // false for slots retired on reload
    };

    int scan_and_compose();

    std::filesystem::path                  dir_;
    std::vector<SlotRecord>                slots_;          // slot -> record
    std::unordered_map<std::string, uint32_t> name_to_slot_;
    std::vector<uint8_t>                   pixels_;         // RGBA, stacked
    uint32_t                               n_tiles_allocated_ = 0;
    uint32_t                               n_slots_live_ = 0;
};

} // namespace demen::texture_composition
