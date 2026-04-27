# Appendix E — Benchmark harness & Benchmarker agent spec

**Status:** v1.0 (finalised at Phase 0 gate; amendments require Planner sign-off).
**Consumers:** Benchmarker agent; every phase's gate criterion.
**Related:** DESIGN.md §1.2, §3.1, §3.3, §3.4.

---

## E.1 Role summary

The **Benchmarker** is one of three agent roles (Planner / Specialist /
Benchmarker). Its only powers are:

1. Run the benchmark suites on a reference build.
2. Produce a machine-readable **gate decision record** (§E.5).
3. PASS or FAIL a phase gate.

It has **no write access to code.** It cannot add tasks, edit the design doc,
or adjust targets. If a target is unreachable, the affected Specialist files a
"budget escalation" to the Planner (§3.6) — the Benchmarker just reports.

## E.2 Reference hardware

Single machine type for gate decisions. All gate-relevant numbers must be
reproduced on this exact spec; nothing else is authoritative.

| Component | Spec |
|---|---|
| CPU | 6-core Zen 3 (Ryzen 5 5600) or Alder Lake equivalent |
| GPU | NVIDIA RTX 3060 12 GB (primary) or RX 6600 (secondary) |
| RAM | 32 GB DDR4-3200 dual-channel |
| Storage | NVMe SSD (≥ 3 GB/s sequential read) |
| OS | Windows 11 23H2, clean profile |
| Driver | Latest WHQL at time of run, pinned in the decision record |

## E.3 Suites

Each suite is a separate binary or dotnet test target. Suites run in isolation
(fresh process per suite) to eliminate cross-contamination.

| Id | Name | Owner subsystem | Added in phase |
|---|---|---|---|
| `B-STREAM`  | Chunk stream round-trip (10 k chunks, disk) | voxel_store | 1 |
| `B-MESH`    | Greedy mesher throughput, 12-chunk radius | meshing | 2 |
| `B-TEXGEN`  | Material recomposition latency | texture_composition | 2 |
| `B-FPS`     | Sustained FPS + 1 %-low, static scene | renderer | 3 |
| `B-COLLIDE` | 10 k-voxel walk, no-tunnel assertion | spatial | 4 |
| `B-AIR`     | Near-zone air tick, ≤ 5 ms / step | fluid | 5 |
| `B-DETERM`  | 60 s sim replay hash stability | fluid + voxel_store | 5 |
| `B-WATER`   | Water + wave tick, 60 FPS hold | fluid | 6 |
| `B-WEATHER` | Full weather cycle, no FPS regression | fluid | 7 |
| `B-COLD`    | Cold-launch-to-playable, Windows Sandbox | installer | 8 |
| `B-ALLOC`   | Zero managed allocations in game loop | DemEn.Game | 4 (invariant #1) |

## E.4 Target table

Pulled verbatim from DESIGN.md §1.2 and phase gates (§3.3); the Benchmarker
must not reinterpret them.

| Suite | Metric | Target |
|---|---|---|
| B-STREAM  | round-trip time  | < 2 s |
| B-MESH    | cold meshing     | < 500 ms for 12-chunk radius |
| B-MESH    | per-dirty chunk  | < 16 ms |
| B-TEXGEN  | hot-reload       | < 50 ms |
| B-FPS     | median FPS       | ≥ 60 |
| B-FPS     | 1 %-low FPS      | ≥ 45 |
| B-AIR     | near-zone tick   | ≤ 5 ms |
| B-DETERM  | hash stability   | identical across 3 consecutive runs |
| B-COLD    | launch-to-play   | ≤ 10 s |
| B-ALLOC   | gen-0 GC count   | 0 over 10 s gameplay sample |

## E.5 Gate decision record

Emitted as JSON at `build/benchmarker/decisions/<phase>-<git-sha>.json`:

```json
{
  "phase": 3,
  "commit": "abc1234",
  "when_utc": "2026-05-12T18:04:00Z",
  "hardware": { "cpu": "...", "gpu": "...", "driver": "..." },
  "suites": [
    {
      "id": "B-FPS",
      "metrics": { "median_fps": 71.2, "one_percent_low_fps": 52.4 },
      "targets": { "median_fps": 60, "one_percent_low_fps": 45 },
      "verdict": "PASS"
    }
  ],
  "overall": "PASS"
}
```

**PASS** requires every suite in the phase's gate criterion to PASS. A single
FAIL blocks phase advance; no partial gates.

## E.6 Statistical hygiene

- Warm-up: 5 seconds discarded before measurement begins.
- Duration: each suite runs for ≥ 30 seconds of measurement unless spec says
  otherwise. FPS suites report both median and 1 %-low.
- Repeats: 3 runs; the reported number is the **worst** run's value (not the
  median). This is deliberately pessimistic — we'd rather over-gate than
  ship a flaky regression.
- Power state: Windows "Ultimate Performance" plan pinned; no background
  updates; network disabled during B-FPS, B-AIR, B-COLD.

## E.7 Resolutions (closed at Phase 0 gate)

- **Per-frame traces for failing runs:** YES. Capped at 10 MB per failing
  suite, stored under `build/benchmarker/traces/<phase>-<git-sha>/`.
  Deleted on the next PASS for the same phase to bound disk.
- **Harness language:** C++ for native suites (statically linked against
  `demen_core` so the measurement path mirrors the shipping build);
  `dotnet test` for B-ALLOC since it is a managed-GC measurement;
  orchestrator is a Python script at `tools/benchmarker/run.py` that shells
  to both and aggregates into the JSON decision record (§E.5).
- **Publishing decision records:** deferred to Phase 8. Until then the
  records live in-repo under `build/benchmarker/decisions/` and are
  committed by the Benchmarker's service account on every PASS.
- **Reference-hardware drift:** if the reference machine is replaced, the
  Planner re-baselines every prior PASS record on the new hardware before
  allowing new phase work. This is a Planner-level decision (§3.1).


## E.8 Optimization philosophy in benchmarking (§2.11, invariant #8)

The Benchmarker reports **whole-system** metrics only. Per-function
micro-benchmarks exist for Specialists to iterate locally, but they are not
the gate. A Specialist cannot clear a gate by producing a kernel benchmark
showing their hand-tuned loop is 2 % faster; the gate asks whether the
frame rate, memory budget, and tick time are met by the full build.

This is deliberate: it keeps the incentive on structural wins, not on
micro-optimization at non-bottlenecks. If the whole-system number is green,
the simpler variant wins every time (§2.11 corollary on parameter choice).

When a Specialist files a budget escalation (§3.6), the Benchmarker's role
is only to run the numbers the Planner asks for. Adjudication is the
Planner's; the Benchmarker does not take a position on whether complexity
is "worth it."

