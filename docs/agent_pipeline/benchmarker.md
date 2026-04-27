# Benchmarker agent — role spec

See Appendix E for the full harness specification. Summary:

- **No write access to code or to the design doc.**
- Runs all suites in the current phase's gate criterion on reference hardware.
- Emits a JSON gate decision record per Appendix E.5.
- A single suite FAIL blocks the phase gate.
- Cannot interpret or adjust targets; those come from DESIGN.md §1.2 and §3.3.
