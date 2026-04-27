#!/usr/bin/env bash
# =============================================================================
# scripts/package.sh — Linux/WSL counterpart of package.ps1. Assembles the
# same payload into ./out/DemEn so the operator can tarball it and copy it
# to the Windows machine for the first benchmark run. Runs builds only if
# the artefacts aren't already present — the sandbox can't compile, so this
# script is expected to error cleanly when invoked there.
# =============================================================================
set -euo pipefail

OUT="${1:-out/DemEn}"
CFG="${CFG:-Release}"

if [ ! -f "build/lib${CFG,,}/libdemen.so" ] && [ ! -f "build/${CFG}/libdemen.so" ]; then
    echo "[package] building native core"
    cmake --preset default 2>/dev/null || cmake -S . -B build -DCMAKE_BUILD_TYPE="$CFG"
    cmake --build build --config "$CFG" -j
fi

mkdir -p "$OUT"
cp -v build/*demen* "$OUT/" 2>/dev/null || true
cp -rv shaders/spirv "$OUT/shaders" 2>/dev/null || true
cp -rv assets        "$OUT/assets"
echo "Payload staged at $OUT"
