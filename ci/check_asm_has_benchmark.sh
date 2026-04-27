#!/usr/bin/env bash
# Invariant #3: no .asm without a ≥15% benchmark. Phase 0 stub enforces the
# sibling-file rule; the ≥15% rule lands when the benchmarker harness does.
set -euo pipefail
changed=$(git diff --name-only origin/main...HEAD 2>/dev/null || true)
fail=0
while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    [[ "$f" != *.asm ]] && continue
    base="${f%.asm}"
    if [[ ! -f "${base}.bench.cpp" ]]; then
        echo "INVARIANT #3 VIOLATION: $f has no sibling ${base}.bench.cpp"
        fail=1
    fi
done <<< "$changed"
exit "$fail"
