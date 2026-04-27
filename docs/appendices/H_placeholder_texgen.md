# Appendix H — Placeholder texture generator spec

**Status:** Stub v0.1 (Phase 0 — shipping).
**Script:** `tools/placeholder_texgen/generate.py`
**Manifest:** `tools/placeholder_texgen/materials.json`
**Related:** DESIGN.md §2.10.

---

## H.1 Purpose

Produce 8–10 natural-material background textures for Layer 1 *without*
committing binary art to the repo. The script is **the art pipeline** for
Layer 1; real textures land in later layers, authored against the same
bindless-atlas slot structure (§2.10).

## H.2 Constraints

- **No external dependencies.** Python 3.10+ standard library only.
  No Pillow, NumPy, OpenCV — CI must not need `pip install` for placeholders.
- **Deterministic.** Same manifest + same script version ⇒ byte-identical
  output. Seed is in the manifest, not on the command line. (Feeds invariant
  #2 by keeping `.generated.png` files reproducible.)
- **Not checkerboards.** The constraint from §2.10. Each texture must read
  as "a material" at gameplay distance, not as a debug swatch.
- **Distinguishable.** No two adjacent materials should be confusable at
  a glance. Perceptual delta between any pair's base RGB must be ≳ 20 in
  approximate CIEDE2000 (Phase 0 eyeballs it; Phase 8 gate tightens it).
- **Readable.** Under default world lighting, the texture must not clip
  to white or black at either noise extreme.

## H.3 Algorithm

```
for each material M in manifest:
    rng = Random(seed XOR hash(M.name))
    noise = bilinear_value_noise(rng, size=128, cells=grain_cells[M.grain])
    if M.grain == "streak":
        noise += 0.4 * bilinear_value_noise(rng, size=128, cells=4)
    for each pixel p:
        delta = (noise[p] - 0.5) * 2 * M.noise * 255
        rgb = clamp(M.rgb + delta)
    write RGB8 PNG at assets/textures/backgrounds/<M.name>.generated.png
```

Grain → lattice-cell-count table:

| grain   | cells |
|---------|-------|
| fine    | 24    |
| medium  | 14    |
| coarse  |  8    |
| smooth  | 32    |
| streak  | 12 (plus a 4-cell directional overlay) |

## H.4 Output format

- 128 × 128 pixels.
- RGB8, no alpha, no palette, no filter byte on scanlines (filter byte = 0).
- File name: `<material>.generated.png`. The `.generated` infix is required
  so `.gitignore` can strip them without stripping hand-authored PNGs that
  share the folder.

## H.5 Build wiring

Invoked by `tools/placeholder_texgen/CMakeLists.txt` as a custom command.
Output stamp file `assets/textures/backgrounds/.stamp` breaks the dependency
chain cleanly — touching the manifest or the script re-generates everything,
touching nothing leaves the outputs alone.

## H.6 Phase 0 distinguishability review

Human gate at end of Phase 0 (§3.5) reviews the 9 outputs side by side.
Any pair that reads as "the same material" triggers a manifest tweak, not a
code change — colours live entirely in `materials.json`.

## H.7 Open questions

- [ ] Do we emit a 64×64 mip-0 variant for low-LOD chunks, or leave that to
      the GPU mipmap chain? (Leave to GPU; Phase 2 mesher confirms.)
- [ ] Should the "grass" material get a subtle green-to-brown gradient for
      the top/side split in Phase 2? (Yes; add a second variant file there.)

## H.8 .texraw companion format (added at Phase 2)

Alongside each `<material>.generated.png` the generator writes a
`<material>.texraw` blob. The engine loads the `.texraw`; the PNG stays
for human review only.

```
offset  size  field
  0     4     magic "DETX"
  4     4     width   (u32 LE)
  8     4     height  (u32 LE)
 12     1     channels (3 = RGB, 4 = RGBA; Phase 2 always writes 3)
 13     3     reserved (must be 0)
 16     w*h*c raw pixels (row-major, no stride)
```

Rationale (§2.11, invariant #8): a correct PNG decoder in C++ is ~500 LOC
of inflate + IDAT framing + filter handling. The format above is a
20-line memcpy loader. We control both the writer (Appendix H) and the
reader (texture_composition subsystem), so PNG buys us nothing here.
Shipping-grade art pipeline in Layer 5 may reintroduce PNG loading via
a vendored library (stb_image is the obvious choice), but Phase 2 does
not need it.
