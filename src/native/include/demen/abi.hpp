// =============================================================================
// demen/abi.hpp — P/Invoke surface.
// Planner-owned. See DESIGN.md §2.1.2 and invariant #4.
// =============================================================================
#pragma once

#include <cstdint>

#if defined(_WIN32)
    #if defined(DEMEN_CORE_EXPORTS)
        #define DEMEN_API __declspec(dllexport)
    #else
        #define DEMEN_API __declspec(dllimport)
    #endif
#else
    #define DEMEN_API __attribute__((visibility("default")))
#endif

// Bumped whenever the ABI surface changes in a non-backward-compatible way.
// Managed side reads this on startup and refuses to load mismatched DLLs,
// per invariant #7 (clean refusal, not crash).
//   0x01 — Phase 0 (abi + renderer probe).
//   0x02 — Phase 2 (voxel_store + meshing + texture_composition).
//   0x03 — Phase 3 (render_graph + atlas upload + mesh upload).
//   0x04 — Phase 4 (spatial + entities ABI).
//   0x05 — Phase 5 (fluid atmosphere + wind/temperature queries).
//   0x06 — Phase 6 (water + waves).
//   0x07 — Phase 7 (weather + rainfall query).
#define DEMEN_ABI_VERSION 0x00000007u

#ifdef __cplusplus
extern "C" {
#endif

DEMEN_API uint32_t demen_abi_version(void);

#ifdef __cplusplus
}
#endif
