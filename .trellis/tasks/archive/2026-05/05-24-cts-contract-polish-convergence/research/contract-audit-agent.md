# Research: iCTS contract audit agent

- Query: Audit `src/operation/iCTS/source/flow` and `src/operation/iCTS/source/module` headers for broad `Options`/`Result`, empty `Input`/`Config`/`Output`/`Summary`, and redundant `Output { Summary summary; }` wrappers.
- Scope: internal
- Date: 2026-05-24

## Findings

### Files Found

- `.trellis/tasks/05-24-cts-contract-polish-convergence/prd.md` - requires no empty contracts, no summary-only `Output` wrappers, and documented `Options`/`Result` exceptions.
- `.trellis/tasks/05-24-cts-contract-polish-convergence/design.md` - defines the Input/Config/Output/Summary taxonomy and the Options/Result exception rule.
- `.trellis/spec/backend/database-guidelines.md` - flow data-shape guidance for Input/Config/Output/Summary.
- `.trellis/spec/backend/quality-guidelines.md` - naming guidance allowing generic `Options`/`Result` only in narrow nested/private helper contexts.
- `src/operation/iCTS/source/flow/Flow.hh` - top-level flow lifecycle public contracts.
- `src/operation/iCTS/source/flow/setup/Setup.hh` - setup facade contract.
- `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh` and `HTree.hh` - main HTree public contract.
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh` and `topology/trunk/SourceTrunkSegment.hh` - topology/source-trunk public contracts.
- `src/operation/iCTS/source/module/routing/local_legalization/LocalLegalization.hh`, `router/Router.hh`, and `bound_skew_tree/config/BSTRoutingConfig.hh` - routing/legalization boundary contracts.
- `src/operation/iCTS/source/module/topology/TopologyGen.hh`, `config/TopologyConfig.hh`, `clustering/Clustering.hh`, and `kmeans/KMeans.hh` - topology module contracts.
- `src/operation/iCTS/source/module/characterization/builder/CharBuilder.hh` and `module/analytical_characterization/*.hh` - characterization module contracts.
- `src/operation/iCTS/source/flow/report/*.hh`, `flow/report/visualization/*.hh`, `flow/optimization/*.hh`, `flow/evaluation/*.hh`, and `flow/instantiation/*.hh` - setup/report/visualization/optimization/evaluation/instantiation stage contracts.

### Code Patterns

- Empty contract sweep: no hits for multiline `struct *Input|Config|Output|Summary { };` under `source/flow` or `source/module`.
- Exact summary-wrapper sweep: no hits for multiline `struct *Output { *Summary summary; };` under `source/flow` or `source/module`.
- Remaining `Options`/`Result` names are concentrated in algorithm submodules rather than the top-level flow facades, except the setup and clock-data findings below.

### Must-Fix Public Boundary

- `src/operation/iCTS/source/flow/setup/Setup.hh:38` declares `SetupResult`; `Setup::initializeRuntime` returns it at `Setup.hh:49`. The fields are only `setup_ready` and `reason` (`Setup.hh:40-41`), so this is execution status, not output payload. Prefer `SetupSummary` or a similarly explicit summary/status type.
- `src/operation/iCTS/source/flow/Flow.hh:54` declares `ClockDataReadOutput` with `ClockDataReadSummary summary` plus `clock_data_ready` (`Flow.hh:56-57`). The call site only gates on readiness and reads `summary.reason` (`Flow.cc:100-105`), and the producer fills only status fields (`Flow.cc:160-169`). This is not an exact summary-only wrapper, but the extra bool duplicates summary status rather than carrying design payload. Prefer returning `ClockDataReadSummary` directly, with callers using `success`/`status`.

### Likely Local Algorithm Vocabulary Acceptable If Documented

- `src/operation/iCTS/source/module/routing/local_legalization/LocalLegalization.hh:47` and `:63` use nested `Options`/`Result` inside a narrow standalone point legalization algorithm. `Router` re-exports them as `LegalizationOptions`/`LegalizationResult` at `Router.hh:47-48`; acceptable if documented as local algorithm vocabulary.
- `src/operation/iCTS/source/module/topology/clustering/Clustering.hh:66` uses `ClusterResult` for clustering payload (`clusters`, `centers`, `electrical_summaries` at `Clustering.hh:68-70`). This is topology algorithm vocabulary, not the CTS flow-stage boundary.
- `src/operation/iCTS/source/module/topology/kmeans/KMeans.hh:40` uses nested template `Result`; this is generic local algorithm vocabulary.
- `src/operation/iCTS/source/module/analytical_characterization/AnalyticalCharacterization.hh:45` and `:74` use `AnalyticalCharacterizationOptions`/`AnalyticalCharacterizationResult`; result carries a model catalog plus fit metrics/failures (`:76-81`). Public module facade, but algorithm/fitting terminology is clearer than flow-stage taxonomy if explicitly documented.
- `src/operation/iCTS/source/module/analytical_characterization/AnalyticalFit.hh:41` and `:55` use `AnalyticalFitOptions`/`AnalyticalFitResult` for least-squares fitting. Local algorithm vocabulary.
- `src/operation/iCTS/source/flow/optimization/options/OptimizationOptions.hh:31` is inside `icts::clock_sizing_optimization` and contains optimizer policy knobs (`:33-56`). The public flow facade itself is clean (`OptimizationInput` at `Optimization.hh:42`, `OptimizationSummary` at `Optimization.hh:54`).
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.hh:62` uses `CharacterizationResult` for the HTree characterization helper. It only carries success/failure/length-step status (`:64-67`), so it is borderline: acceptable as HTree-local helper vocabulary if documented, but the easiest stricter convergence rename would be `CharacterizationSummary`.
- HTree local pruning/legality/search helper names are algorithm vocabulary: `HTreeFanoutPruningOptions` and `CandidateCharRefFilterResult` (`TopologyPruning.hh:72`, `:84`), `RootDriverCompensationOptions` and `RootDriverCompensationApplyResult` (`RootDriverCompensation.hh:80`, `:140`), `SinkLoadRegionLegalityResult`/`Options`/`EntryFilterResult` (`SinkLoadRegion.hh:100`, `:113`, `:131`), `DepthSearchResult`/`DepthCandidateResult` (`DepthPlan.hh:72`, `:82`), and `PatternSearchResult` (`TopologyPatternLibrary.hh:147`).
- HTree analytical solver names are local solver vocabulary: `AnalyticalSolverOptions`/`AnalyticalSolverResult` (`AnalyticalSolver.hh:48`, `:75`), `AnalyticalDpTransitionOptions`/`AnalyticalDominanceOptions` (`AnalyticalCandidate.hh:103`, `:113`), and `AnalyticalValidationResult` (`AnalyticalValidation.hh:53`).
- Bound-skew tree detail helpers are private/detail vocabulary: `BalancePointResult` in `bound_skew_tree/algorithm/BoundSkewTreeImpl.hh:99`; `LineDistanceResult` in `bound_skew_tree/geometry/GeomCalc.hh:66`.

### Already Clean

- Main HTree boundary is clean: `HTreeInput` (`HTreeContracts.hh:70`), `HTreeConfig` (`:88`), `HTreeOutput` (`:169`), `HTreeSummary` (`:188`), and `HTreeBuild` (`:247`) all carry real payload/summary concepts. `HTreeOutput` owns topology/materialized object payload (`:171-185`); `HTreeSummary` carries metrics/diagnostics (`:190-244`).
- Main topology boundary is clean: `TopologyInput` (`Topology.hh:58`), `TopologyConfig` (`:76`), nested `Topology::Output` (`:129`) and `Topology::Summary` (`:142`) are paired through `Build` (`:154-165`) because output carries inserted object payload while summary carries execution status/metrics. `SourceTrunkOutput`/`SourceTrunkSummary` follow the same pattern (`:168`, `:179`).
- `SourceTrunkSegment` is clean: nested `Input` (`SourceTrunkSegment.hh:51`), `Config` (`:65`), `Output` (`:70`), and `Summary` (`:81`) all have non-empty stage meaning; `Build` pairs payload and observations (`:96-107`).
- Characterization builder boundary is clean: `CharBuilderInput` (`CharBuilder.hh:56`) groups runtime services and characterization facts; `CharBuilderConfig` (`:68`) contains behavior-affecting grid/routing knobs.
- Routing config is clean: `BSTRoutingConfig` (`BSTRoutingConfig.hh:49`) carries bounded-skew routing parameters; `BSTRouter` returns `ClockSteinerTreeType` payload directly (`BSTRouter.hh:45-48`); `Router` route APIs return tree/RC payloads directly (`Router.hh:53-65`).
- Topology generation is clean: `TopologyGenInput` (`TopologyGen.hh:50`) groups reporting/root-location context; `TopologyGenConfig` (`:62`) carries behavior knobs; tree payload is returned directly (`:78-80`). `BiPartitionConfig` and `ClusterConfig` carry real algorithm knobs (`TopologyConfig.hh:36`, `:67`).
- Report and visualization contracts are clean: `ReportInput`/`ReportConfig`/`ReportSummary` (`Report.hh:41`, `:54`, `:59`), `VisualizationInput`/`VisualizationConfig`/`VisualizationSummary` (`Visualization.hh:39`, `:49`, `:55`), `SvgVisualizationInput`/`SvgVisualizationSummary` (`SvgVisualization.hh:41`, `:51`), and `GdsVisualizationInput`/`GdsVisualizationSummary` (`GdsVisualization.hh:41`, `:51`) are non-empty and not output wrappers.
- Evaluation is clean: `EvaluationOutput` (`Evaluation.hh:38`) carries `EvaluationState` payload and `EvaluationSummary` (`:40-41`); `EvaluationState` carries `QorSummary` and `Qor` statistics (`QorEvaluation.hh:114-118`).
- Optimization is clean at the public flow boundary: `OptimizationInput` (`Optimization.hh:42`) groups runtime dependencies; `OptimizationSummary` (`:54`) carries stage status/counters.
- Instantiation is clean: `InstantiationInput`/`InstantiationSummary` (`Instantiation.hh:39`, `:47`) and `IdbConversionInput`/`IdbConversionSummary` (`IdbConversion.hh:39`, `:47`) carry real inputs and execution summaries.
- Clock-data substage helper is clean by itself: `ClockDataRead::read` returns a direct `bool` (`ClockDataRead.hh:40`); the remaining issue is only the wrapping `Flow::readClockData` contract noted above.

### Related Specs

- `.trellis/spec/backend/database-guidelines.md`: Input/Config/Output/Summary shape rules and explicit runtime dependency passing.
- `.trellis/spec/backend/quality-guidelines.md`: avoid standalone generic `Options`/`Result`; nested narrow helper types can be acceptable.
- `.trellis/spec/project-constraints.md`: iCTS backend scope and validation constraints.

### External References

- None. This audit used internal task artifacts, Trellis specs, and repository headers only.

## Caveats / Not Found

- `python3 ./.trellis/scripts/task.py current --source` reported no active task, so the audit used the explicit task path from the prompt.
- This was a read-only header audit plus selected call-site checks for the two likely public issues; no source edits, builds, or tests were run.
- No empty `Input`/`Config`/`Output`/`Summary` structs were found in the target headers.
- No exact redundant `Output { Summary summary; }` wrapper was found in the target headers.
- If final convergence wants zero public-header `Options`/`Result` suffixes regardless of locality, prioritize the borderline public module helpers after the two must-fix items: `AnalyticalCharacterization*`, `AnalyticalFit*`, `CharacterizationResult`, and `LocalLegalization::Options/Result`.
