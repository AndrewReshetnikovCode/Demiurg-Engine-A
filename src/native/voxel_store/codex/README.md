# Codex-native voxel store utilities

## Agent preset

See `agent_presets.md`: in this folder, Codex Agent should not run or write tests unless explicitly instructed.

## `voxel_grid_debug`

Standalone console debug utility that uses only the voxel store to:
1. Parse arguments.
2. Initialize world storage.
3. Generate random debug cell data (direction, temperature, pressure, occupancy).
4. Fill the voxel store according to mode (`air`, `liquid`, `solid`).
5. Render a selected `y` layer in console (top layer by default).

### Build target
- CMake target: `demen_voxel_grid_debug`
- Output binary name: `voxel_grid_debug`

### Usage
```bash
./voxel_grid_debug air 10 10
./voxel_grid_debug liquid 10 10 3 4
./voxel_grid_debug solid 10 10 2
```

`air 10 10` fills with gas blocks and renders top-layer wind arrows.
`liquid 10 10 3 4` renders layer `y=3`, with liquid filled up to height `4`; only liquid surface uses arrows, non-surface uses `L`.
`solid 10 10 2` renders random solid-vs-empty distribution at layer `y=2` (`#` or `.`).

## Why there is one utility (not separate liquid/solid scripts)

`voxel_grid_debug` is intentionally a single entry point that switches behavior by mode.
This keeps parsing, world setup, and rendering orchestration in one place, while mode-specific behavior is implemented in generator/fill/render modules.
