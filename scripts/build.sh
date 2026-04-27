#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# scripts/build.sh — dev-environment build orchestrator (Linux/WSL).
# Windows is the shipping target; this script exists so agents can iterate on
# non-renderer subsystems on Linux runners. The renderer phase requires Windows.
# -----------------------------------------------------------------------------
set -euo pipefail

CONFIG="${CONFIG:-Release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD"
    shift
fi

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD" -j

if [[ "${1:-}" == "--test" ]]; then
    ctest --test-dir "$BUILD" --output-on-failure
fi

echo "DemEn build ($CONFIG) complete."
