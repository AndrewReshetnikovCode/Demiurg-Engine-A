# Benchmarker harness

Implementation of Appendix E §E.7 — the C++ binary + Python orchestrator
that runs Layer 1's gate suites.

- `main.cpp` — `demen_benchmarker` executable. Times a suite by shelling
  to its acceptance-test binary and emits a single-suite JSON record.
- `run.py` — orchestrator. Iterates the SUITES list, invokes the harness
  per suite, aggregates results into a §E.5 gate decision record under
  `build/benchmarker/decisions/`.

The Layer-1 cut wires only the suites whose acceptance binaries already
exist (B-STREAM, B-MESH, B-TEXGEN). Add to SUITES + main.cpp's `kSuites`
table as new suites land.
