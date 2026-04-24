# Numerical H-Tree Characterization Design

## Summary

The implementation will add an isolated numerical path next to the current enumerative iCTS H-tree path. It keeps the expensive iSTA/iPA interaction inside existing `CharBuilder`, fits compact response surfaces from a reduced characterization library, then selects level segment patterns by numerical scoring and top-K composition rather than exhaustive hash-join frontier enumeration.

## Key Constraints

* Do not replace, delete, or refactor the existing H-tree implementation.
* Keep production source additions under:
  * `src/operation/iCTS/source/module/numerical_characterization`
  * `src/operation/iCTS/source/flow/numerical_htree`
* Minimal parent CMake wiring is allowed because iCTS CMake discovery is explicit.
* Development checks should use focused build/tests only; do not use `ecc_dev_tools` until acceptance passes.
* Final verification must run an iCTS in-scope/full check and leave no in-scope findings.

## Module Design

### numerical_characterization

Responsibilities:

* Convert existing `SegmentChar` records plus `UniformValueLattice` metadata into numerical samples.
* Fit per-pattern 2D response models for:
  * delay
  * output slew
  * power
  * driven cap
  * source-boundary switching power when enough variation exists
* Expose fit quality:
  * sample count
  * rank
  * RMSE
  * R2
  * max absolute error
  * model status

Core types:

* `NumericalSample`
* `Polynomial2D`
* `PolynomialFitOptions`
* `FitMetrics`
* `PatternResponseModel`
* `NumericalCharLibrary`

Implementation notes:

* Use physical units at the public API boundary.
* Normalize variables internally before least-squares fitting.
* Prefer quadratic basis when sample count/rank supports it, otherwise fall back to affine/constant.
* Implement a small dense least-squares helper scoped to these tiny systems; avoid adding a global solver dependency.
* Existing `SegmentChar` provides exact delay/power but binned output slew and driven cap. Reconstruct physical values from lattice indices in v1 and record this in fit/report metadata.

### numerical_htree

Responsibilities:

* Build a numerical H-tree solution from real loads and fitted segment response models.
* Reuse existing public data types where possible:
  * `HTreeTopologyChar`
  * `HTreeTopologyPattern`
  * `PatternId`
  * `HTreeBuilder::LevelPlan`-compatible fields where practical
* Select one segment pattern per level using top-K numerical scoring.
* Compose selected QoR analytically/recursively:
  * `delay_total = delay_up + delay_down`
  * `power_total = power_up + 2 * (power_down - source_boundary_switch_down)`
* Report runtime, selected depth, selected pattern ids, selected delay/power, model quality summary, and native comparison deltas.

Core types:

* `NumericalHTreeOptions`
* `NumericalHTreeLevelResult`
* `NumericalHTreeResult`
* `NumericalHTreeComparison`
* `NumericalHTreeBuilder`

Implementation notes:

* Use top-K candidate retention to avoid full frontier explosion.
* Keep the first version deterministic and solver-free.
* Use representative top/input slew and actual leaf load cap derived from test inputs/config. If exact actual-load filtering cannot be reused cleanly without copying large private code, record numerical coverage using the selected leaf load cap and compare against native result fields.
* Materializing CTS objects is optional for the first comparison flow; selected pattern/QoR/reporting is required.

## Testing Plan

1. Synthetic fitting tests:
   * exact affine surface
   * exact quadratic surface
   * noisy quadratic with nonzero RMSE and high R2
   * rank-deficient fallback behavior

2. Numerical H-tree unit tests:
   * tiny two-level model with known best pattern
   * binary fanout power composition
   * top-K candidate pruning still selects the expected candidate

3. ARM9 realtech comparison:
   * env-gated, e.g. `ICTS_RUN_ARM9_NUMERICAL_HTREE_COMPARISON=1`
   * skip if realtech setup unavailable
   * use full ARM9 clock sinks
   * run native `HTreeBuilder::build()` and numerical flow
   * require numerical runtime < native runtime
   * require delay/power deltas within default tolerances
   * write native/numerical runtime, QoR, selected depth, per-level segment ids, model metrics, and deltas to an inspectable report

## Subagent Implementation Split

* Worker A owns `src/operation/iCTS/source/module/numerical_characterization` and module tests.
* Worker B owns `src/operation/iCTS/source/flow/numerical_htree` and flow tests.
* Worker C owns CMake wiring and ARM9 comparison test, after Worker A/B APIs exist or with coordination against their headers.

Workers must not revert each other's changes and must keep write sets disjoint where possible.
