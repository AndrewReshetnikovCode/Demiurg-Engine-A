# Specialist agent — contract template

**Scope (write):** exactly one folder under `src/native/<subsystem>/`.
**Scope (read):** all of `src/native/include/`, the design doc, other
subsystems' sources, their own tests folder under `tests/native/`.

> **Sandbox mode.** Wherever this contract says "PR" or "pull request",
> read it as "local commit on the Specialist's branch." The sandbox never
> contacts a remote; Specialists must not run `git push`, open PRs, or
> author push/PR scripts. See `docs/agent_pipeline/README.md` — Sandbox
> mode. The scope-lockout, invariant, and optimization-discipline rules
> below apply unchanged; they just operate at commit granularity instead
> of PR granularity.

## Inputs (provided by Planner per dispatch)

- `subsystem`: the one folder you can edit.
- `interface_header`: the header under `src/native/include/demen/` you
  must implement against. Any change to it requires coming back to the Planner.
- `acceptance_tests`: list of test names in `tests/native/` that must pass.
- `benchmark_targets`: the suite IDs from Appendix E you must clear.
- `scope_lockout`: enforced by CI. Touching anything outside `subsystem`
  causes mechanical PR rejection.

## Deliverables

- Implementation (`.cpp` files under `subsystem/`).
- Unit tests (`.cpp` files under `tests/native/`).
- Benchmark-harness entries if applicable.
- A "proposed deltas" note if the interface needs to change.

## Optimization discipline (DESIGN.md §2.11, invariant #8)

**Default to the simplest implementation that clears the gate.** The order
in which you should attempt optimization, when a gate is not yet cleared:

1. Eliminate the work (cache, precompute, skip).
2. Batch it (N calls → 1 call; N allocations → 1 arena).
3. Lay data out for the cache (SoA, contiguous, chunk-sized).
4. Shrink the working set (palettes, smaller structs, dedup).
5. Move to the GPU if it fits the pipeline.
6. Parallelise across cores.
7. SIMD-vectorise the inner kernel (AVX2 baseline).
8. Hand-written ASM (invariant #3 — requires ≥15 % benchmark win).

**Do not submit PRs whose main justification is "this is ~2 % faster."** A
whole-system benchmark that the gate already passes is not a reason to add
complexity. If a PR adds non-obvious optimization complexity (custom
allocators, hand-rolled intrinsics, lock-free structures, cache-line
alignment hacks, loop-unroll attributes, inline asm), invariant #8 requires
the PR description to include:

- (a) whole-system benchmark number before and after,
- (b) the simpler variant being replaced,
- (c) the §3.3 gate criterion that *requires* the complexity.

All three or the PR is rejected. When in doubt: **ship the simpler variant,
document the headroom, and move on.**

## Failure protocol (§3.6)

After 3 failed attempts on the same test the specialist is killed and the
task returns to the Planner with the failure trace.

## Budget escalation (§3.6)

If a benchmark target is provably unreachable with the simplest approach,
file a budget escalation to the Planner *before* reaching for invariant-#8
complexity. The escalation must argue either:

- The target is wrong (cite numbers), or
- A *specific* simpler-to-complex step from the list above is justified
  (cite which one, show the benchmark).

Planner decides; a >20 % budget relaxation needs human review.
