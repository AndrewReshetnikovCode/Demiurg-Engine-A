// =============================================================================
// demen/texture_composition.hpp — public API for the texture composition
// subsystem (§2.10). Planner-owned (invariant #4).
//
// Phase 2 surface: load a directory of .texraw placeholder textures, compose
// one atlas slot per material (background only — overlays and filters are
// format-supported but unused until Layer 5), expose the packed atlas pixel
// buffer to the renderer. Hot-reload is supported via demen_atlas_reload.
// =============================================================================
#pragma once

#include "demen/abi.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Result codes.
#define DEMEN_TC_OK               0
#define DEMEN_TC_ERR_IO           1
#define DEMEN_TC_ERR_CORRUPT      2   // bad .texraw magic / size
#define DEMEN_TC_ERR_NO_MATERIALS 3   // directory empty
#define DEMEN_TC_ERR_NOT_FOUND    4
#define DEMEN_TC_ERR_BUFFER_SIZE  5

typedef uint64_t demen_atlas_t;

// Summary of an atlas. `tile_size` is the per-material edge length (Phase 2:
// always 128). The atlas is a column-packed texture array at the renderer
// level, but at the ABI we hand back a single flat buffer:
//   atlas_width  = tile_size
//   atlas_height = tile_size * n_tiles
//   n_channels   = 4 (we always compose into RGBA, even if sources are RGB)
typedef struct demen_atlas_info {
    uint32_t tile_size;
    uint32_t n_tiles;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t n_channels;
} demen_atlas_info;

/// Build an atlas from every *.texraw in `assets_dir`. Materials are keyed by
/// filename stem; deterministic slot order is alphabetical by stem.
DEMEN_API int demen_atlas_create(const char* assets_dir, demen_atlas_t* out_atlas);

/// Re-read the asset directory and recompose. Used by the hot-reload path
/// (§2.10). Must complete in < 50 ms (Phase 2 gate).
DEMEN_API int demen_atlas_reload(demen_atlas_t atlas);

DEMEN_API int demen_atlas_info_get(demen_atlas_t atlas, demen_atlas_info* out_info);

/// Copy the RGBA pixel buffer into caller-provided memory. Buffer size must
/// equal `atlas_width * atlas_height * n_channels`. Used by the renderer to
/// upload to a Vulkan image.
DEMEN_API int demen_atlas_copy_pixels(demen_atlas_t atlas,
                                      uint8_t* out_buffer,
                                      uint32_t buffer_len_bytes);

/// Look up the slot index for a material by name. Returns DEMEN_TC_ERR_NOT_FOUND
/// if the name doesn't exist.
DEMEN_API int demen_atlas_material_slot(demen_atlas_t atlas,
                                        const char* material_name,
                                        uint32_t* out_slot);

/// Look up the material name at a slot. `out_name` must point to at least
/// `DEMEN_TC_MAX_NAME` bytes; the copy is NUL-terminated.
#define DEMEN_TC_MAX_NAME 64
DEMEN_API int demen_atlas_material_name(demen_atlas_t atlas,
                                        uint32_t slot,
                                        char* out_name);

DEMEN_API int demen_atlas_release(demen_atlas_t atlas);

#ifdef __cplusplus
}
#endif
