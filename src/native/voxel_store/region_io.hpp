// =============================================================================
// region_io.hpp — region-file serialisation per Appendix A v1.0.
//
// All functions are single-threaded. Compression is `none` in Phase 1
// (§A.4 compression byte = 0). LZ4 lands later only if a benchmark shows
// it's needed — §2.11, invariant #8.
// =============================================================================
#pragma once

#include "column.hpp"
#include "demen/voxel_store.hpp"

#include <cstdint>
#include <filesystem>

namespace demen::voxel_store {

class World;

// Writes (or re-writes) the root region header file. Called by
// demen_world_create to materialise a new world directory. Uses the temp-
// file + fsync + atomic-rename pattern from §A.7.3.
int region_write_root_header(const std::filesystem::path& dir,
                             const demen_world_params& params);

// Reads the root region header and validates magic + version byte
// (invariant #7). Populates `out_params` on success.
int region_read_root_header(const std::filesystem::path& dir,
                            demen_world_params* out_params);

// Load a single column from the region file that covers its (cx, cz), or
// return false if the region file doesn't exist / doesn't contain the column.
// On true, fills `col` with palette-chunks + live cells.
bool region_load_column_if_exists(const std::filesystem::path& dir,
                                  ChunkColumn& col);

// Write every live column in the world out to disk. Writes whole region
// files at a time — Phase 1 doesn't split-write within a region. Each
// file uses temp+rename so a crash mid-write never corrupts the prior save.
int region_write_all_dirty_columns(const std::filesystem::path& dir,
                                   World& world);

// On startup, remove any orphan *.tmp files that might be left over from an
// aborted write (§A.7.3 step 4).
void region_clean_stale_tmp(const std::filesystem::path& dir);

} // namespace demen::voxel_store
