# Numerical H-Tree Characterization

## Goal

Build a mathematical alternative to the current enumerative H-tree characterization flow for iCTS. The new flow should reuse a small initial characterization built through the existing CharBuilder/iSTA/iPA path, fit compact numerical models over input slew and load capacitance, and derive H-tree QoR and level segment patterns analytically or near-analytically instead of relying on the current expensive concatenation, H-tree composition, and exhaustive pattern enumeration.

## What I Already Know

* The existing characterization flow enumerates input slew and load capacitance, then queries iSTA/iPA for delay, output slew, and power-like values.
* User research indicates that, for a fixed characterization pattern, related query values can be represented as linear or quadratic functions of `slew_in` and `cap_load` with high R2 and low RMSE.
* The requested implementation must add new modules under:
  * `src/operation/iCTS/source/module/numerical_characterization`
  * `src/operation/iCTS/source/flow/numerical_htree`
* Existing source code should not be modified unless a later build/test integration point is proven unavoidable and explicitly minimized.
* The new method should compare against the existing H-tree algorithm using the ARM9 full-design/full-sink case.
* The final in-scope iCTS check should run only after the numerical method passes the requested acceptance target; do not use `ecc_dev_tools` checks during the development loop.

## Requirements

* Provide a numerical characterization module that can fit functions for delay, output slew, power, and related QoR terms from sparse initial char samples.
* Provide a numerical H-tree flow that can construct a function space for a requested H-tree shape and compute QoR expressions without enumerating all concatenation variants.
* Given a target objective, select per-level segment patterns directly from fitted models rather than by exhaustive H-tree composition enumeration.
* Preserve comparison compatibility with the existing H-tree flow, including reporting QoR deltas and selected segment patterns.
* Add tests that compare the numerical flow against the existing H-tree algorithm on the ARM9 full-design/full-sink scenario.
* Keep the implementation isolated in the requested new directories as much as possible.
* Use existing CharBuilder as the initial sparse sample source; do not add new direct iSTA/iPA call paths outside the existing adapter boundary.
* Use a top-K discrete numerical optimizer for the first implementation rather than introducing a MILP/QP solver dependency.
* Add only minimal parent CMake/test wiring required to compile the new isolated directories.

## Acceptance Criteria

* [ ] Numerical characterization fits sparse init-char data and exposes model quality metrics such as R2/RMSE.
* [ ] Numerical H-tree flow computes selected segment patterns and QoR for each level.
* [ ] Runtime is better than the native H-tree method on the ARM9 full-design/full-sink comparison test.
* [ ] QoR is broadly consistent with the native H-tree method: default automated tolerance is relative delay <= 20% and relative power <= 25% unless realtech data indicates a tighter safe bound.
* [ ] The comparison test records both native and numerical outputs in an inspectable artifact.
* [ ] Final iCTS in-scope checks pass with no in-scope findings.

## Definition of Done

* New code is implemented under the requested numerical module/flow directories.
* Focused tests for fitting/model composition/optimization pass.
* ARM9 full-design/full-sink comparison test passes or skips only when realtech prerequisites are unavailable.
* Final iCTS full/in-scope verification is run after acceptance testing.
* Research and design tradeoffs are persisted under this task directory.

## Out of Scope

* Replacing or deleting the existing H-tree builder in this task.
* Large refactors of existing iCTS characterization, timing, routing, or synthesis code.
* Changing iSTA/iPA APIs unless unavoidable for a narrow adapter.
* Guaranteeing exact pattern identity with the native enumerative flow; QoR parity and runtime improvement are the target.

## Open Questions

* If the first ARM9 run shows the default QoR tolerance is too loose/tight, adjust it based on observed native/numerical deltas and record the decision.

## Technical Notes

* Existing likely implementation areas:
  * `src/operation/iCTS/source/module/characterization`
  * `src/operation/iCTS/source/flow/htree`
  * `src/operation/iCTS/test/module/characterization`
  * `src/operation/iCTS/test/flow/htree`
* Existing ARM9-style entry points found so far:
  * `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc`
  * `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc`
* Research outputs will be persisted in `research/` before implementation decisions are finalized.

## Research References

* `research/existing-characterization.md` - documents current CharBuilder sampling, Segment/HTree char models, hash-join composition, frontier pruning, and iSTA/iPA query path.
* `research/existing-htree-arm9-tests.md` - documents HTreeBuilder result fields, selected pattern reporting, QoR/runtimes, artifacts, and ARM9 full-sink test gating.
* `research/numerical-methods-web.md` - supports response-surface polynomial fitting plus hybrid DP/top-K selection as the first implementation strategy.
* `research/build-test-integration.md` - confirms new source/test dirs require minimal parent CMake wiring.

## Technical Approach

Use a hybrid numerical approach:

* Sparse initial characterization remains delegated to the existing CharBuilder. The numerical module consumes `SegmentChar`/`BufferingPattern` outputs and lattice metadata.
* `numerical_characterization` fits per-pattern response models over physical `(slew_in_ns, cap_load_pf)`:
  * affine basis `[1, s, c]`
  * quadratic basis `[1, s, c, s*c, s^2, c^2]`
  * normalized variables for conditioning
  * metrics: sample count, rank, RMSE, R2, max absolute error
* Because existing `SegmentChar` stores exact delay/power but only binned output slew/driven cap, the first implementation reconstructs physical output slew/driven cap from active lattices. This is acceptable for an isolated prototype; a future exact-sample adapter can capture pre-binning values.
* `numerical_htree` builds level candidates from fitted per-pattern models, scores them at representative load/slew scenarios, keeps top-K candidates per level, and composes QoR with existing H-tree semantics:
  * delay is additive by level
  * binary fanout power uses `up + 2 * (down - downstream_source_boundary_switch)`
  * downstream driven cap relation uses continuous half-load cap for scoring and lattice bins only for compatibility reporting
* Final selection follows a delay/power Pareto-style policy compatible with native HTreeBuilder reporting.
* ARM9 comparison test calls native `HTreeBuilder::build()` and the numerical flow side-by-side, measures runtimes externally, compares selected QoR and per-level segment patterns, and writes a `matrix_report.txt`-style artifact.

## Implementation Plan

* Step 1: Add `numerical_characterization` library with `Polynomial2D`, fit solver, fit metrics, sample extraction from `SegmentChar`, and focused synthetic tests.
* Step 2: Add `numerical_htree` library with public flow/result API, level candidate scoring, top-K/DP pattern selection, H-tree QoR composition, and unit tests against tiny enumerated references.
* Step 3: Add minimal CMake wiring and test targets for the two new module/flow directories.
* Step 4: Add ARM9 realtech comparison test, env-gated like existing HTree ARM9 matrix, with inspectable runtime/QoR/pattern report.
* Step 5: Run focused tests during development. Do not run `ecc_dev_tools` until acceptance passes.
* Step 6: After acceptance passes, run the final iCTS full/in-scope check and fix any in-scope findings.

## Decision (ADR-lite)

**Context**: The current H-tree flow pays exponential/combinatorial cost in segment pattern enumeration and then further cost in hash-join frontier composition. The user-provided observation indicates per-pattern timing/power values are well approximated by linear/quadratic functions of input slew and load cap.

**Decision**: Implement a solver-free hybrid numerical method: sparse CharBuilder sampling, per-pattern polynomial response surfaces, top-K discrete pattern selection, and H-tree QoR composition using existing delay/power/fanout semantics.

**Consequences**: This avoids new optimizer dependencies and keeps outputs compatible with current iCTS data models. It may not produce exact native pattern IDs and can carry quantization error for output slew/driven cap until a future exact sample adapter is added. QoR parity is validated against native HTreeBuilder rather than assumed.
