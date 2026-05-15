# Analytical H-Tree Construction Design

## Summary

The native H-tree flow should remain the source of truth for legality and materialization. The analytical path should initially act as a fast candidate ranking and shortlisting layer that uses fitted electrical models to avoid expanding the full native discrete frontier where possible.

The recommended route is now the Structural-Cap Functional Pareto DP path:

```text
native topology + native characterization samples
  -> iter-1 F/D/P/W model fitting
  -> exact structural capacitance operator composition
  -> branch-aware analytical frontier / shortlist
  -> native legality and materialization validation
  -> existing BuildResult / embedding / reporting
  -> visible analytical failure on any analytical-enabled failure
```

This keeps implementation risk controlled because the current code depends heavily on materializable `PatternId` metadata and physical legality passes.

## Native H-Tree Construction Flow

The native path is:

1. `Synthesis::run` iterates clocks and prepares sink-domain contexts.
2. `Topology::formClock` builds sink-domain trees and source-to-root trunk topology.
3. `HTree::build` validates root net preconditions and calls `TopologyGen::build` to build the geometric binary topology.
4. `RunCharacterizationFlow` collects topology level lengths, adapts the characterization grid, and ensures `CharacterizationLibrary` has usable `CharBuilder` data.
5. `BuildLevelPlans` computes average parent-child Manhattan length per topology level and aligns it to the characterization length lattice.
6. `ResolveDepthCandidates` picks explicit or windowed candidate depths.
7. `SynthesizeSegmentFrontiers` groups and composes `SegmentChar` entries for required level lengths.
8. `SearchTopologyDepthCandidates` evaluates each depth by composing one segment frontier per level into `HTreeTopologyChar` frontiers.
9. Root-driver compensation, root fanout filtering, sink-load-region legality, and boundary coverage are applied.
10. The selected delay/power Pareto entry is materialized into `HTreeTopologyPattern`.
11. `BuildEmbedding` creates or rewires CTS `Inst`, `Pin`, and `Net` objects from selected segment patterns.

The important consequence is that selection is not just numeric. The selected candidate must carry enough pattern metadata for compensation, legality, embedding, reporting, and root-driver sizing.

## Existing Characterization Flow

`CharBuilder` currently:

- Builds uniform lattices for wirelength, input slew, and load cap.
- Enumerates every buffer topology bitset over length-index slots.
- Enumerates monotonic nondecreasing buffer-cell combinations for selected slots.
- Creates temporary iSTA char-only circuits.
- Sweeps `load_cap` and `input_slew`.
- Queries iSTA for delay/output slew and iPA for power/source-boundary net switching power.
- Converts physical output slew and driven cap to lattice indices.
- Stores successful samples as `SegmentChar` entries and stores pattern metadata as `BufferingPattern`.

The expensive parts are:

- Exponential pattern topology enumeration by length slots.
- Cross product of `patterns * load_steps * slew_steps`.
- iSTA/iPA updates per sample.
- Hash-join frontier expansion across segment and H-tree levels.

The analytical path can reduce cost by modeling target metrics as functions of `(slew_in, cap_load)` and using those functions to rank or filter candidates before full native frontier expansion.

## Analytical Characterization Module

Place under:

```text
src/operation/iCTS/source/module/analytical_characterization/
```

Proposed responsibilities:

- Build fitted models from existing `SegmentChar` samples and `CharBuilder` lattice metadata.
- Build structural source-capacitance operators from pattern geometry and buffer input-pin capacitance semantics.
- Store model coefficients in normalized physical units.
- Evaluate model predictions for arbitrary in-domain `(slew_in, load_cap)` points.
- Report fit quality and domain bounds per `(pattern_id, length_idx, metric)`.
- Reject or mark models unusable when residuals or monotonicity checks fail.

Recommended files:

- `AnalyticalModel.hh/.cc`: coefficient storage, metric evaluation, domain guard.
- `AnalyticalFit.hh/.cc`: small least-squares fitting helpers for affine/quadratic surfaces.
- `AnalyticalCharacterization.hh/.cc`: high-level builder from `CharBuilder` data to model catalog.
- `AnalyticalCharacterizationReport.hh/.cc` if reporting becomes large enough to split.

### Model Form

For each materializable segment pattern and length index:

```text
slew_out(s, c) =
  a0 + a1*s + a2*c

delay(s, c) =
  b0 + b1*s + b2*c + b3*s^2 + b4*s*c + b5*c^2

power(s, c) =
  q0 + q1*s + q2*c + q3*s^2 + q4*s*c + q5*c^2

source_boundary_switch_power(s, c) =
  w0 + w1*s + w2*c + w3*s^2 + w4*s*c + w5*c^2
```

Driven capacitance is not a fitted model. It is an affine structural operator:

```text
C_a(c) = alpha_a * c + eta_a

wire-only:
  alpha_a = 1
  eta_a = wire capacitance of the unit

buffered:
  alpha_a = 0
  eta_a = first buffer input pin capacitance + pre-buffer wire capacitance

branch/root:
  alpha_a = fanout factor
  eta_a = junction/root wire capacitance
```

Use normalized variables:

```text
s_norm = s / max_slew_ns
c_norm = c / max_cap_pf
```

The MVP should use affine output slew by default unless the data proves affine is insufficient. Delay and power can use quadratic fits, but the current experiment results make affine a reasonable first implementation target. If a quadratic fit is not positive or monotone enough, the model should be rejected instead of extrapolated.

### Fit Quality Gates

Fit metadata should include:

- sample count
- domain min/max for slew and cap
- RMSE
- max absolute residual
- max relative residual
- R2 when meaningful
- monotonicity pass/fail on the sampled grid
- bucket-aware residuals:
  - output slew residual in ns and equivalent lattice buckets
  - structural cap bucket compatibility against native source-boundary buckets

Recommended initial rules:

- Require enough samples for the chosen basis (`>= 3` affine, `>= 6` quadratic, preferably more).
- Reject output slew models when max residual exceeds the configured safe envelope.
- Reject structural cap operators only when they cannot be reconciled with native bucket semantics or required pattern metadata.
- Reject delay / power models when max relative residual exceeds a configurable threshold.
- Reject models that predict nonpositive slew/cap/delay for in-domain candidate points.
- Reject any analytical solve that requires extrapolation outside the training domain.

## Analytical Solver Module

Place under:

```text
src/operation/iCTS/source/flow/synthesis/htree/analytical_solver/
```

Proposed responsibilities:

- Consume `Tree`, `LevelPlan`, `SegmentFrontierCatalog`, `BufferPatternLibrary`, boundary constraints, root compensation options, fanout options, and the analytical model catalog.
- Produce a top-K sequence of materializable segment pattern IDs per depth.
- Validate shortlisted candidates through existing native legality and compensation paths.
- Return a result compatible with the selected candidate path in `HTree::build`.
- Return explicit analytical failure reasons.

Recommended files:

- `AnalyticalSolver.hh/.cc`: public entry point and result type.
- `AnalyticalCandidate.hh/.cc`: candidate sequence, scoring, stable ordering.
- `AnalyticalValidation.hh/.cc`: bridge to native `CandidateBuildEvaluation`, sink-load legality, and root-driver compensation.
- `AnalyticalSolverReport.hh/.cc` if diagnostics become substantial.

## Structural-Cap Functional Pareto DP

The target solver should use structural capacitance and functional timing/power labels:

```text
label = {
  structural state,
  cap operator C_L,
  delay response delta_L(s),
  power response pi_L(s),
  input slew interval I_L,
  materialization trace
}
```

Regular slot prepend:

```text
c_down = C_L(C_leaf)
C_L' = C_a o C_L
s_out = F_a_safe(s, c_down)
delta_L'(s) = D_a(s, c_down) + delta_L(s_out)
pi_L'(s) = M_l * (P_a(s, c_down) - beta * W_a(s, c_down)) + pi_L(s_out)
```

Branch/root transitions compose capacitance operators and leave downstream delay/power ownership unchanged unless there is an explicit junction pseudo-slot.

## Solver MVP

The implementation MVP should still use analytical ranking with native validation before it attempts a full replacement:

1. Build iter-1 analytical models for `F/D/P/W`.
2. Build structural cap operators for unit patterns, branch coupling, and root closure.
3. Compose continuous boundary estimates with structural cap and function-level slew/delay/power.
4. Build a deterministic top-K candidate set per depth by power/delay/boundary slack.
5. Convert top-K candidates back to materializable topology pattern metadata.
6. Validate candidates through existing root-driver compensation, sink-load legality, fanout, branch-buffer, and embedding preconditions.
7. Select using the existing delay/power Pareto policy where practical.
8. Return a visible analytical failure if no validated analytical candidate remains; keep native `SearchTopologyDepthCandidates` available only when analytical mode is disabled.

The MVP should avoid a new MIQP/QCQP dependency. The current repository has no obvious production numerical-optimization dependency wired into iCTS H-tree, and the first useful speedup can come from candidate shortlisting plus native validation.

## Compatibility Boundaries

The analytical path must preserve:

- `HTree::BuildResult` fields used by callers and reports.
- `HTree::LevelPlan::segment_pattern_id` and selected buffer metadata.
- `HTreeTopologyPattern` materialization as one segment pattern ID per H-tree level.
- `BufferingPattern` positions, cell masters, terminal branch-buffer flag, and monotonic boundary state.
- `UniformValueLattice` semantics for covering indices.
- Root-driver compensation and strict closure via `RootDriverCompensationPass`.
- Sink-load-region legality via `ResolveSinkLoadRegionLegality`.
- Embedding via `BuildEmbedding`.

## Failure Handling

Analytical solving should return `success=false` with an explicit failure reason for recoverable cases:

- model fit unavailable or insufficient samples
- residual threshold failure
- monotonicity failure
- out-of-domain solve point
- no legal top-K candidate
- root-driver closure mismatch
- sink-load-region violation
- missing pattern metadata
- invalid lattice conversion

Analytical-enabled mode should not continue into native H-tree search after these failures. This is intentional for the expert-route validation phase: failures must be visible instead of being hidden by native search. Native search remains the default path when analytical mode is disabled.

Fallback must be visible in structured logs and must continue through the native H-tree path.

## CMake Shape

Add:

```text
source/module/analytical_characterization/CMakeLists.txt
source/flow/synthesis/htree/analytical_solver/CMakeLists.txt
```

Update parent CMake files:

```text
source/module/CMakeLists.txt
source/flow/synthesis/htree/CMakeLists.txt
```

Link analytical solver privately to existing H-tree submodules and analytical characterization. Avoid duplicating include paths when target links already expose headers.

## Validation Strategy

Use staged validation:

0. Structural-cap function compose experiment:
   - compose length-2/3 patterns from iter-1 `F/D/P/W` models plus structural cap operators
   - compare against direct characterization for driven cap, output slew, delay, power, and source-boundary switching power
   - require driven cap to return close to native direct characterization before using iter-1 models as the solver base
1. Fitting unit tests:
   - exact affine/quadratic surfaces
   - noisy but acceptable surfaces
   - insufficient samples
   - out-of-domain rejection
   - bucket-aware residual rejection
2. Solver unit tests on synthetic segment/frontier catalogs:
   - top-K deterministic ranking
   - branch-buffer filtering
   - leaf cap coverage
   - explicit analytical failure on validation failure
3. Existing native tests:
   - keep current `HTreeTest`, `SegmentJoinTest`, `HTreeJoinTest`, and real-tech smoke/regression behavior in native mode.
4. A/B real-tech matrix:
   - compare native vs analytical depth, pattern, delay, power, root compensation, analytical failure/fallback status, frontier count, and runtime.

## Risks

- A scalar analytical objective will not exactly match the native median-of-Pareto selection policy unless the solver reconstructs a Pareto set.
- Poor fit around lattice boundaries can produce candidates that look good analytically but fail native closure.
- Top-K that is too small can miss legal candidates and cause analytical-enabled mode to fail visibly.
- Directly replacing `CharBuilder` output without pattern metadata would break embedding.
- A full mixed-integer optimizer would duplicate much of the existing legality machinery and create a dependency decision outside the MVP.

## Recommended First Review Decision

Decide how analytical mode should be exposed:

- A runtime config switch for experimental CTS runs.
- A `HTree::BuildOptions` flag for test-only / internal integration.
- A matrix-runner-only mode first, with no default flow behavior change.

Recommendation: start with a `BuildOptions` or test-only path plus explicit matrix comparison. Promote to runtime config after fit/selection quality is measured.
