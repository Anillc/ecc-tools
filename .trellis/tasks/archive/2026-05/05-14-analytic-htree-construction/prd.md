# Analytical H-Tree Construction

## Goal

Build an analytical CTS H-tree path that can replace or short-circuit the native discrete H-tree search for selected cases while preserving the existing CTS legality, embedding, reporting, and native-mode behavior.

The initial goal is not to remove the native implementation. The first deliverable should introduce an analytical characterization and solver path that uses fitted models over `(input_slew, load_cap)` to rank or shortlist materializable H-tree candidates, then validates the selected candidate through the existing native legality and embedding contracts.

## Background / Known Context

- The native H-tree is built from `HTree::build` in `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`.
- The current flow constructs a geometric binary topology, adapts a characterization grid to topology level lengths, builds segment frontiers, searches topology depths, filters by sink-load legality and root-driver closure, then materializes the selected pattern into `Inst` / `Pin` / `Net` objects.
- Existing characterization enumerates `slew_in` and `cap_load` points for each buffering pattern and queries iSTA/iPA to obtain delay, output slew, driven cap, and power.
- Prior empirical work indicates that, for a reasonably simple characterization, target values can be modeled as affine or quadratic functions of `slew_in` and `cap_load`, with high R2 and low RMSE.
- The requested new implementation areas are:
  - `src/operation/iCTS/source/module/analytical_characterization`
  - `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver`
- Research artifacts:
  - `research/native_htree_flow.md`
  - `research/characterization_pipeline.md`
  - `research/analytical_solver_options.md`

## Requirements

- Preserve the existing H-tree public behavior for default/native mode.
- Add an analytical characterization path capable of fitting delay, output slew, local power, and source-boundary switching power models from existing `SegmentChar` samples or equivalent analytical samples.
- Treat source-side driven capacitance as a structural capacitance operator, not as a fitted response surface:
  - wire-only unit: `C_source(c) = c + C_wire`
  - buffered unit: `C_source(c) = C_in(first_buffer) + C_prewire`
  - branch/root coupling: affine fanout operators.
- Keep model inputs in physical units internally (`ns`, `pF`, `um`) and convert to `UniformValueLattice` indices only at compatibility boundaries.
- Preserve materializable pattern metadata: selected results must map back to concrete `BufferingPattern` / `HTreeTopologyPattern` data so existing embedding can build buffer insts and nets.
- Preserve existing hard constraints:
  - branch-buffer forcing
  - monotonic buffer-strength composition
  - fanout legality
  - sink-load-region legality and leaf-load cap coverage
  - root-driver compensation and strict boundary closure
- Prefer analytical candidate ranking or shortlisting for the MVP, followed by native validation and materialization.
- In analytical-enabled mode, do not bypass failed analytical solving by continuing into native H-tree search; return a visible analytical failure reason instead.
- Keep native H-tree search as the default behavior when analytical mode is disabled.
- Emit structured diagnostics for analytical mode, including fit residuals, selected/failure path, validation failures, and runtime comparison where available.
- Do not introduce a large external optimizer dependency in the MVP unless a separate design decision approves it.

## Non-Functional Requirements

- The analytical path must be deterministic for identical inputs.
- The analytical path must not silently extrapolate outside the characterized slew/cap domain.
- Fit quality gates must be explicit and bucket-aware, especially for output slew because it affects feasibility and boundary propagation.
- Structural capacitance must remain bucket-compatible with native validation, but it must not be modeled with fitted residuals like timing/power responses.
- Recoverable analytical failures must return safe failure information and log via existing iCTS logging/schema helpers; no exceptions.
- New C++ files must follow iCTS conventions: `.hh` / `.cc`, PascalCase filenames, required copyright/Doxygen header, `#pragma once`, CMake updates before implementation.

## Acceptance Criteria

- [ ] A task design exists for analytical characterization and analytical H-tree solving.
- [ ] The native H-tree construction flow and characterization pipeline are documented in task research.
- [ ] The selected MVP approach is explicit: analytical shortlist/ranking with native validation and no native-search bypass in analytical-enabled mode.
- [ ] The future implementation can produce an `HTree::BuildResult` compatible with existing embedding and reports.
- [ ] Analytical failure reasons are visible in structured logs and do not change native-mode semantics.
- [ ] Unit tests cover fitting on known affine/quadratic surfaces and out-of-domain rejection.
- [ ] Existing H-tree tests continue to pass in native mode.
- [ ] Real-tech smoke/regression tests compare native and analytical selected depth, pattern, delay, power, root compensation, legality, analytical failure/fallback status, and runtime.

## Definition of Done

- Planning artifacts (`prd.md`, `design.md`, `implement.md`, and research files) are complete enough to start implementation after review.
- CMake and source layout changes are planned before code is added.
- Implementation includes focused unit tests and real-tech regression coverage.
- Full `src/operation/iCTS` validation is run before handoff after implementation.

## Out of Scope For MVP

- Removing or rewriting the native discrete H-tree implementation.
- Full MIQP/MINLP pattern selection with a new external solver dependency.
- Replacing root-driver compensation with a fitted analytical model.
- Replacing sink-load-region legality, clustering, routing, or embedding logic.
- Optimizing arbitrary continuous buffer positions without mapping back to `BufferingPattern`.

## Open Questions

- Should analytical mode be enabled by a new runtime config switch, by an experimental build option, or by an internal A/B matrix runner first?
- What fit-quality thresholds should be enforced for delay, slew, and power before analytical selection is allowed to replace native selection?
- Should MVP select one analytical best candidate or a top-K shortlist per depth for native validation?

## Research References

- `research/native_htree_flow.md`
- `research/characterization_pipeline.md`
- `research/analytical_solver_options.md`
