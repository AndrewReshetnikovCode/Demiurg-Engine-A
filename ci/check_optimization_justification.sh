#!/usr/bin/env bash
# Invariant #8 — optimization complexity justification.
#
# Mechanical rule: if a PR touches any file matching the "complexity markers"
# list below, the PR description must contain the three required sections
# (a/b/c from invariant #8). The Planner still reviews the content; CI only
# checks that the sections are present so a Specialist cannot sneak in a
# 2 %-at-a-non-bottleneck PR with no justification at all.
#
# Trigger paths / markers:
#   - any new .asm file                      (also covered by invariant #3)
#   - files containing __m256/__m512/_mm_    (SIMD intrinsics added)
#   - files with #pragma unroll / pragma GCC unroll
#   - files defining a custom operator new / placement-new arena
#   - files using alignas(64) for cache-line hacks
set -euo pipefail

diff_range="${DIFF_RANGE:-origin/main...HEAD}"
changed=$(git diff --name-only "$diff_range" 2>/dev/null || true)
touched_complex=0
reasons=()

while IFS= read -r f; do
    [[ -z "$f" || ! -f "$f" ]] && continue
    if [[ "$f" == *.asm ]]; then
        touched_complex=1; reasons+=("$f: inline asm"); continue
    fi
    if [[ "$f" == *.cpp || "$f" == *.hpp || "$f" == *.h ]]; then
        if grep -qE '(__m256|__m512|_mm[0-9]*_)' "$f" 2>/dev/null; then
            touched_complex=1; reasons+=("$f: SIMD intrinsics")
        fi
        if grep -qE '#pragma[[:space:]]+(GCC[[:space:]]+)?unroll' "$f" 2>/dev/null; then
            touched_complex=1; reasons+=("$f: loop-unroll pragma")
        fi
        if grep -qE 'operator[[:space:]]+new\b|placement[[:space:]]*new' "$f" 2>/dev/null; then
            touched_complex=1; reasons+=("$f: custom allocator")
        fi
        if grep -qE 'alignas\([[:space:]]*64' "$f" 2>/dev/null; then
            touched_complex=1; reasons+=("$f: cache-line alignment")
        fi
    fi
done <<< "$changed"

if [[ $touched_complex -eq 0 ]]; then
    echo "No optimization-complexity markers in this diff; invariant #8 not triggered."
    exit 0
fi

# Read PR body from env (GitHub Actions supplies it).
body="${PR_BODY:-}"
missing=()
grep -qi '(a)' <<< "$body" || missing+=("(a) before/after whole-system benchmark")
grep -qi '(b)' <<< "$body" || missing+=("(b) simpler variant being replaced")
grep -qi '(c)' <<< "$body" || missing+=("(c) §3.3 gate criterion requiring the complexity")

echo "Invariant #8 triggered by:"
printf '  - %s\n' "${reasons[@]}"
if [[ ${#missing[@]} -gt 0 ]]; then
    echo ""
    echo "PR description is missing the required sections:"
    printf '  - %s\n' "${missing[@]}"
    exit 1
fi

echo ""
echo "All three justification sections present; Planner will adjudicate content."
