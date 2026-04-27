// =============================================================================
// region_io.cpp — region file reader + writer per Appendix A.
// =============================================================================
#include "region_io.hpp"
#include "world.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace demen::voxel_store {

// -----------------------------------------------------------------------------
// Endianness — little-endian only (Appendix A §A.7.2). Enforced at compile
// time so the reader can straight-memcpy.
// -----------------------------------------------------------------------------
#if defined(__cpp_lib_endian)
#include <bit>
static_assert(std::endian::native == std::endian::little,
    "DemEn on-disk format requires a little-endian host (Appendix A §A.7.2).");
#endif

// -----------------------------------------------------------------------------
// File-layout constants (Appendix A §A.3).
// -----------------------------------------------------------------------------
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kRegionEdge = DEMEN_REGION_EDGE_CHUNKS; // 32
constexpr uint32_t kColumnsPerRegion = kRegionEdge * kRegionEdge; // 1024
constexpr size_t   kHeaderSize = kSectorSize * 2;          // 8 KiB including tables

static constexpr std::array<char, 8> kMagic = {'D','E','M','E','N',0,0,0};

// -----------------------------------------------------------------------------
// floor-divide helpers (duplicated small — not worth a shared header).
// -----------------------------------------------------------------------------
static int32_t floordiv(int32_t a, int32_t b) {
    int32_t q = a / b;
    int32_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) --q;
    return q;
}
static uint32_t floormod(int32_t a, int32_t b) {
    int32_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) r += b;
    return static_cast<uint32_t>(r);
}

// -----------------------------------------------------------------------------
// Region key = (region_x, region_z). A region covers 32 chunk columns * 32.
// -----------------------------------------------------------------------------
struct RegionKey {
    int32_t rx;
    int32_t rz;
    bool operator<(const RegionKey& o) const noexcept {
        return (rx != o.rx) ? (rx < o.rx) : (rz < o.rz);
    }
};

static RegionKey region_key_of_column(int32_t cx, int32_t cz) {
    return { floordiv(cx, static_cast<int32_t>(kRegionEdge)),
             floordiv(cz, static_cast<int32_t>(kRegionEdge)) };
}

static fs::path region_path(const fs::path& dir, RegionKey k) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "r.%d.%d.dem", k.rx, k.rz);
    return dir / buf;
}

// -----------------------------------------------------------------------------
// temp+rename write helper (§A.7.3).
// -----------------------------------------------------------------------------
static int write_atomic(const fs::path& final_path, const std::vector<uint8_t>& bytes) {
    fs::path tmp = final_path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return DEMEN_VS_ERR_IO;
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!f) return DEMEN_VS_ERR_IO;
        // std::ofstream has no direct fsync; rely on std::filesystem::rename
        // being atomic on NTFS/ext4. The §A.7.3 "fsync + rename" durability
        // guarantee lands in Phase 8 when we wire a small platform helper.
    }
    std::error_code ec;
    fs::rename(tmp, final_path, ec);
    if (ec) {
        // Try copy-then-remove as a fallback across filesystem boundaries.
        fs::remove(final_path, ec);
        fs::rename(tmp, final_path, ec);
        if (ec) return DEMEN_VS_ERR_IO;
    }
    return DEMEN_VS_OK;
}

// -----------------------------------------------------------------------------
// Sector utilities
// -----------------------------------------------------------------------------
static uint32_t sectors_for(size_t bytes) {
    return static_cast<uint32_t>((bytes + kSectorSize - 1) / kSectorSize);
}

// =============================================================================
// Write path
// =============================================================================

// Serialise a chunk column into a single decompressed payload per §A.4.
static std::vector<uint8_t> serialize_column(const ChunkColumn& col) {
    // Payload layout (§A.4.0):
    //   u16 num_chunks
    //   i16 base_chunk_y
    //   u8  column_cells[8 * 1024] = ColumnCell[1024]
    //   for each chunk: PalettedChunkV1
    std::vector<uint8_t> out;
    const uint32_t n = col.n_chunks();
    out.resize(2 + 2 + DEMEN_COLUMN_CELLS * sizeof(demen_column_cell));
    std::memcpy(&out[0], &n, 2);
    int16_t base = static_cast<int16_t>(col.base_cy());
    std::memcpy(&out[2], &base, 2);
    std::memcpy(&out[4], col.cells_raw(),
                DEMEN_COLUMN_CELLS * sizeof(demen_column_cell));

    for (uint32_t ci = 0; ci < n; ++ci) {
        const auto& chk = col.chunk_ref(ci);
        const size_t sz = chk.serialized_size();
        const size_t prev = out.size();
        out.resize(prev + sz);
        chk.serialize_into(out.data() + prev);
    }
    return out;
}

static bool deserialize_column(const uint8_t* in, size_t size, ChunkColumn& col) {
    if (size < 4 + DEMEN_COLUMN_CELLS * sizeof(demen_column_cell)) return false;
    uint16_t n;
    int16_t  base;
    std::memcpy(&n, in, 2);
    std::memcpy(&base, in + 2, 2);
    if (n == 0 || n > kColumnMaxChunks) return false;
    if (n != col.n_chunks()) return false;           // column shape fixed
    if (base != col.base_cy()) return false;

    // cells_raw() has a non-const overload on non-const ChunkColumn& — no
    // const_cast needed; the overload set picks the writable pointer.
    std::memcpy(col.cells_raw(), in + 4,
                DEMEN_COLUMN_CELLS * sizeof(demen_column_cell));

    size_t off = 4 + DEMEN_COLUMN_CELLS * sizeof(demen_column_cell);
    for (uint32_t ci = 0; ci < n; ++ci) {
        // Read the PalettedChunk fields to determine this chunk's size, then
        // hand the raw buffer to PalettedChunk::deserialize_from.
        if (off + 4 > size) return false;
        const uint8_t bits = in[off];
        const uint8_t psz  = in[off + 1];
        const size_t idx_bytes = (bits == 0) ? 0 :
            static_cast<size_t>(DEMEN_CHUNK_VOXELS) * bits / 8;
        const size_t chunk_size = 4 + static_cast<size_t>(psz) * 2 + idx_bytes;
        if (off + chunk_size > size) return false;
        if (!col.chunk_ref(ci).deserialize_from(in + off, chunk_size)) return false;
        off += chunk_size;
    }
    return off == size;
}

// -----------------------------------------------------------------------------
// Read an existing region file (or return empty). Always returns a buffer
// large enough for the header + index + size tables; payloads append.
// -----------------------------------------------------------------------------
struct RegionFileInMemory {
    std::vector<uint8_t> bytes;
    std::array<uint32_t, kColumnsPerRegion> index{};     // sector offsets
    std::array<uint16_t, kColumnsPerRegion> size_sectors{}; // sector counts
    demen_world_params params{};
    bool has_header = false;
};

static int read_region_file(const fs::path& p, RegionFileInMemory& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return DEMEN_VS_ERR_IO;
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    out.bytes.assign(size, 0);
    f.read(reinterpret_cast<char*>(out.bytes.data()),
           static_cast<std::streamsize>(size));
    if (!f) return DEMEN_VS_ERR_IO;

    if (size < kHeaderSize) return DEMEN_VS_ERR_CORRUPT;
    if (std::memcmp(out.bytes.data(), kMagic.data(), 8) != 0) return DEMEN_VS_ERR_CORRUPT;
    if (out.bytes[8] != DEMEN_REGION_FORMAT_VERSION) return DEMEN_VS_ERR_VERSION_MISMATCH;

    // World-level params live in the root header (rx=0, rz=0). We store
    // them in every region's header too for easier forensic recovery.
    std::memcpy(&out.params.rng_seed,   &out.bytes[0x14], 4);
    std::memcpy(&out.params.world_id,   &out.bytes[0x18], 8);
    // Remaining fields (bounds, scale) are world-level, stored only in the
    // root-region header. Read them there, not here.

    std::memcpy(out.index.data(),        &out.bytes[0x0020], 1024 * 4);
    std::memcpy(out.size_sectors.data(), &out.bytes[0x1020], 1024 * 2);
    out.has_header = true;
    return DEMEN_VS_OK;
}

// Build a minimal empty region header as a new file buffer.
static RegionFileInMemory new_empty_region(const demen_world_params& params) {
    RegionFileInMemory r;
    r.bytes.assign(kHeaderSize, 0);
    std::memcpy(r.bytes.data(), kMagic.data(), 8);
    r.bytes[8] = DEMEN_REGION_FORMAT_VERSION;
    r.bytes[9] = 0;  // flags: no compression
    std::memcpy(&r.bytes[0x14], &params.rng_seed, 4);
    std::memcpy(&r.bytes[0x18], &params.world_id, 8);
    r.has_header = true;
    r.params = params;
    return r;
}

// =============================================================================
// Public API
// =============================================================================

int region_write_root_header(const fs::path& dir, const demen_world_params& params) {
    fs::create_directories(dir);
    region_clean_stale_tmp(dir);

    auto region = new_empty_region(params);
    // Root region: additionally stash world bounds + scale at an appendix
    // offset inside the header's reserved-padding block (0x1820 onwards).
    // §A.3 reserves 0x1820..0x2000 as padding — we reuse its tail, so a
    // reader built against §A.3 alone sees a well-formed empty region.
    uint8_t* tail = &region.bytes[0x1E00];  // 512 bytes at the tail
    std::memcpy(tail + 0,  &params.scale,              4);
    std::memcpy(tail + 4,  &params.bounds_min_chunk_x, 4);
    std::memcpy(tail + 8,  &params.bounds_min_chunk_z, 4);
    std::memcpy(tail + 12, &params.bounds_max_chunk_x, 4);
    std::memcpy(tail + 16, &params.bounds_max_chunk_z, 4);
    std::memcpy(tail + 20, &params.min_chunk_y,        4);
    std::memcpy(tail + 24, &params.max_chunk_y,        4);
    // Marker to distinguish a root-header extension from random padding.
    const char mk[8] = {'D','E','M','R','O','O','T',0};
    std::memcpy(tail + 28, mk, 8);

    return write_atomic(region_path(dir, {0, 0}), region.bytes);
}

int region_read_root_header(const fs::path& dir, demen_world_params* out_params) {
    if (!out_params) return DEMEN_VS_ERR_IO;
    RegionFileInMemory r;
    int rc = read_region_file(region_path(dir, {0, 0}), r);
    if (rc != DEMEN_VS_OK) return rc;
    *out_params = r.params;

    const char mk[8] = {'D','E','M','R','O','O','T',0};
    const uint8_t* tail = &r.bytes[0x1E00];
    if (r.bytes.size() >= 0x1E00 + 36 && std::memcmp(tail + 28, mk, 8) == 0) {
        std::memcpy(&out_params->scale,              tail + 0,  4);
        std::memcpy(&out_params->bounds_min_chunk_x, tail + 4,  4);
        std::memcpy(&out_params->bounds_min_chunk_z, tail + 8,  4);
        std::memcpy(&out_params->bounds_max_chunk_x, tail + 12, 4);
        std::memcpy(&out_params->bounds_max_chunk_z, tail + 16, 4);
        std::memcpy(&out_params->min_chunk_y,        tail + 20, 4);
        std::memcpy(&out_params->max_chunk_y,        tail + 24, 4);
    }
    return DEMEN_VS_OK;
}

void region_clean_stale_tmp(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".tmp") {
            fs::remove(e.path(), ec);
        }
    }
}

bool region_load_column_if_exists(const fs::path& dir, ChunkColumn& col) {
    const auto rk = region_key_of_column(col.cx(), col.cz());
    RegionFileInMemory r;
    if (read_region_file(region_path(dir, rk), r) != DEMEN_VS_OK) return false;

    const uint32_t slot_x = floormod(col.cx(), static_cast<int32_t>(kRegionEdge));
    const uint32_t slot_z = floormod(col.cz(), static_cast<int32_t>(kRegionEdge));
    const uint32_t slot   = slot_z * kRegionEdge + slot_x;
    const uint32_t sect_off = r.index[slot];
    const uint16_t n_sectors = r.size_sectors[slot];
    if (sect_off == 0 || n_sectors == 0) return false;

    const size_t blob_off = static_cast<size_t>(sect_off) * kSectorSize;
    if (blob_off + 12 > r.bytes.size()) return false;
    uint32_t payload_size = 0, compressed_size = 0;
    std::memcpy(&payload_size,    &r.bytes[blob_off + 0], 4);
    std::memcpy(&compressed_size, &r.bytes[blob_off + 4], 4);
    const uint8_t comp = r.bytes[blob_off + 8];
    if (comp != 0) return false;   // Phase 1: compression=0 only.
    if (blob_off + 12 + compressed_size > r.bytes.size()) return false;
    if (payload_size != compressed_size) return false;
    return deserialize_column(&r.bytes[blob_off + 12], payload_size, col);
}

int region_write_all_dirty_columns(const fs::path& dir, World& world) {
    // Group columns by region.
    std::map<RegionKey, std::vector<ChunkColumn*>> by_region;
    world.for_each_column([&](ChunkColumn& col) {
        by_region[region_key_of_column(col.cx(), col.cz())].push_back(&col);
    });

    // Ensure root exists (needed so open() has a header to probe even if
    // nothing was edited in region (0,0) itself).
    const fs::path root = region_path(dir, {0, 0});
    if (!fs::exists(root)) {
        region_write_root_header(dir, world.params());
    }

    for (auto& [rk, cols] : by_region) {
        const fs::path p = region_path(dir, rk);
        RegionFileInMemory r;
        if (fs::exists(p)) {
            if (read_region_file(p, r) != DEMEN_VS_OK) {
                r = new_empty_region(world.params());
            }
        } else {
            r = new_empty_region(world.params());
        }

        // Lay out: keep the existing header + tables bytes, but recompute
        // the payload region by serialising every column we have in memory.
        // Columns not in memory retain their previous slot pointer (we do
        // NOT rewrite payloads we didn't touch — §2.11 rung 1, don't do work
        // we don't need).

        // Build new payloads for the columns we own, then pack sequentially
        // after the header.
        std::vector<uint8_t> header_and_tables(kHeaderSize);
        std::memcpy(header_and_tables.data(), r.bytes.data(), kHeaderSize);

        // Start with the existing index/size tables; we'll patch entries for
        // the columns we re-serialise. Everything else keeps its pointers —
        // but those pointers are offsets into the OLD file, which is about
        // to be overwritten. For a correct Phase 1 implementation, we copy
        // unchanged blobs across too. Keep it simple: build a fresh file
        // containing every known column in this region.
        std::array<uint32_t, kColumnsPerRegion> new_index{};
        std::array<uint16_t, kColumnsPerRegion> new_size{};

        std::vector<uint8_t> payloads;
        payloads.reserve(cols.size() * 64 * 1024);

        // Re-emit columns that were already on disk but aren't in memory.
        for (uint32_t slot = 0; slot < kColumnsPerRegion; ++slot) {
            if (r.index[slot] == 0 || r.size_sectors[slot] == 0) continue;
            const size_t old_off = static_cast<size_t>(r.index[slot]) * kSectorSize;
            if (old_off + 12 > r.bytes.size()) continue;
            uint32_t ps = 0, cs = 0;
            std::memcpy(&ps, &r.bytes[old_off + 0], 4);
            std::memcpy(&cs, &r.bytes[old_off + 4], 4);
            if (old_off + 12 + cs > r.bytes.size()) continue;

            // Will we overwrite this column with an in-memory one below?
            const uint32_t sx = slot % kRegionEdge;
            const uint32_t sz = slot / kRegionEdge;
            const int32_t  cx = rk.rx * static_cast<int32_t>(kRegionEdge) + static_cast<int32_t>(sx);
            const int32_t  cz = rk.rz * static_cast<int32_t>(kRegionEdge) + static_cast<int32_t>(sz);
            bool will_rewrite = false;
            for (ChunkColumn* col : cols) {
                if (col->cx() == cx && col->cz() == cz) { will_rewrite = true; break; }
            }
            if (will_rewrite) continue;

            // Pad to sector boundary, write header triplet + the old payload.
            while (payloads.size() % kSectorSize != 0) payloads.push_back(0);
            const size_t this_off_bytes = kHeaderSize + payloads.size();
            new_index[slot] = static_cast<uint32_t>(this_off_bytes / kSectorSize);
            const uint8_t hdr[12] = {0};
            payloads.insert(payloads.end(), hdr, hdr + 12);
            std::memcpy(&payloads[payloads.size() - 12] + 0, &ps, 4);
            std::memcpy(&payloads[payloads.size() - 12] + 4, &cs, 4);
            payloads[payloads.size() - 12 + 8] = 0; // compression=0
            payloads.insert(payloads.end(),
                            &r.bytes[old_off + 12],
                            &r.bytes[old_off + 12 + cs]);
            const size_t end_bytes = kHeaderSize + payloads.size();
            new_size[slot] = static_cast<uint16_t>(
                sectors_for(end_bytes - (static_cast<size_t>(new_index[slot]) * kSectorSize)));
        }

        // Emit in-memory columns.
        for (ChunkColumn* col : cols) {
            const uint32_t sx = floormod(col->cx(), static_cast<int32_t>(kRegionEdge));
            const uint32_t sz = floormod(col->cz(), static_cast<int32_t>(kRegionEdge));
            const uint32_t slot = sz * kRegionEdge + sx;

            std::vector<uint8_t> payload = serialize_column(*col);
            while (payloads.size() % kSectorSize != 0) payloads.push_back(0);
            const size_t this_off_bytes = kHeaderSize + payloads.size();
            new_index[slot] = static_cast<uint32_t>(this_off_bytes / kSectorSize);

            const uint32_t ps = static_cast<uint32_t>(payload.size());
            const uint32_t cs = ps;
            uint8_t triplet[12] = {0};
            std::memcpy(&triplet[0], &ps, 4);
            std::memcpy(&triplet[4], &cs, 4);
            triplet[8] = 0;              // compression=0 (Phase 1)
            triplet[9] = 1;              // column_version = 1
            payloads.insert(payloads.end(), triplet, triplet + 12);
            payloads.insert(payloads.end(), payload.begin(), payload.end());

            const size_t end_bytes = kHeaderSize + payloads.size();
            new_size[slot] = static_cast<uint16_t>(
                sectors_for(end_bytes - (static_cast<size_t>(new_index[slot]) * kSectorSize)));
        }

        // Stitch header + tables + payloads into the final file.
        std::vector<uint8_t> final_bytes = header_and_tables;
        // Patch the index/size tables.
        std::memcpy(&final_bytes[0x0020], new_index.data(), 1024 * 4);
        std::memcpy(&final_bytes[0x1020], new_size.data(),  1024 * 2);
        final_bytes.insert(final_bytes.end(), payloads.begin(), payloads.end());
        // Pad to sector multiple.
        while (final_bytes.size() % kSectorSize != 0) final_bytes.push_back(0);

        int rc = write_atomic(p, final_bytes);
        if (rc != DEMEN_VS_OK) return rc;
    }
    return DEMEN_VS_OK;
}

} // namespace demen::voxel_store
