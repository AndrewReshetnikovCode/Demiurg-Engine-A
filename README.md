# DemEn — Voxel Simulation Engine

**Status:** Pre-Phase-0 scaffolding
**Platform:** Windows (x64)
**See:** [DESIGN.md](./DESIGN.md) for the full design doc & AI agent pipeline spec.

---

## Quick orientation

DemEn is a voxel engine differentiated from Minecraft by first-class
simulation of wind, water, waves, and weather. It is built as a **layered
simulation** — each layer is shippable alone and grounds the next.
This repository implements **Layer 1** only (physics substrate + 5 passive toys).

## Repo layout

| Path | Contents |
|---|---|
| `src/native/` | C++20 core: voxel store, meshing, fluid solver, renderer, texture composition |
| `src/native/include/` | Public C++ headers (interface spec — §3.2, invariant #4) |
| `src/managed/` | C# orchestration layer (.NET 8): game loop, toys, UI, save/load |
| `shaders/src/` | GLSL/HLSL source; precompiled to SPIR-V at build time into `shaders/spirv/` |
| `assets/` | Block definitions (JSON), texture backgrounds/overlays/filters |
| `tools/placeholder_texgen/` | Build-time placeholder texture generator (Appendix H) |
| `tools/benchmarker/` | Benchmarker agent harness (Appendix E) |
| `tests/native/` | C++ unit + perf tests |
| `tests/managed/` | C# unit tests |
| `tests/determinism/` | Recorded-input replay hash tests (invariant #2) |
| `ci/` | GitHub Actions / build pipelines; invariant enforcement |
| `docs/appendices/` | Appendices A–H from DESIGN.md |
| `docs/agent_pipeline/` | Planner / Specialist / Benchmarker contracts |

## Current phase

**Phase 0 — Scaffolding** (not started). See DESIGN.md §3.3.

Gate criterion: empty Vulkan window opens; CI green on Windows runner;
placeholder textures generated at build time.

## Cross-cutting invariants (CI-enforced)

See DESIGN.md §3.4. Summary:

1. No managed allocations in the game loop.
2. Deterministic sim (fixed-rate tick, seeded RNG, replay-hash stable).
3. No ASM without a benchmark proving ≥15 % win over SIMD-C++.
4. Public header changes require Planner sign-off.
5. Cold-launch-to-playable ≤10 s on clean Windows Sandbox.
6. Layer 2 readiness APIs (wind/water/rain/temp point queries) must not regress.
7. Region files carry a version byte from Phase 1 onward.

## Building

**Windows (shipping target):**
```powershell
.\scripts\build.ps1 -Config Release -RunTests
```

**Linux / WSL (dev iteration on non-renderer subsystems):**
```bash
./scripts/build.sh --test
```

Prerequisites:
- Vulkan SDK ≥ 1.3 (with `glslangValidator` on PATH)
- .NET 8 SDK
- CMake ≥ 3.26
- Python ≥ 3.10 (stdlib only — no pip needed)

## Documentation map

- [DESIGN.md](./DESIGN.md) — the single source of truth.
- [docs/appendices/A_region_file_format.md](./docs/appendices/A_region_file_format.md)
- [docs/appendices/E_benchmark_harness.md](./docs/appendices/E_benchmark_harness.md)
- [docs/appendices/H_placeholder_texgen.md](./docs/appendices/H_placeholder_texgen.md)
- [docs/agent_pipeline/](./docs/agent_pipeline/) — Planner, Specialist, Benchmarker role specs.
