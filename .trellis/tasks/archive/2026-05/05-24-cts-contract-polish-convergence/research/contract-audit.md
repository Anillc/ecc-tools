# Research: iCTS contract convergence audit

- Query: Audit iCTS flow/module contracts after desingleton and contract-polish refactors.
- Scope: `src/operation/iCTS/source`, `src/operation/iCTS/test`, and task artifacts.
- Date: 2026-05-24
- Final validation refresh: 2026-05-25 04:30 CST

## Current Contract State

The public flow/module contracts now use explicit `Input`, `Config`, `Output`, `Summary`, or direct domain payloads where those names carry real
meaning. Empty contracts and summary-only output wrappers were removed rather than kept as placeholders.

Clean grep checks:

- Empty contract structs: no `struct *Input|Config|Output|Summary {}` matches under `source` or `test`.
- Single nested contract wrappers: no `struct *Output|Summary|Input|Config { *Output|Summary|Input|Config field; }` matches under `source` or `test`.
- Stale summary-only output wrappers: no hits for `InstantiationOutput`, `IdbConversionOutput`, `OptimizationOutput`, `ReportOutput`,
  `VisualizationOutput`, `SvgVisualizationOutput`, or `GdsVisualizationOutput`.
- Stale setup/read contracts: no hits for `SetupResult` or `ClockDataReadOutput`.
- Stale HTree/topology result compatibility names: no hits for `HTreeBuildResult`, `HTree::BuildResult`, `BuildResult` under HTree/topology
  contracts, `HTreeSynthesisOptions`, `HTreeSynthesisResult`, `htree_result`, or `source_trunk_result`.
- Singleton access: `_INST` and `getInst()` are present only in `src/operation/iCTS/api/CTSAPI.hh` and `src/operation/iCTS/api/CTSAPI.cc`.

## Fixed In This Convergence Pass

- Removed summary-only wrappers and empty config/output contracts from flow/report/visualization/instantiation/optimization setup paths.
- Converted setup/read status contracts to direct summaries:
  - `Setup::initializeRuntime(...) -> SetupSummary`
  - `Flow::readClockData() -> ClockDataReadSummary`
  - `ClockDataRead::read(...) -> bool`
- Split SDC clock tracing into payload and diagnostics:
  - `ClockTraceOutput`: resolved clock targets.
  - `ClockTraceSummary`: records and unowned-clock diagnostics.
  - `ClockTraceBuild`: pair returned only where both payload and diagnostics are needed.
- Split HTree and topology payload/summary responsibilities:
  - `HTreeOutput` owns topology, selected pattern/char, inserted object payload, and root pins/nets.
  - `HTreeSummary` owns only caller-relevant status: success/failure, selected depth, and boundary-relaxation status.
  - `HTreeDiagnostics` owns HTree-local report/test diagnostics while the HTree build remains in scope.
  - `Topology::Output` carries `HTree::Output` and topology payload.
  - `Topology::Summary` carries only topology-level status and aggregation metrics rather than nested HTree diagnostics.
  - `SourceTrunkOutput` and `SourceTrunkSummary` follow the same split.
- Converted remaining long public stage facades into named input contracts:
  - `Synthesis::run(const SynthesisInput&)`
  - `Topology::formClock(const ClockTopologyInput&)`
  - `Topology::build(const TopologyInput&, const TopologyConfig&)`
  - `Topology::buildSourceTrunk(const SourceTrunkInput&)`
  - `ClockDistribution::prepare(const ClockDistributionInput&)`
  - `TopologyDistanceReport::EmitClusterLeafDistanceTables(const ClusterLeafDistanceReportInput&)`
- Converged CTS reporter spelling:
  - Business signatures use CTS-level `SchemaWriter`.
  - `schema::` qualification remains for schema/report DSL types and helpers.
- Resolved final IWYU include-provider conflicts around `SchemaWriter` and characterization headers without adding suppressions:
  - headers that only need `SchemaWriter*`/`SchemaWriter&` use local forward alias declarations;
  - `AnalyticalSolution.hh` uses `logger/Schema.hh` for `SchemaWriter::StageScope` and a local CTS alias;
  - unsafe unqualified `characterization/Characterization.hh` includes were not added to `HTreeContracts.hh`.
- Removed HTree compatibility result shims and stale `Result` function names:
  - `TryBuildAnalyticalHTreeResult -> TryBuildAnalyticalHTree`
  - `ApplyRootDriverCompensationResult -> ApplyRootDriverCompensationSummary`
- Converted analytical characterization/fitting contracts:
  - `AnalyticalFitConfig`, `AnalyticalFitOutput`, `AnalyticalFitSummary`, `AnalyticalFitBuild`.
  - `AnalyticalCharacterizationConfig`, `AnalyticalCharacterizationOutput`, `AnalyticalCharacterizationSummary`,
    `AnalyticalCharacterizationBuild`.
- Converted routing/topology module contracts:
  - `LocalLegalization::Config` and `LocalLegalization::Output`.
  - `Router::LegalizationConfig` and `Router::LegalizationOutput`.
  - `ClusterOutput`.
- Converted HTree local helper contracts where the iCTS taxonomy is clearer:
  - `CharacterizationSummary`
  - `HTreeFanoutPruningConfig`
  - `CandidateCharRefFilterOutput` / `CandidateCharRefFilterSummary` / `CandidateCharRefFilterBuild`
  - `DepthSearchOutput` / `DepthSearchSummary` / `DepthSearchBuild`
  - `SinkLoadRegionLegalityInput` / `SinkLoadRegionLegalitySummary`
  - `SinkLoadRegionEntryFilterOutput` / `SinkLoadRegionEntryFilterSummary` / `SinkLoadRegionEntryFilterBuild`
  - `AnalyticalSolverConfig` / `AnalyticalSolverOutput` / `AnalyticalSolverSummary` / `AnalyticalSolverBuild`
  - `AnalyticalValidationSummary`
- Converted optimization user-facing knobs from broad options to policy:
  - `OptimizationOptions -> OptimizationPolicy`
  - `DefaultOptimizationOptions() -> DefaultOptimizationPolicy()`
  - `ValidateOptimizationOptions(...) -> ValidateOptimizationPolicy(...)`
  - `source/flow/optimization/options/` removed; `source/flow/optimization/policy/` added.
- Removed the redundant self-alias `using SynthesisTraceSummary = SynthesisTraceSummary;`.
- Tightened `icts_source_flow_synthesis_topology` CMake link visibility by making its `icts_source_flow_synthesis_trace` dependency private; topology
  public headers no longer expose trace headers.
- Polished remaining test terminology:
  - Analytical characterization tests use local `config` variables for `*Config` values.
  - HTree smoke test expects the current `CharBuilder Input/Config` log wording.

## Accepted Remaining Vocabulary

These remaining `Options`/`Result` names are intentionally not converted because they are not public CTS flow/module boundary contracts or because the
word is part of the actual domain object name.

- `schema::StageReportOptions`: logger/report utility configuration, not an algorithm contract.
- `RootedTreeLCA::BuildResult`: generic graph utility local build status.
- `FastSta*Result` and `FastStaBuilder::applyRuntimeOptions`: FastSTA timing-model vocabulary below the CTS flow/module boundary.
- `KMeans::Result`: nested generic algorithm return type inside a template implementation.
- `BalancePointResult` and `LineDistanceResult`: bound-skew-tree geometry/math helper outputs.
- `ResultExport` and `ResultExportPaths`: report artifact path resolver for final CTS result files; `Result` names the exported CTS result artifact,
  not an algorithm output wrapper.
- Local function names such as `BuildResultFanoutHistogram(...)`: implementation helper names over an already converted `ClusterOutput`.
- Test-only benchmark/validation types such as `TopologyMatrixRunResult`, `TopologyToleranceComparisonResult`, `TopologyValidationResult`,
  `Arm9ExperimentMatrixRunResult`, `CaseResult`, `ClusterRunResult`, `GroupFitResult`, and `HTreeFlowResult`.
- CMake macro local `set(options PUBLIC_RUNTIME REALTECH)` in `src/operation/iCTS/test/CMakeLists.txt`.
- Local HTree, topology pruning, optimization solver, and routing helper functions may still use direct multi-parameter signatures when they are
  narrow algorithm/math helpers rather than public flow/module facade contracts. These are intentionally deferred to
  `05-25-cts-structural-optimization-refactor` only if the post-convergence structural audit shows a readability or ownership issue.

## Output/Summary Responsibility Check

The remaining `*Build` contracts are not output wrappers. Each pairs a real payload with execution observations only where both are consumed:

- `HTreeBuild`: `HTreeOutput` plus `HTreeSummary` plus HTree-local `HTreeDiagnostics` for report/test observation.
- `Topology::Build`: `Topology::Output` plus `Topology::Summary`.
- `Topology::SourceTrunkBuild`: `SourceTrunkOutput` plus `SourceTrunkSummary`.
- `SourceTrunkSegment::Build`: segment payload plus stage summary.
- `EvaluationBuild`: reusable `EvaluationState` payload plus evaluation-ready summary.
- `AnalyticalFitBuild` and `AnalyticalCharacterizationBuild`: model/catalog payload plus fitting/build diagnostics.
- HTree helper `*Build` types pair helper payloads and helper summaries where pruning/search consumers require both.

No `Output` type owns only a `Summary`, and no `Summary` type carries design payload. Broad HTree diagnostics are not nested into
`Topology::Summary` or `SourceTrunkSummary`.

## Validation After Audit

- `git diff --check -- src/operation/iCTS .trellis/tasks/05-24-cts-contract-polish-convergence .trellis/tasks/05-25-cts-refactor-reflection .trellis/tasks/05-25-cts-structural-optimization-refactor`: passed.
- Contract greps for empty contracts, one-field contract wrappers, stale result/options names, `schema::SchemaWriter` business signatures, and singleton access: passed.
- Singleton grep for `_INST` / `getInst()`: only `src/operation/iCTS/api/CTSAPI.hh` and `src/operation/iCTS/api/CTSAPI.cc` remain.
- Targeted iCTS ninja build passed:
  `ninja -C build icts_source_flow icts_test_flow icts_test_flow_synthesis icts_test_flow_synthesis_htree icts_test_module_characterization icts_test_module_analytical_characterization icts_test_module_routing`
- `ctest --test-dir build -R '^icts_test_' --output-on-failure`: passed, 15/15 tests.
- `ninja -C build iEDA`: passed.
- Real `scripts/design/ics55_dev` iCTS flow passed and produced DEF, Verilog, CTS statistics, metrics, SVG, and GDS visualization outputs.
- Final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`: passed with 0 in-scope findings after final IWYU include-provider cleanup.
