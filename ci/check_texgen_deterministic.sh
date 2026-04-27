#!/usr/bin/env bash
# Invariant enforcement for Appendix H.2 ("deterministic").
# Runs the texgen twice with different PYTHONHASHSEEDs and asserts byte
# equality. This caught the `hash(material.name)` bug during Phase 0.
set -euo pipefail

tmp_a=$(mktemp -d)
tmp_b=$(mktemp -d)

PYTHONHASHSEED=0 python3 tools/placeholder_texgen/generate.py \
    --manifest tools/placeholder_texgen/materials.json \
    --out "$tmp_a" --stamp "$tmp_a/.stamp" > /dev/null

PYTHONHASHSEED=random python3 tools/placeholder_texgen/generate.py \
    --manifest tools/placeholder_texgen/materials.json \
    --out "$tmp_b" --stamp "$tmp_b/.stamp" > /dev/null

fail=0
for a in "$tmp_a"/*.generated.png; do
    name=$(basename "$a")
    b="$tmp_b/$name"
    if ! cmp -s "$a" "$b"; then
        echo "NON-DETERMINISTIC: $name differs between seed=0 and seed=random"
        fail=1
    fi
done

rm -rf "$tmp_a" "$tmp_b"
if [[ $fail -eq 0 ]]; then
    echo "Appendix H.2 determinism check PASSED."
fi
exit $fail
