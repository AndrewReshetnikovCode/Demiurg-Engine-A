#!/usr/bin/env python3
"""
Placeholder texture generator — Appendix H (DESIGN.md §2.10).

Emits one PNG per material defined in materials.json:
    flat RGB base colour + low-amplitude deterministic value noise, optionally
    shaped by a "grain" parameter (fine/coarse/smooth/streak/medium).

Output files are named "<material>.generated.png" so .gitignore can exclude
them without excluding hand-authored textures that might share the folder.

Dependencies: Python 3.10+ standard library only. No Pillow, no NumPy —
the build machines we target should not need pip packages for Phase 0.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import random
import struct
import sys
import zlib
from typing import Iterable


def _stable_name_hash(name: str) -> int:
    """Deterministic 64-bit hash of a material name.

    Python's built-in ``hash()`` is randomised per process (PYTHONHASHSEED),
    which would violate Appendix H.2's determinism constraint. We use
    BLAKE2b-64 instead so the seed is fully reproducible across runs,
    platforms, and Python versions.
    """
    digest = hashlib.blake2b(name.encode("utf-8"), digest_size=8).digest()
    return int.from_bytes(digest, "big", signed=False)


# -----------------------------------------------------------------------------
# Minimal PNG writer (RGB8, no palette, no filter)
# -----------------------------------------------------------------------------

def _chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_png_rgb(path: pathlib.Path, width: int, height: int, pixels: bytes) -> None:
    """Write an RGB8 PNG. `pixels` must be width*height*3 bytes."""
    assert len(pixels) == width * height * 3, "pixel buffer size mismatch"
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # RGB, no filter
    # Prepend a filter byte (0 = None) per scanline.
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * stride : (y + 1) * stride])
    idat = zlib.compress(bytes(raw), level=9)
    path.write_bytes(sig + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", idat) + _chunk(b"IEND", b""))


# -----------------------------------------------------------------------------
# Deterministic value-noise texture synthesis
# -----------------------------------------------------------------------------

def _clamp8(v: float) -> int:
    if v < 0.0:
        return 0
    if v > 255.0:
        return 255
    return int(v)


def _value_noise(rng: random.Random, size: int, cells: int) -> list[float]:
    """Bilinear value noise sampled at `size` from a `cells`x`cells` lattice."""
    lattice = [[rng.random() for _ in range(cells + 1)] for _ in range(cells + 1)]
    out = [0.0] * (size * size)
    scale = cells / size
    for y in range(size):
        fy = y * scale
        iy = int(fy)
        ty = fy - iy
        for x in range(size):
            fx = x * scale
            ix = int(fx)
            tx = fx - ix
            a = lattice[iy][ix]
            b = lattice[iy][ix + 1]
            c = lattice[iy + 1][ix]
            d = lattice[iy + 1][ix + 1]
            ab = a + (b - a) * tx
            cd = c + (d - c) * tx
            out[y * size + x] = ab + (cd - ab) * ty
    return out


GRAIN_CELLS = {
    "fine":    24,
    "medium":  14,
    "coarse":   8,
    "smooth":  32,   # smoother: larger low-freq cells + more blur
    "streak":  12,   # slightly directional, handled separately
}


def synthesize(material: dict, size: int, seed: int) -> bytes:
    """Return width*height*3 bytes for one material's PNG payload."""
    rng = random.Random(seed ^ _stable_name_hash(material["name"]))
    r0, g0, b0 = material["rgb"]
    amp = float(material["noise"]) * 255.0
    cells = GRAIN_CELLS.get(material["grain"], 14)

    base = _value_noise(rng, size, cells)

    # Streak grain: add anisotropic ripples along one axis to suggest wood.
    if material["grain"] == "streak":
        streaks = _value_noise(rng, size, 4)
        base = [b + 0.4 * s for b, s in zip(base, streaks)]

    pixels = bytearray(size * size * 3)
    for i, n in enumerate(base):
        # Re-centre noise to [-1, 1], scale by amp.
        d = (n - 0.5) * 2.0 * amp
        pixels[i * 3 + 0] = _clamp8(r0 + d)
        pixels[i * 3 + 1] = _clamp8(g0 + d)
        pixels[i * 3 + 2] = _clamp8(b0 + d)
    return bytes(pixels)


# -----------------------------------------------------------------------------
# .texraw — minimal engine-consumable texture format (Appendix H §H.4.1).
# -----------------------------------------------------------------------------
# Layout (little-endian):
#   offset  size  field
#     0     4     magic "DETX"
#     4     4     width   (u32)
#     8     4     height  (u32)
#    12     1     channels (3 for RGB, 4 for RGBA; Phase 2 writes 3)
#    13     3     reserved (must be 0)
#    16     w*h*c raw pixel bytes
#
# Chosen so the C++ loader is a 20-line memcpy. PNG decoding in C++ is
# ~500 LOC that §2.11 would reject on sight — we already ship Python
# that can write whatever format we want.
# -----------------------------------------------------------------------------

def write_texraw_rgb(path: pathlib.Path, width: int, height: int, pixels: bytes) -> None:
    """Write a .texraw file with 3-channel (RGB) pixel data."""
    assert len(pixels) == width * height * 3
    header = (
        b"DETX"
        + struct.pack("<III", width, height, 0)  # 3 u32
    )
    # Rewrite the 3rd u32 as channels=3 + 3 reserved bytes.
    header = header[:12] + bytes([3, 0, 0, 0])
    assert len(header) == 16
    path.write_bytes(header + pixels)


# -----------------------------------------------------------------------------
# CLI
# -----------------------------------------------------------------------------

def main(argv: Iterable[str]) -> int:
    ap = argparse.ArgumentParser(description="Appendix H placeholder texture generator.")
    ap.add_argument("--manifest", required=True, type=pathlib.Path)
    ap.add_argument("--out",      required=True, type=pathlib.Path)
    ap.add_argument("--stamp",    required=True, type=pathlib.Path)
    args = ap.parse_args(list(argv))

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    size = int(manifest["size"])
    seed = int(manifest["seed"])
    args.out.mkdir(parents=True, exist_ok=True)

    written = []
    for mat in manifest["materials"]:
        png_path = args.out / f"{mat['name']}.generated.png"
        raw_path = args.out / f"{mat['name']}.texraw"
        pixels = synthesize(mat, size, seed)
        write_png_rgb(png_path, size, size, pixels)
        write_texraw_rgb(raw_path, size, size, pixels)
        written.append(png_path.name)
        written.append(raw_path.name)

    args.stamp.write_text(
        "materials: " + ", ".join(sorted(written)) + "\n",
        encoding="utf-8",
    )
    print(f"[placeholder_texgen] wrote {len(written)} textures to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
