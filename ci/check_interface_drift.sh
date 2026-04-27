#!/usr/bin/env bash
# Invariant #4: any change under src/native/include/ requires a PR label
# "planner-sign-off". This is the mechanical gate; the Planner agent applies
# the label after running its dependency analysis.
set -euo pipefail
changed=$(git diff --name-only origin/main...HEAD 2>/dev/null || true)
if ! grep -q '^src/native/include/' <<< "$changed"; then
    exit 0
fi
if echo "${LABELS:-}" | grep -q 'planner-sign-off'; then
    exit 0
fi
echo "INVARIANT #4 VIOLATION: public header changed without planner-sign-off label."
echo "Changed headers:"
grep '^src/native/include/' <<< "$changed" | sed 's/^/  /'
exit 1
