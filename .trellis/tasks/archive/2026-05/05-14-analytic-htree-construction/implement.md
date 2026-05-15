# Analytical H-Tree Construction Implementation Plan

## Implementation Checklist

- [x] P1: Add a real-tech experiment that composes iter-1 fitted functions with structural driven-cap operators instead of fitted driven-cap surfaces.
- [x] P1: Run the structural-cap compose experiment and record the gap against direct length-2/3 characterization.
- [x] P2: Update the mathematical problem statement so characterization fits only `F/D/P/W` and carries source capacitance as a structural operator.
- [x] P3: Add CMake skeletons for `module/analytical_characterization` and `flow/synthesis/htree/analytical_solver`.
- [x] Add analytical characterization data types:
  - model basis enum
  - normalized domain bounds
  - coefficient vector
  - residual / R2 / monotonicity metadata
  - structural cap operator
  - model catalog keyed by unit pattern and length index
- [x] Implement small least-squares fitting for 3-term affine and 6-term quadratic models without adding a large solver dependency.
- [x] Build an `AnalyticalCharacterization` facade that consumes `CharBuilder` data and produces the function model catalog plus structural cap operator catalog.
- [x] Add fit-quality checks:
  - sample count
  - domain bounds
  - max absolute / relative residual
  - R2 where applicable
  - bucket-aware output slew residuals
  - structural cap bucket compatibility checks
  - monotonicity scan over sampled points
- [x] Add unit tests for exact affine/quadratic fitting and rejected bad fits.
- [x] P4: Add `analytical_solver` request/result types.
- [x] Implement deterministic per-level segment shortlist generation from iter-1 unit models and structural cap operators.
- [x] Implement continuous candidate scoring using structural cap plus fitted delay/slew/power models.
- [x] Implement top-K candidate sequence construction per depth.
- [x] P5: Implement branch-aware functional DP labels with cap operator, slew domain, delay/power response summaries, and materialization trace.
- [x] P6: Implement interval-safe dominance and epsilon-Pareto compression after model envelopes are calibrated.
- [x] Bridge analytical candidates back to materializable `TopologyPatternLibrary` / `HTreeTopologyPattern` metadata.
- [x] P7: Validate analytical candidates through existing fanout, branch-buffer, root-driver compensation, and sink-load-region checks.
- [x] Integrate `TrySolveAnalyticalHTree` into `HTree::build` behind an explicit experimental option.
- [x] Preserve native default behavior.
- [x] Remove native-search fallback from analytical-enabled mode so analytical failures are visible instead of bypassed.
- [x] Add structured reports for analytical fit/solve/failure metrics.
- [x] Add synthetic solver tests for candidate ranking and fallback.
- [x] Run same-binary real-design native vs analytical A/B and record runtime, QoR, and Evaluation STA root-to-leaf delay error.

## Current Implementation Note

P1-P7 and the production experimental integration are implemented. The analytical path is guarded by the runtime `enable_analytical_htree` option, keeps native mode disabled by default, and uses iter-1 `F/D/P/W` models with structural source-cap operators and function-level unit composition. Analytical-enabled mode now fails visibly when no validated analytical candidate is produced instead of falling back to native search.

Focused analytical tests pass, and same-binary real-design A/B results are recorded in `research/real_design_validation.md`. Per explicit user instruction, ecc dev checking was not run for this task.

## Suggested Milestones

### Milestone 1: Fit Catalog Only

Deliver analytical characterization model fitting from existing `CharBuilder` output plus structural cap operators, with no H-tree selection behavior change.

Validation:

```bash
<repo build command for iCTS characterization tests>
```

Expected outcome:

- Fit catalog can be built from real `CharBuilder` samples.
- Structural cap operator catalog matches native source-boundary cap buckets on direct samples.
- Fit failures are explicit and logged.
- Native H-tree behavior remains unchanged.

### Milestone 2: Analytical Ranking in Tests

Deliver standalone analytical candidate scoring over structural cap and fitted `F/D/P/W` models, used only by tests or matrix runner.

Validation:

```bash
<repo build command for iCTS htree tests>
<repo test command for synthetic analytical solver tests>
```

Expected outcome:

- Candidate ranking is deterministic.
- Top-K includes native-selected or near-native candidates on representative fixtures.
- No production path change by default.

### Milestone 2.5: Branch-Aware Functional DP Prototype

Deliver a test-only DP prototype that carries structural capacitance operators, slew domains, delay/power summaries, and trace metadata.

Validation:

```bash
<repo test command for analytical DP synthetic fixtures>
```

Expected outcome:

- Interval-safe dominance never prunes a candidate whose uncertainty interval overlaps the retained frontier.
- Trace metadata is sufficient to reconstruct a native-valid pattern sequence.
- DP label counts remain bounded under epsilon-Pareto compression on representative depths.

### Milestone 3: Experimental HTree Integration

Integrate analytical solving behind an explicit `BuildOptions` or test-only switch.

Validation:

```bash
<repo test command for HTreeTest>
<repo test command for HTreeRealTechSmokeTest>
<repo test command for HTreeRealTechBranchBufferRegressionTest>
```

Expected outcome:

- Native mode remains byte-for-byte or behaviorally equivalent.
- Analytical mode either validates and embeds a materializable candidate or falls back to native.
- Reports show analytical success/fallback and fit residuals.

### Milestone 4: Runtime Evaluation

Add A/B matrix coverage and runtime summaries.

Validation:

```bash
<repo test command for HTree real-tech matrix>
<full src/operation/iCTS validation command>
```

Expected outcome:

- Runtime improvement is measured on high-frontier cases.
- Quality deltas are visible: selected depth, pattern, delay, power, root compensation, legality, fallback.
- Thresholds are ready for a decision on runtime config exposure.

## Test Cases To Add

- `AnalyticalFitTest`
  - exact affine surface
  - exact quadratic surface
  - noisy accepted surface
  - insufficient samples rejected
  - high RMSE rejected
  - out-of-domain evaluation rejected
  - non-monotone model rejected
- `AnalyticalCharacterizationTest`
  - builds catalog from synthetic `SegmentChar` rows
  - preserves pattern IDs and length indices
  - reports bucket-aware residuals
- `AnalyticalSolverTest`
  - scores fixed sequence correctly
  - ranks lower-power candidate under equal delay
  - respects branch-buffer requirement
  - rejects candidate with insufficient leaf cap coverage
  - falls back when no candidate validates
- Real-tech comparison tests
  - native mode unchanged
  - analytical mode succeeds or falls back with explicit reason
  - selected result is materializable and embedding succeeds

## Validation Commands

Exact commands should be filled in during implementation based on the current build preset. At minimum:

- Build affected iCTS targets.
- Run characterization module tests.
- Run H-tree unit tests.
- Run H-tree real-tech smoke/regression tests.
- Run full `src/operation/iCTS` validation before finish-work.

## Review Gates

- Before implementation: review P1/P2 results and confirm transition from Trellis planning to implementation (`task.py start`).
- Before implementation: confirm exposure strategy for analytical mode.
- After Milestone 1: inspect fit residuals on real-tech samples before using models for selection.
- After Milestone 2: compare top-K analytical candidates with native selected candidates.
- Before enabling in flow: confirm fallback diagnostics and native-mode non-regression.
- Before handoff: full iCTS check must be clean or in-scope findings must be fixed and rerun.

## Rollback Plan

- Keep native flow as the default path.
- Guard analytical integration behind a single experimental option.
- On any analytical failure, return to native search without mutating `BuildResult` beyond diagnostic counters.
- If analytical integration causes instability, disable the option and keep fit catalog tests as isolated work.
