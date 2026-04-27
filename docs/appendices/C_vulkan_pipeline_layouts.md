# Appendix C — Vulkan pipeline layouts

**Status:** v1.0 (Phase 3 — landed with render_graph.cpp).
**Consumers:** renderer subsystem; shader authors.
**Related:** DESIGN.md §2.6.

---

## C.1 Purpose

Fixes the Vulkan descriptor sets, push constants, and vertex input layout
the Phase 3 opaque pass ships with. Phase 6 transparent and Phase 8 post-
process passes layer on top; Phase 3 is the baseline.

## C.2 Device features required

Enabled in `vk_instance.cpp` via `VkPhysicalDeviceFeatures2`:

| Feature | Use |
|---|---|
| `dynamicRendering` (1.3 core) | No VkRenderPass objects; direct begin/end rendering. |
| `descriptorIndexing.shaderSampledImageArrayNonUniformIndexing` | Bindless atlas indexed by per-vertex slot. |
| `descriptorIndexing.runtimeDescriptorArray` | Variable-size atlas table. |
| `descriptorIndexing.descriptorBindingPartiallyBound` | Dead atlas slots do not break the set. |
| `descriptorIndexing.descriptorBindingVariableDescriptorCount` | Set allocated with `kMaxAtlasSlots=1024`. |

## C.3 Descriptor set 0 — bindless atlas + instance SSBO

```
layout(set = 0, binding = 0)  // bindless texture array (fragment)
    sampler2D u_atlas[1024];  // PARTIALLY_BOUND | VARIABLE_DESCRIPTOR_COUNT

layout(set = 0, binding = 1)  // per-instance transforms (vertex)
    readonly buffer Instances { mat4 xform[]; } u_instances;
```

Phase 3 writes binding 0 once per atlas upload and leaves binding 1
unwritten — instance transforms ride via push constants until Phase 4
when instance count climbs past a handful.

## C.4 Push constants

```
layout(push_constant) uniform PC {
    mat4  mvp;               // offset 0,  16 floats
    float time_seconds;      // offset 64
    float atlas_override;    // offset 68 — UINT32_MAX (sentinel) or slot id
    float _pad0, _pad1;      // to 80 bytes (128-byte minimum on all vendors)
} pc;
```

Total 80 bytes. Well inside the 128-byte guaranteed minimum of `maxPushConstantsSize`
so no vendor (NVIDIA 256 / AMD 128 / Intel 128) refuses the layout.

## C.5 Vertex input — `demen_mesh_vertex`

Layout matches the 32-byte blittable struct in `demen/meshing.hpp`:

| Location | Format | Offset | Field |
|:---:|---|:---:|---|
| 0 | `R16G16B16_SINT` | 0  | `pos[3]` (voxel-local) |
| 1 | `R8G8_UINT`      | 6  | `normal_face`, `ao` |
| 2 | `R16G16_SINT`    | 8  | `uv[0..1]` (fixed-point) |
| 3 | `R32_UINT`       | 12 | `atlas_slot` |
| 4 | `R32_UINT`       | 16 | `_pad` (reserved) |

Stride is fixed at 32 bytes; binding 0, `VK_VERTEX_INPUT_RATE_VERTEX`.

Changing any field here requires bumping `DEMEN_ABI_VERSION` (§2.1.2) and
is Planner-sign-off (invariant #4).

## C.6 Dynamic state

Only viewport and scissor are dynamic. Every other state is baked into the
pipeline, so the render loop never rebuilds a pipeline at runtime. This
feeds invariant #5 (out-of-the-box: no first-frame pipeline-compile
stutter) — shaders are SPIR-V on disk, pipelines are created once at
startup, pipeline cache is primed on first launch (Phase 8 §2.6).

## C.7 Color + depth attachments

- Color: swapchain format (SRGB B8G8R8A8 preferred); load OP CLEAR to sky
  blue, store OP STORE.
- Depth: `VK_FORMAT_D32_SFLOAT`, transient (STORE_DONT_CARE). Depth test
  `LESS`, depth write enabled. Inverted-Z is a Phase 8 optimization if
  B-FPS shows depth precision issues on the 2048 m far plane; not worth
  the complexity yet (§2.11 rung 1).

## C.8 Transparent pass (Phase 6 — not yet wired)

Expected shape when it lands:

- Same set 0 layout.
- Pipeline: alpha blending `VK_BLEND_OP_ADD` with `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA`,
  depth test `LESS_OR_EQUAL`, depth write **disabled**.
- Draw order: back-to-front sort emitted by the fluid-surface mesher.

This appendix grows with §C.9 at that time.
