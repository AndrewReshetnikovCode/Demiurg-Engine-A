# AI Agent Pipeline — operational notes

Implementation of DESIGN.md §3.

- `planner.md` — Planner role, permissions, dispatch protocol (TBD)
- `specialist_contract.md` — template specialists instantiate from (TBD)
- `benchmarker.md` — Benchmarker harness + gate decision record (TBD)
- `phase_gates.md` — live tracking of which phase is current and gate status (TBD)

Specialists cannot modify DESIGN.md or files in this folder.
Only the Planner writes here.

---

## Sandbox mode (operator-imposed, overrides DESIGN.md §3 where it conflicts)

This repository lives inside a local sandbox. The agent pipeline operates
under the following hard constraints, which take precedence over any
"PR", "push", "GitHub", or "CI job" language elsewhere in the docs:

1. **Local commits only.** Every deliverable lands as a plain `git commit`
   on a local branch in the sandbox. Branch names still follow the
   `phase-N/pre-dispatch` / `phase-N/impl` convention from DESIGN.md §3
   so history reads the same.
2. **No `git push`.** Agents must not execute `git push`, `git remote add`,
   `git fetch <url>`, or any other operation that contacts a remote host.
   An existing `origin` remote may be present from an earlier environment;
   it must be treated as read-only / ignored.
3. **No PR-opening scripts.** Agents must not author, resurrect, or
   execute scripts that push branches or call GitHub / GitLab / Gitea
   APIs. Any `scripts/open_*_pr.sh`, `gh pr create`, or `curl`-to-API
   pattern is forbidden. The previous Planner's `open_phase*_pr.sh`
   scripts have been removed under this rule.
4. **No external credentials.** Agents must not read, prompt for, or
   reference `GH_TOKEN`, `GITHUB_TOKEN`, SSH keys, or `.netrc` entries.
5. **CI-as-description, not CI-as-action.** Where DESIGN.md §3.4 or
   Appendix E describe a CI job, the sandbox agent treats that as a
   *specification* of what a future CI would enforce — it does not try
   to run CI itself and does not emit workflow files beyond those already
   checked in under `.github/` (which are inert in sandbox mode).
6. **Human gates disabled by operator** (same rule that applied to §3.5):
   phases advance autonomously. Phase transitions are recorded by
   editing `phase_gates.md` in the same commit that lands the work.
7. **All phase gates abolished by operator for Layer 1 completion.**
   The Benchmarker-gated phase-advance rule from DESIGN.md §3.3 is
   suspended: code for phases 2–8 lands continuously without waiting
   on Windows+Vulkan benchmark records. Rationale: the sandbox cannot
   run reference hardware and the operator elected to write everything
   first, then build-and-debug in one pass rather than phase by phase.
   The benchmark harness and acceptance tests are still authored per
   DESIGN.md so gate records can be produced later once the code
   compiles on a real machine; invariants #1, #2, #3, #6, #7, #8 still
   bind every commit. What's relaxed is *scheduling*, not *correctness*.

If the operator later brings up an external remote, Sandbox mode is lifted
by removing this section in a dedicated commit — not by ad-hoc deviation.
