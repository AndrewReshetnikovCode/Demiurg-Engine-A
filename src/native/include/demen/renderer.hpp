// =============================================================================
// demen/renderer.hpp — public API for the renderer subsystem.
// Planner-owned (invariant #4).
// =============================================================================
//
// Phase 0 surface: one blocking call that opens a window, runs the render
// loop until closed, and returns. No window-handle lifecycle, no per-frame
// submission, no multi-window — all of that lands in Phase 3 when the real
// render graph arrives.
//
// The simplest API that clears the Phase 0 gate is a single function
// (§2.11 rung 1 — eliminate the work of maintaining window state in C#).
// =============================================================================
#pragma once

#include "demen/abi.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Result codes for demen_run_phase0_window. Non-zero is failure; the managed
// entry point logs which code it got and exits with that as its process code.
#define DEMEN_WINDOW_OK                  0
#define DEMEN_WINDOW_ERR_GLFW_INIT       1
#define DEMEN_WINDOW_ERR_NO_VULKAN       2
#define DEMEN_WINDOW_ERR_NO_DEVICE       3
#define DEMEN_WINDOW_ERR_NO_SURFACE      4
#define DEMEN_WINDOW_ERR_SWAPCHAIN       5
#define DEMEN_WINDOW_ERR_PIPELINE        6
#define DEMEN_WINDOW_ERR_SHADER_LOAD     7
#define DEMEN_WINDOW_ERR_RUNTIME         8

/// Open a Vulkan-backed window, run the render loop until the user closes it
/// (or presses ESC), then tear everything down and return.
///
/// Phase 0 gate criterion: this function opens a window that draws the probe
/// shader pair and does not stutter on first frame.
///
/// Ownership: none — fully self-contained. All resources are freed before
/// return. Thread-affinity: must be called on the thread that owns the
/// process main loop (GLFW limitation on macOS; harmless elsewhere).
DEMEN_API int demen_run_phase0_window(uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
