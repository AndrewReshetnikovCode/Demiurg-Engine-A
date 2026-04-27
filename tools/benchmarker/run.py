#!/usr/bin/env python3
"""
Benchmarker orchestrator (Appendix E §E.7).

Runs the C++ harness binary across the suites registered in main.cpp, then
shells `dotnet test` for B-ALLOC. Aggregates everything into a single
gate decision record at build/benchmarker/decisions/<phase>-<git-sha>.json
per Appendix E §E.5.

Layer 1 cut: only the suites whose acceptance tests exist as binaries
(B-STREAM, B-MESH, B-TEXGEN) are wired here. Add suites to the SUITES
list below as their acceptance binaries land.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time
from typing import Any


SUITES = ["B-STREAM", "B-MESH", "B-TEXGEN"]


def find_harness(build_dir: pathlib.Path) -> pathlib.Path:
    candidates = [
        build_dir / "tools" / "benchmarker" / "Release" / "demen_benchmarker.exe",
        build_dir / "tools" / "benchmarker" / "demen_benchmarker.exe",
        build_dir / "tools" / "benchmarker" / "demen_benchmarker",
    ]
    for c in candidates:
        if c.exists():
            return c
    raise SystemExit(f"demen_benchmarker not found under {build_dir}")


def find_test_bin_dir(build_dir: pathlib.Path) -> pathlib.Path:
    for c in [build_dir / "tests" / "native" / "Release",
              build_dir / "tests" / "native"]:
        if c.exists():
            return c
    raise SystemExit(f"native tests not built under {build_dir}")


def run_suite(harness: pathlib.Path, suite: str, out_dir: pathlib.Path,
              env: dict[str, str]) -> dict[str, Any]:
    out_path = out_dir / f"{suite.lower()}.json"
    proc = subprocess.run(
        [str(harness), "--suite", suite, "--out", str(out_path)],
        env=env, capture_output=True, text=True,
    )
    record = {"suite": suite, "verdict": "FAIL", "stderr": proc.stderr.strip()}
    if out_path.exists():
        try:
            record.update(json.loads(out_path.read_text()))
        except json.JSONDecodeError:
            pass
    return record


def main() -> int:
    p = argparse.ArgumentParser(description="Benchmarker orchestrator (Appendix E)")
    p.add_argument("--build-dir", default="build", type=pathlib.Path)
    p.add_argument("--phase", default=os.environ.get("DEMEN_PHASE", "layer1"))
    p.add_argument("--commit", default=os.environ.get("DEMEN_COMMIT", "unknown"))
    args = p.parse_args()

    if not args.build_dir.exists():
        sys.exit(f"--build-dir {args.build_dir} does not exist; build first")

    harness   = find_harness(args.build_dir)
    test_dir  = find_test_bin_dir(args.build_dir)
    decisions = args.build_dir / "benchmarker" / "decisions"
    decisions.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["DEMEN_TEST_BIN_DIR"] = str(test_dir)

    suites_out: list[dict[str, Any]] = []
    for s in SUITES:
        rec = run_suite(harness, s, decisions, env)
        suites_out.append(rec)
        print(f"  {s:<10} {rec.get('verdict', 'FAIL')}")

    overall = "PASS" if all(s.get("verdict") == "PASS" for s in suites_out) else "FAIL"
    record = {
        "phase":    args.phase,
        "commit":   args.commit,
        "when_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "suites":   suites_out,
        "overall":  overall,
    }

    out = decisions / f"{args.phase}-{args.commit}.json"
    out.write_text(json.dumps(record, indent=2))
    print(f"\nDecision record: {out}  ({overall})")
    return 0 if overall == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
