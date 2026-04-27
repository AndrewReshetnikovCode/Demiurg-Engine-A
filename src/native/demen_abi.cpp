// =============================================================================
// demen_abi.cpp — C ABI surface for P/Invoke from the C# orchestration layer.
// =============================================================================
// Rules (DESIGN.md §2.1.2, invariant #4):
//   - Only blittable types cross this boundary.
//   - Every function is `extern "C"`, returns a primitive or an opaque handle.
//   - Ownership rules documented per-function in Appendix D.
//   - Editing this file (or demen/abi.hpp, demen/renderer.hpp, ...) requires
//     Planner sign-off.
// =============================================================================

#include "demen/abi.hpp"
#include "demen/renderer.hpp"

extern "C" {

DEMEN_API uint32_t demen_abi_version(void) {
    return DEMEN_ABI_VERSION;
}

// demen_run_phase0_window is defined in src/native/renderer/vk_window.cpp.
// Its declaration lives in demen/renderer.hpp (included above) so the linker
// sees the symbol when building demen_core.

} // extern "C"
