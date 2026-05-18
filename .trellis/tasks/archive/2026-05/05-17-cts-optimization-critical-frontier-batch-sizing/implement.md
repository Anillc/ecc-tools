# Implementation Plan: CTS optimization fast STA search quality

## Phase 1: Rebuild Task and Baseline

- [x] Replace old char-backed PRD/design/implementation text with fast STA-backed scope.
- [x] Confirm current active task points to this task.
- [x] Collect current fast STA `ics55_dev` binary logs for 80ps, 40ps, and 0ps, reusing fresh logs only when provenance is clear.
- [x] Extract current fast STA baseline metrics:
  - total flow runtime,
  - optimization runtime,
  - initial/optimized fast STA skew,
  - final iSTA setup/hold skew,
  - accepted/rejected/cap/slew-rejected counts where available,
  - area delta,
  - transition distribution,
  - relevant characterization/runtime summary fields.
- [x] Locate previous iSTA/char-backed logs or task artifacts.
- [x] Summarize current-vs-previous differences and call out gaps that require reproducible old-commit runs.

## Phase 2: Search Design Refinement

- [x] Inspect current `flow/optimization` fast STA solver data structures and fast STA clock context topology fields.
- [x] Decide whether critical-frontier search can stay local to `Optimization.cc` or needs a small helper split.
- [x] Define objective ordering:
  - reach target skew first,
  - minimize area among target-satisfying states,
  - otherwise minimize skew spread,
  - use deterministic tie-breaks.
- [x] Define bounded late and early critical frontiers so runtime remains acceptable on `ics55_dev`.
- [x] Define batch-first candidate templates for late-side upsize and early-side downsize/intentional delay balancing.
- [x] Define cap and slew no-new-violation legality checks using fast STA state only.

## Phase 3: Implementation

- [x] Implement fast STA-backed late/early critical frontier candidate collection.
- [x] Implement batch-first candidate generation; do not run an exhaustive global single-buffer scan as the first tier.
- [x] Implement fast STA batch sizing evaluation or an equivalent bounded transaction so one batch is validated as a complete state.
- [x] Validate every accepted batch through fast STA timing/power/slew/cap updates.
- [x] Keep no-new-cap-violation and no-new-slew-violation legality.
- [x] Keep default logs concise and add only compact search counters.

## Phase 4: Validation

- [x] Build the affected iCTS targets or `scripts/design/ics55_dev/iEDA`.
- [x] Run `ics55_dev` pressure tests at 80ps, 40ps, and 0ps.
- [x] Summarize before/after metrics against the Phase 1 baseline.
- [x] Run final full check only after convergence:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Notes

- Do not run `ecc_dev_tools` during normal development.
- Do not commit until implementation and pressure-test results are ready for review.
- Do not restore old `module/buffer_sizing` char-backed optimization code.
