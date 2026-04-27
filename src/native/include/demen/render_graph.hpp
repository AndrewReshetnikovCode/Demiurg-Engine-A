// =============================================================================
// demen/render_graph.hpp — Phase 3 public API for the Vulkan render graph.
// Planner-owned (invariant #4). Supersedes the Phase 0 demen_run_phase0_window
// entry point; that function is now deprecated and returns DEMEN_RG_OK
// immediately.
//
// Design references:
//   - DESIGN.md §2.6  (Vulkan 1.3, bindless, indirect, triple-buffer,
//                      opaque + transparent + post-process passes)
//   - Appendix C      (pipeline layouts, descriptor sets, push constants)
//   - Invariants #1 (no managed allocs in game loop), #5 (cold launch)
// =============================================================================
#pragma once

#include "demen/abi.hpp"
#include "demen/meshing.hpp"
#include "demen/texture_composition.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Result codes.
#define DEMEN_RG_OK                        0
#define DEMEN_RG_ERR_GLFW_INIT             1
#define DEMEN_RG_ERR_NO_VULKAN             2
#define DEMEN_RG_ERR_NO_DEVICE             3
#define DEMEN_RG_ERR_NO_SURFACE            4
#define DEMEN_RG_ERR_SWAPCHAIN             5
#define DEMEN_RG_ERR_PIPELINE              6
#define DEMEN_RG_ERR_SHADER_LOAD           7
#define DEMEN_RG_ERR_RUNTIME               8
#define DEMEN_RG_ERR_OOM                   9
#define DEMEN_RG_ERR_INVALID_HANDLE       10
#define DEMEN_RG_ERR_BUFFER_SIZE          11

typedef uint64_t demen_render_graph_t;

// Camera state. Blittable. The managed side updates this each frame; the
// renderer samples the latest copy when recording. Right-handed, Y-up.
typedef struct demen_camera {
    float pos[3];
    float forward[3];
    float up[3];
    float fov_radians;
    float aspect;
    float near_plane;
    float far_plane;
} demen_camera;

// Frame input. All blittable; the renderer copies it at frame-submit time.
// `tick` is the deterministic sim tick (invariant #2); recorded with frame
// traces so a replay hash can be tied to frame N.
typedef struct demen_frame_input {
    demen_camera camera;
    uint64_t     tick;
    float        time_seconds;
    uint32_t     window_width;
    uint32_t     window_height;
    uint32_t     _reserved;
} demen_frame_input;

// ----------------------------------------------------------------------------
// Lifecycle. One render graph per window.
// ----------------------------------------------------------------------------

DEMEN_API int demen_render_graph_create(uint32_t width, uint32_t height,
                                        demen_render_graph_t* out);

DEMEN_API int demen_render_graph_destroy(demen_render_graph_t rg);

/// Attach the texture atlas to the renderer. Uploads pixels once; the
/// renderer retains a GPU image. Calling again replaces the previous atlas
/// (e.g., after demen_atlas_reload on the composition side).
DEMEN_API int demen_render_graph_set_atlas(demen_render_graph_t rg,
                                           demen_atlas_t atlas);

/// Register a built mesh with the renderer. The renderer copies vertex +
/// index data into a resident GPU buffer slot. `out_slot` is the
/// draw-call-side handle used by demen_render_graph_submit_draw.
DEMEN_API int demen_render_graph_upload_mesh(demen_render_graph_t rg,
                                             demen_mesh_t mesh,
                                             uint32_t* out_slot);

/// Forget a mesh upload. Its slot is reclaimed on the next frame boundary
/// so in-flight frames do not read freed memory.
DEMEN_API int demen_render_graph_drop_mesh(demen_render_graph_t rg,
                                           uint32_t slot);

/// World-space transform for a mesh instance. Phase 3 carries translation
/// only; rotation arrives with the toys (Phase 4). Keeping it mat4-shaped
/// now so toys don't force a header bump.
typedef struct demen_mesh_instance {
    uint32_t mesh_slot;
    uint32_t atlas_override;    // 0xFFFFFFFF = use the vertex's atlas_slot
    float    world_xform[16];   // column-major 4x4
} demen_mesh_instance;

/// Submit one frame. The managed side gathers its mesh instance list each
/// tick and hands it to the renderer wholesale. Ownership: the renderer
/// copies everything before returning.
DEMEN_API int demen_render_graph_submit_frame(demen_render_graph_t rg,
                                              const demen_frame_input* input,
                                              const demen_mesh_instance* instances,
                                              uint32_t instance_count);

/// Pump the OS window once. Returns 1 if the window should close, 0 to
/// continue. Pulled out of submit_frame because the managed side wants to
/// drive ticks independently of presentation.
DEMEN_API int demen_render_graph_poll(demen_render_graph_t rg,
                                      int* out_should_close);

#ifdef __cplusplus
}
#endif
