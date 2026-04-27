// =============================================================================
// demen/spatial.hpp — collision + raycast against the voxel grid (§2.7).
// Planner-owned. Thin ABI; the heavy work is C++-side stateless math.
// =============================================================================
#pragma once

#include "demen/abi.hpp"
#include "demen/voxel_store.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#define DEMEN_SP_OK                 0
#define DEMEN_SP_ERR_INVALID_HANDLE 1
#define DEMEN_SP_ERR_OUT_OF_BOUNDS  2

typedef struct demen_aabb {
    float min[3];
    float max[3];
} demen_aabb;

typedef struct demen_sweep_hit {
    int32_t hit;             // 1 = collided, 0 = free path
    float   time;            // [0..1] along the sweep; 1.0 if no hit
    float   normal[3];       // unit normal of the blocker face (voxel-aligned)
    int32_t voxel[3];        // blocker voxel coordinates (world-voxel)
} demen_sweep_hit;

typedef struct demen_ray_hit {
    int32_t hit;
    float   distance;        // metres along the ray
    int32_t voxel[3];
    float   normal[3];
} demen_ray_hit;

/// Swept-AABB against the voxel grid. `velocity` is metres; AABB positions
/// are metres in world space (voxel size is 2 m per §2.3). Returns the
/// earliest collision along [0, 1]. Axis-separated response is the caller's
/// job (apply `t`, zero the velocity component along the hit normal, re-sweep).
///
/// Implementation: expand the AABB by its own extents and walk the voxel
/// grid with a slab test along the velocity vector. O(path_length_in_voxels).
DEMEN_API int demen_sweep_aabb(demen_world_t world,
                               const demen_aabb* box_world,
                               const float velocity[3],
                               demen_sweep_hit* out_hit);

/// Ray vs voxel grid. Woo-Amanatides 3D-DDA. `ray_dir` need not be normalised;
/// `max_distance` is in metres.
DEMEN_API int demen_raycast_voxel(demen_world_t world,
                                  const float ray_origin[3],
                                  const float ray_dir[3],
                                  float max_distance,
                                  demen_ray_hit* out_hit);

/// Frustum cull a chunk AABB. `vp` is a 4x4 column-major view-projection
/// matrix. Returns 1 if visible, 0 if culled. Used by the renderer to skip
/// mesh uploads for out-of-sight chunks (Phase 3 + Phase 4).
DEMEN_API int demen_frustum_cull_chunk(const float vp[16],
                                       int32_t cx, int32_t cy, int32_t cz);

#ifdef __cplusplus
}
#endif
