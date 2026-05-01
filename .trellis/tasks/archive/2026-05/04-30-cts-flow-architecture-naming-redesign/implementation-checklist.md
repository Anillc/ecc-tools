# CTS flow architecture refactor implementation checklist

## Usage contract

This checklist is the execution ledger for the phased CTS flow refactor.

After each implementation phase:

* Update that phase status from `pending` to `completed`, or to `blocked` with a concrete blocker.
* Fill in the implementation notes with files changed and important design decisions.
* Mark each acceptance item as complete only when the implementation phase satisfies it by code inspection or local non-binary verification.
* Do not run CTS binaries or ECC checks during development phases. A later explicit user instruction supersedes the previous final-verification deferral and requires unified build/script/ECC verification after convergence.
* Do not advance to the next phase if the current phase leaves known architecture-boundary violations.

Status values:

* `pending`: not started.
* `in_progress`: implementation currently active.
* `completed`: phase implementation finished.
* `blocked`: cannot continue without resolving the blocker.

## Global acceptance gates

* [x] Target architecture exists under `src/operation/iCTS/source/flow` with the agreed top-level folders and primary entry files.
* [x] Each flow folder root contains only its primary entry pair plus build metadata.
* [x] Legacy DB-output wording is not exposed as a new architecture-level name.
* [x] Final-QoR, selection-only, generic execution-log, and repeated-topology labels are not introduced for synthesis trace or H-tree search.
* [x] H-tree internals are organized under `characterization`, `pattern_search`, and `construction`.
* [x] Stable CTS semantic data has a path toward `database/design/ClockTree`; report-only visualization data does not become database state.
* [x] Public struct exposure is reduced or explicitly justified by stable module-boundary contracts.
* [x] `Flow` is the only CTS flow lifecycle owner; no secondary lifecycle manager, transitional facade, alternate entry, macro, or build source remains.
* [x] No CTS binary or ECC check was run during development phases.
* [x] Non-ECC final verification is performed after all implementation phases finish.
* [x] Final unified verification per latest user instruction passes: `iEDA` build, full iCTS dev script, and ECC dev check for `src/operation/iCTS`.

## Phase 0: Architecture facade skeleton

Status: `completed`

### Development scope

Introduce the new architecture entry points without changing CTS behavior.

Target files/folders:

* `flow/Flow.hh`
* `flow/Flow.cc`
* `flow/setup/Setup.hh`
* `flow/setup/Setup.cc`
* `flow/synthesis/Synthesis.hh`
* `flow/synthesis/Synthesis.cc`
* `flow/instantiation/Instantiation.hh`
* `flow/instantiation/Instantiation.cc`
* `flow/evaluation/Evaluation.hh`
* `flow/evaluation/Evaluation.cc`
* `flow/report/Report.hh`
* `flow/report/Report.cc`
* related `CMakeLists.txt` entries

### Goal

Make the target flow architecture visible through readable primary entry files while old implementation classes remain available as delegates.

### Acceptance criteria

* [x] New primary entry files exist.
* [x] New primary entries compile by construction from existing includes and delegate patterns.
* [x] Existing public CTS API behavior is not intentionally changed.
* [x] No H-tree internals are moved in this phase.
* [x] Checklist is updated before moving to Phase 1.

### Implementation notes

Completed as a behavior-preserving facade skeleton.

Changed files:

* Added `src/operation/iCTS/source/flow/Flow.hh` and `Flow.cc` as the target top-level lifecycle entry; this transitional delegation was later removed in Phase 14.
* Added `src/operation/iCTS/source/flow/setup/Setup.hh` and `Setup.cc` as the target setup entry, temporarily delegating to the previous setup implementation.
* Added `src/operation/iCTS/source/flow/synthesis/Synthesis.hh` and `Synthesis.cc` as the target synthesis entry, delegating to `CTSClockTreeSynthesisStep`.
* Added `src/operation/iCTS/source/flow/instantiation/Instantiation.hh` and `Instantiation.cc` as the target instantiation entry, temporarily delegating to the previous external-DB projection step for this phase only.
* Added `src/operation/iCTS/source/flow/report/Report.hh` and `Report.cc` as the target report entry, delegating to the existing report step.
* Updated `src/operation/iCTS/source/flow/CMakeLists.txt` and `src/operation/iCTS/source/flow/synthesis/CMakeLists.txt` to register the new skeleton targets.

Key decisions:

* Phase 0 intentionally uses delegate facades instead of moving behavior, so CTS runtime behavior is unchanged.
* The old implementation classes remain temporary delegates and will be migrated in later phases.

## Phase 1: Flow and setup boundary

Status: `completed`

### Development scope

Move or wrap runtime setup/readiness behavior under `setup/Setup`, and make the top-level flow entry depend on setup explicitly.

Target current code:

* previous setup entry files
* former flow-owner implementation files
* data-load readiness pieces in `stage/CTSClockDataLoadStep.*` where appropriate

### Goal

Separate runtime readiness from synthesis decisions and reduce generic `stage` ownership.

### Acceptance criteria

* [x] Runtime config/work-dir/log/schema/Wrapper/STA setup is reachable through `Setup`.
* [x] Flow orchestration calls setup before synthesis through the new boundary.
* [x] Setup does not own topology construction, evaluation, or report output.
* [x] Existing setup behavior is preserved.
* [x] Checklist is updated before moving to Phase 2.

### Implementation notes

Completed as a setup ownership migration with a temporary delegate.

Changed files:

* Moved the actual setup implementation into `src/operation/iCTS/source/flow/setup/Setup.cc`.
* Updated `src/operation/iCTS/source/flow/setup/CMakeLists.txt` so `icts_source_flow_setup` owns the setup dependencies directly.
* Changed the previous setup implementation and its CMake target into a temporary delegate to `CTSSetup`.
* Updated `src/operation/iCTS/api/CTSAPI.cc` to call `FLOW_INST` and `CTSSetup` instead of the former flow/setup entries.
* Updated the then-active flow owner runtime setup emission to use `CTSSetup`.

Key decisions:

* `CTSSetup` owns only config loading, work/log/statistics/visualization directory setup, schema writer opening, Wrapper initialization, STA adapter initialization, and runtime setup diagnostics.
* Clock data import remains outside setup; topology construction, instantiation, evaluation, and report output are unchanged.
* The previous setup entry remained temporarily during this phase and was planned for Phase 8 cleanup.

## Phase 2: Synthesis facade and distribution boundary

Status: `completed`

### Development scope

Make `synthesis/Synthesis` the CTS algorithm-body entry and isolate clock distribution preparation.

Target files/folders:

* `flow/synthesis/Synthesis.*`
* `flow/synthesis/distribution/ClockDistribution.hh`
* `flow/synthesis/distribution/ClockDistribution.cc`

Target current code:

* `stage/ClockTreeSynthesisDriver.*`
* `stage/ClockSinkDomainBuilder.*`
* distribution-related parts of `stage/ClockTreeSynthesisTransaction.*`
* distribution-related parts of `synthesis/ClockSynthesis*`

### Goal

Move per-clock/domain synthesis coordination and clock source/root/sink organization into synthesis-owned modules, without exposing `sink_domain` or `per_clock` as top-level architecture.

### Acceptance criteria

* [x] `Synthesis` owns algorithm orchestration.
* [x] `ClockDistribution` owns clock source/root/sink organization, source-to-root preparation, sink grouping, and root anchors.
* [x] Distribution does not own H-tree pattern search, committed design mutation, iDB conversion, or report output.
* [x] Public distribution structs are minimized or justified as stable boundary data.
* [x] Checklist is updated before moving to Phase 3.

### Implementation notes

Completed as a behavior-preserving ownership migration with temporary transition names.

Changed files:

* Added `src/operation/iCTS/source/flow/synthesis/distribution/ClockDistribution.hh` and `ClockDistribution.cc`.
* Moved synthesis-loop ownership into `src/operation/iCTS/source/flow/synthesis/Synthesis.cc`; the then-active flow owner started calling `CTSSynthesis::run(...)`.
* Converted the previous synthesis step into a temporary delegate to `CTSSynthesis`.
* Converted `stage/ClockSinkDomainBuilder.*` into a delegate to `ClockDistribution`.
* Updated `stage/ClockTreeSynthesisDriver.cc` and `stage/ClockTreeSynthesisTransaction.*` to use `ClockDistributionContext`.
* Updated the then-active generic stage CMake file and `src/operation/iCTS/source/flow/synthesis/CMakeLists.txt` so synthesis orchestration compile units are built by the synthesis target.

Key decisions:

* `ClockDistribution` now owns sink partitioning, root-buffer anchor creation, downstream-net preparation, and domain context assembly.
* H-tree search, temporary object construction, committed design mutation, iDB projection, evaluation, and report output remain outside distribution.
* The public distribution records are the existing root-buffer spec, partition, and context shapes under new distribution names. The old domain-shaped records remained only during phased migration.
* `ClockTreeSynthesisDriver`, `ClockTreeSynthesisTransaction`, and `ClockTreeSynthesisStatusTable` remain path-compatible under `stage/` for now but are compiled by the synthesis target; they are scheduled for later trace/instantiation cleanup.

## Phase 3: H-tree decomposition

Status: `completed`

### Development scope

Reorganize H-tree implementation into characterization, pattern search, and construction responsibilities.

Target files/folders:

* `flow/synthesis/topology/Topology.*`
* `flow/synthesis/topology/htree/HTree.*`
* `flow/synthesis/topology/htree/characterization/`
* `flow/synthesis/topology/htree/pattern_search/`
* `flow/synthesis/topology/htree/construction/`

Target current code:

* `htree/CharacterizationLibrary.*`
* `htree/HTreeCharacterization*`
* `htree/HTreePatternRegistry.hh`
* `htree/HTreeSegmentCandidateFrontier.cc`
* `htree/HTreeTopologyDepthSearch.cc`
* `htree/HTreeTopologyDepthEvaluation.cc`
* `htree/HTreeTopologyAssembly.cc`
* `htree/HTreeSinkLoadProfile*`
* `htree/HTreeClockTreeObjectBuilder.cc`
* `htree/HTreeClockTopologyBuildContext.hh`

### Goal

Make H-tree algorithm responsibilities readable and prevent pattern-search internals from leaking through synthesis-level public headers.

### Acceptance criteria

* [x] H-tree root exposes `HTree.hh/.cc` as the primary entry.
* [x] Characterization files live under `characterization`.
* [x] Pattern registry, frontier, depth search/evaluation, topology assembly, and sink-load legality live under `pattern_search`.
* [x] Temporary object builders live under `construction`.
* [x] No new broad candidate folder, selection-only topology folder, or repeated-topology folder is introduced.
* [x] Checklist is updated before moving to Phase 4.

### Implementation notes

Completed as a file movement and include-path migration.

Changed files:

* Moved the old peer H-tree implementation under `src/operation/iCTS/source/flow/synthesis/topology/htree`.
* Renamed the public H-tree entry files from `HTreeBuilder.hh/.cc` to `HTree.hh/.cc`; the class name stayed unchanged during this behavior-preserving phase.
* Added `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh` and `Topology.cc` as the topology-family facade.
* Moved characterization files to `synthesis/topology/htree/characterization/`.
* Moved pattern registry, frontier, depth search/evaluation, topology assembly, sink-load profile, and level-plan/search support files to `synthesis/topology/htree/pattern_search/`.
* Moved temporary object construction files and source-to-root segment builder files to `synthesis/topology/htree/construction/`.
* Updated include paths and `CMakeLists.txt` entries for the new H-tree locations.

Key decisions:

* The old peer H-tree directory was removed.
* No broad candidate folder, selection-only topology folder, or repeated-topology folder was introduced.
* `HTreeSynthesisSummary.cc` and `HTreeLogging.cc` are temporarily under `pattern_search` to keep the H-tree root clean; Phase 4 will move synthesis status/summary ownership toward `synthesis/trace/SynthesisTrace`.

## Phase 4: Synthesis trace and public struct convergence

Status: `completed`

### Development scope

Introduce `trace/SynthesisTrace` and reduce scattered public synthesis/H-tree status structs.

Target files/folders:

* `flow/synthesis/trace/SynthesisTrace.hh`
* `flow/synthesis/trace/SynthesisTrace.cc`

Target current code:

* `synthesis/ClockTreeSynthesisMetrics.*`
* `synthesis/ClockSynthesisReporter.*`
* `htree/HTreeSynthesisSummary.cc`
* `htree/HTreeLogging.cc`
* summary/status fields inside `ClockSynthesis::BuildResult`
* summary/status fields inside `HTreeBuilder::BuildResult`

### Goal

Separate execution trace from temporary object ownership and final QoR evaluation.

### Acceptance criteria

* [x] `SynthesisTrace` records synthesis status, selected depth/pattern summary, fallback/failure reason, and inserted-count summaries.
* [x] `SynthesisTrace` does not own temporary `Inst/Pin/Net` objects.
* [x] `SynthesisTrace` does not compute final skew, latency, area, or routed wirelength.
* [x] H-tree candidate/frontier/search structs are private to pattern search unless explicitly justified.
* [x] Checklist is updated before moving to Phase 5.

### Implementation notes

Completed as a trace boundary introduction.

Changed files:

* Added `src/operation/iCTS/source/flow/synthesis/trace/SynthesisTrace.hh` and `SynthesisTrace.cc`.
* Changed the previous run-summary header into a temporary include for `synthesis/trace/SynthesisTrace.hh`.
* Updated `src/operation/iCTS/source/flow/Flow.hh`, `src/operation/iCTS/source/flow/synthesis/Synthesis.hh`, and synthesis CMake wiring to use/build the trace boundary.
* Updated `src/operation/iCTS/source/flow/synthesis/Synthesis.cc` to populate `SynthesisTraceSummary::domain_status` from the existing synthesis status rows.

Key decisions:

* `SynthesisTraceSummary` is the architecture-owned synthesis trace record. The previous run-summary name was temporary during the phased migration.
* Trace data records per-clock/per-domain status, selected H-tree depth/level summaries, inserted H-tree object counts, and failure/detail text.
* Trace does not own temporary `Inst`, `Pin`, or `Net` objects and does not compute final QoR metrics such as skew, latency, area, or routed wirelength.
* H-tree candidate/frontier/search headers remain under `synthesis/topology/htree/pattern_search`; broader public-struct reduction was deferred to final convergence because it required API-level changes beyond a safe move-only refactor.

## Phase 5: Instantiation split

Status: `completed`

### Development scope

Move materialization into the instantiation layer and split design conversion from iDB conversion.

Target files/folders:

* `flow/instantiation/Instantiation.*`
* `flow/instantiation/design/DesignConversion.hh`
* `flow/instantiation/design/DesignConversion.cc`
* `flow/instantiation/idb/IdbConversion.hh`
* `flow/instantiation/idb/IdbConversion.cc`

Target current code:

* previous external-DB projection step files
* previous design-net edit helper files
* commit/materialization parts of `stage/ClockTreeSynthesisTransaction.*`

### Goal

Expose materialization as `instantiation`, keep design object conversion and iDB projection separate, and remove legacy DB-output architecture naming.

### Acceptance criteria

* [x] `Instantiation` consumes synthesis products and coordinates design/iDB conversion.
* [x] `DesignConversion` owns committed iCTS `Design` object insertion, clock membership updates, and rollback cleanup.
* [x] `IdbConversion` owns iDB projection through `Wrapper`.
* [x] New architecture-level names do not expose legacy DB-output wording.
* [x] Checklist is updated before moving to Phase 6.

### Implementation notes

Completed as a materialization naming and ownership migration.

Changed files:

* Moved the previous design-net edit helper implementation to `src/operation/iCTS/source/flow/instantiation/design/DesignConversion.hh/.cc`.
* Added a temporary forwarding header for the previous design-net edit helper; the old peer net-editing target was temporary and is removed in the final architecture.
* Added `src/operation/iCTS/source/flow/instantiation/idb/IdbConversion.hh/.cc` for iDB projection through `Wrapper`.
* Updated `src/operation/iCTS/source/flow/instantiation/Instantiation.hh/.cc` to return `CTSInstantiationResult`.
* Updated the then-active flow owner to use `instantiate()` and `_instantiation_result` instead of a legacy DB-output method/result.
* Updated synthesis transaction and distribution code to call `DesignConversion` directly.
* Kept the previous external-DB projection step only as a temporary delegate to `CTSInstantiation`.

Key decisions:

* `DesignConversion` owns the current committed-design mutation surface, including read-data transition methods, sink partition helpers, root/downstream object creation, reconnect/rollback helpers, and inserted-object commit.
* `IdbConversion` owns the external iDB projection by calling `WRAPPER_INST.writeClocks(...)`; raw iDB access remains inside `Wrapper`.
* Existing design-commit timing is preserved: per-domain/source-to-root temporary objects are still committed at the same synthesis transaction points.
* Remaining legacy materialization names are temporary old-stage/view API delegates and are planned for Phase 8 cleanup.

## Phase 6: ClockTree semantic database boundary

Status: `completed`

### Development scope

Introduce or migrate stable clock-tree semantic data into `database/design/ClockTree`.

Target files/folders:

* `source/database/design/ClockTree.hh`
* `source/database/design/ClockTree.cc`

Target current code:

* stable role/domain/topology-level portions of `clock_tree_view/ClockTreeView.*`
* stable synthesis-to-view role data that should become design semantics

### Goal

Separate stable CTS design semantics from report-only visualization/view records.

### Acceptance criteria

* [x] `ClockTree` owns stable roles, relationships, domains, and topology level/depth facts.
* [x] `ClockTree` does not own H-tree candidates/frontiers/search state.
* [x] `ClockTree` does not own temporary synthesis object lifetimes.
* [x] `ClockTree` does not own report/visualization formatting data.
* [x] Checklist is updated before moving to Phase 7.

### Implementation notes

Completed as a stable database model introduction.

Changed files:

* Added `src/operation/iCTS/source/database/design/ClockTree.hh` and `ClockTree.cc`.
* Updated `src/operation/iCTS/source/database/design/CMakeLists.txt` to build `ClockTree.cc`.

Key decisions:

* `ClockTree` is a stable semantic model over `Design`-owned objects. It stores borrowed `Clock`, `Inst`, `Pin`, and `Net` references plus semantic roles/domains/topology metadata.
* It owns coarse enums for domain kind, inst role, net role, topology kind, and lifecycle state.
* It does not own temporary synthesis `unique_ptr` objects, H-tree candidate/frontier/search records, timing/QoR/evaluation metrics, file paths, SVG/GDS formatting, report rows, or visualization layer policy.
* This phase introduces the target boundary without changing current synthesis/report producers; migrating `ClockTreeView` consumers to `ClockTree` remains a later behavior-preserving adapter step.

## Phase 7: Evaluation and report ownership cleanup

Status: `completed`

### Development scope

Separate final metric computation from report output generation.

Target files/folders:

* `flow/evaluation/Evaluation.*`
* `flow/report/Report.*`
* `flow/report/summary/Summary.*`
* `flow/report/statistics/StatisticsReport.*`
* `flow/report/export/ResultExport.*`
* `flow/report/visualization/ClockTreeVisualization.*`
* `flow/report/visualization/svg/`
* `flow/report/visualization/gds/`

Target current code:

* `evaluation/ClockTreeEvaluator.*`
* `evaluation/CTSStatisticsWriter.*`
* previous report step files
* `clock_tree_view/ClockTreeVisualizationModel.*`
* `visualization/ClockTreeSvgVisualization.*`
* `visualization/ClockTreeGdsVisualization.*`
* `visualization/ClockTreeGdsWriter.*`
* `visualization/ClockTreeVisualizationLayerPolicy.*`

### Goal

Keep evaluation as readonly metric computation and make report output responsibilities explicit.

### Acceptance criteria

* [x] `Evaluation` computes or exposes final metric summaries without owning report files long-term.
* [x] `Summary` owns final status/runtime/status summary output.
* [x] `StatisticsReport` owns `.rpt` and statistics table output.
* [x] `ResultExport` owns save-dir/report-root/manifest/export status.
* [x] `ClockTreeVisualization` owns SVG/GDS output and visual model adaptation.
* [x] Report does not mutate design/iDB or depend on synthesis internals beyond readonly trace/product summaries.
* [x] Checklist is updated before moving to Phase 8.

### Implementation notes

Completed as a report/evaluation ownership migration with temporary delegates.

Changed files:

* Added and wired `src/operation/iCTS/source/flow/evaluation/Evaluation.hh/.cc` as the evaluation entry. `CTSEvaluation::run(...)` now owns the evaluation runtime/stage/schema wrapper and delegates metric computation to the existing `ClockTreeEvaluator`.
* Added and wired report children:
  * `src/operation/iCTS/source/flow/report/summary/Summary.hh/.cc`
  * `src/operation/iCTS/source/flow/report/statistics/StatisticsReport.hh/.cc`
  * `src/operation/iCTS/source/flow/report/export/ResultExport.hh/.cc`
  * `src/operation/iCTS/source/flow/report/visualization/ClockTreeVisualization.hh/.cc`
* Updated `src/operation/iCTS/source/flow/report/Report.hh/.cc` so `CTSReport` owns report orchestration directly: path resolution, report-mode summary, optional evaluation rebuild, statistics export, SVG/GDS visualization, runtime metric, and report stage status.
* Updated the then-active flow owner so evaluation/report calls go through `CTSEvaluation` and `CTSReport` instead of report/evaluation stage wrappers.
* Converted the previous evaluation/report step files into temporary delegates to the new evaluation/report entries.
* Updated `src/operation/iCTS/source/flow/evaluation/CMakeLists.txt`, `report/CMakeLists.txt`, the then-active generic stage CMake file, and top-level `flow/CMakeLists.txt` so report no longer depends on the generic stage boundary.

Key decisions:

* Existing evaluation metric computation remains in `ClockTreeEvaluator` to preserve behavior; `CTSEvaluation` is the architectural entry and stage wrapper.
* Existing report-specific statistics writing is now reached through `report/statistics/StatisticsReport`. The legacy immediate statistics write in `ClockTreeEvaluator::evaluate(...)` remains as transitional behavior for this phase and is removed in later convergence.
* `report/export/ResultExport` owns report-root, visualization-dir, and statistics-dir resolution using the existing config/save-dir policy.
* `report/visualization/ClockTreeVisualization` wraps the existing SVG/GDS emitters as a report child. Moving low-level SVG/GDS writers out of the legacy top-level `visualization` target is deferred to Phase 8 convergence because it requires broader include and target churn.
* No CTS binary, ECC check, or final unified verification was run during this phase.

## Phase 8: Compatibility cleanup and final convergence

Status: `completed`

### Development scope

Remove obsolete architecture-level names, transitional wrappers, and stale include paths after all target modules are in place.

Target current names:

* `stage`
* legacy DB-output architecture naming
* `run_setup`
* `clock_tree_view` where stable semantics moved to `ClockTree`
* old top-level `visualization` target after report visualization owns outputs
* broad public `BuildResult`/candidate/header exposure where replacement boundaries exist

### Goal

Converge the codebase on the final architecture and remove transitional debt.

### Acceptance criteria

* [x] Obsolete architecture-level folders/classes are removed before final convergence.
* [x] Stale includes are migrated to final paths.
* [x] Public headers expose primary facades and stable boundary records only.
* [x] Global acceptance gates are updated.
* [x] Final unified verification is ready to run after this phase.

### Implementation notes

Completed as final architecture convergence by source inspection only.

Changed files:

* Updated `src/operation/iCTS/source/flow/CMakeLists.txt` to remove the old top-level visualization subdirectory/target and its link from the flow aggregate.
* Updated the then-active generic stage CMake file to drop the stale visualization dependency.
* Updated `src/operation/iCTS/source/flow/report/CMakeLists.txt` so `icts_source_flow_report` directly owns report visualization sources and links the dependencies those sources need.
* Moved the SVG report writer to `src/operation/iCTS/source/flow/report/visualization/svg/ClockTreeSvgVisualization.hh/.cc`.
* Moved the GDS report writer, GDS stream writer, and visualization layer policy to `src/operation/iCTS/source/flow/report/visualization/gds/`.
* Updated report visualization includes from the old top-level `visualization/*` paths to `report/visualization/svg/*` and `report/visualization/gds/*`.
* Removed the old top-level visualization CMake file; that directory is no longer a source architecture folder.

Retained boundaries after this phase:

* `stage/CTSClockDataLoadStep`, `stage/CTSClockTreeSynthesisStep`, and `stage/ClockSinkDomainBuilder` were removed by later convergence work.
* The synthesis driver, transaction, and status-table classes moved under synthesis-owned paths, while their compile ownership is under the synthesis target.
* `clock_tree_view` moved under `report/visualization/model` as a diagnostic/report adapter while stable CTS semantic database state is introduced through `database/design/ClockTree`.
* Existing synthesis and H-tree `BuildResult` records remain behavior-preserving module boundary records for temporary object transfer. Candidate/frontier/search internals are kept under `synthesis/topology/htree/pattern_search` and are not exposed through the high-level `Flow`, `Synthesis`, `Instantiation`, `Evaluation`, or `Report` entries.

Static inspection performed:

* Searched active iCTS source/test paths for forbidden or obsolete architecture names covering legacy DB-output wording, old setup/net-edit/stage step classes, final-QoR trace labels, selection-only topology labels, old visualization targets, and old peer flow paths.
* Searched active iCTS source/test paths for stale old include paths covering top-level visualization, old stage wrappers, old peer net-editing/setup folders, and old peer H-tree source paths.
* Inspected the resulting flow directory tree to confirm the old top-level visualization directory is gone and report visual output now lives under `report/visualization/svg` and `report/visualization/gds`.

Final unified verification:

* Deferred by user request. No CTS binary, ECC check, or final unified verification command was run during Phase 8.

## Phase 9: Synthesis root convergence

Status: `completed`

### Development scope

Converge `src/operation/iCTS/source/flow/synthesis/` so the root contains only the primary entry pair and build metadata.

Target current files:

* `ClockSinkTreeSynthesizer.*`
* `ClockSourceRootSynthesizer.*`
* `ClockSynthesis.*`
* `ClockSynthesisHtreeOptions.*`
* `ClockSynthesisNetEditor.*`
* `ClockSynthesisReporter.*`
* `ClockSynthesisSinkClustering.*`
* `ClockTreeSynthesisMetrics.*`
* `ClockTreeViewAdapter.*`
* synthesis-owned `stage/ClockTreeSynthesisDriver.*`
* synthesis-owned `stage/ClockTreeSynthesisStatusTable.*`
* synthesis-owned `stage/ClockTreeSynthesisTransaction.*`

### Goal

Keep behavior unchanged while moving synthesis implementation helpers into the existing recommended architecture subtrees: `distribution/`, `topology/`, and `trace/`. Do not create dumping-ground folders such as `utils`, `helper`, `internal`, `context`, or `types`.

### Acceptance criteria

* [x] `synthesis/` root contains only `Synthesis.hh`, `Synthesis.cc`, and `CMakeLists.txt`.
* [x] Sink/source/root/topology construction helpers are placed under clear `distribution/` or `topology/htree/construction/` boundaries.
* [x] H-tree option adaptation and view adaptation are placed under topology/report-adapter boundaries with explicit business ownership.
* [x] Synthesis metrics/status/reporting helpers are placed under `trace/`.
* [x] Include paths, CMake sources, and affected tests are updated.
* [x] Checklist is updated before moving to Phase 10.

### Implementation notes

Completed as a source-layout convergence with behavior-preserving class names.

Changed files:

* Moved root synthesis helper files into recommended architecture subtrees:
  * `distribution/ClockSynthesisSinkClustering.*`
  * `topology/ClockSynthesis.*`
  * `topology/ClockSinkTreeSynthesizer.*`
  * `topology/ClockSourceRootSynthesizer.*`
  * `topology/htree/characterization/ClockSynthesisHtreeOptions.*`
  * `topology/htree/construction/ClockSynthesisNetEditor.*`
  * `trace/ClockSynthesisReporter.*`
  * `trace/ClockTreeSynthesisMetrics.*`
  * `trace/ClockTreeViewAdapter.*`
* Moved synthesis-owned stage implementation files out of `stage/`:
  * `distribution/ClockTreeSynthesisDriver.*`
  * `topology/ClockTreeSynthesisTransaction.*`
  * `trace/ClockTreeSynthesisStatusTable.*`
* Updated include paths across iCTS source/tests.
* Updated `src/operation/iCTS/source/flow/synthesis/CMakeLists.txt`.

Key decisions:

* `ClockTreeViewAdapter` remains under `synthesis/trace` for now because synthesis still produces the diagnostic trace/view side channel. Phase 11 will move the actual clock-tree view model into a report visualization adapter boundary.
* H-tree option adaptation lives under the H-tree characterization boundary because it resolves H-tree build/characterization options rather than owning high-level flow policy.
* `ClockSynthesisNetEditor` lives under H-tree construction because it creates and guards temporary synthesis products before final instantiation.

## Phase 10: Evaluation/report boundary convergence

Status: `completed`

### Development scope

Converge `src/operation/iCTS/source/flow/evaluation/` root and separate metric computation from statistics report writing.

Target current files:

* `evaluation/ClockTreeEvaluator.*`
* `evaluation/CTSStatisticsWriter.*`
* `report/statistics/StatisticsReport.*`

### Goal

Keep `evaluation` as readonly metric computation and move statistics file/schema output into `report/statistics`.

### Acceptance criteria

* [x] `evaluation/` root contains only `Evaluation.hh`, `Evaluation.cc`, and `CMakeLists.txt`.
* [x] The evaluation engine lives under a clear metric-computation subfolder if it remains a separate class.
* [x] Statistics file/schema writing lives under `report/statistics`.
* [x] `evaluation` does not depend on `report`.
* [x] Report consumes readonly evaluation state/statistics and owns report output paths.
* [x] Include paths, CMake sources, and affected tests are updated.
* [x] Checklist is updated before moving to Phase 11.

### Implementation notes

Completed as an ownership split between readonly metric computation and report output.

Changed files:

* Moved `ClockTreeEvaluator.hh/.cc` to `src/operation/iCTS/source/flow/evaluation/metrics/`.
* Split `CTSStatistics` data into `src/operation/iCTS/source/flow/evaluation/metrics/CTSStatistics.hh`.
* Moved `CTSStatisticsWriter.cc` to `src/operation/iCTS/source/flow/report/statistics/` and added `CTSStatisticsWriter.hh` there.
* Updated `StatisticsReport` to own statistics writing directly from readonly evaluation state.
* Removed `ClockTreeEvaluator::writeStatistics(...)` and the immediate statistics write from evaluation.
* Updated include paths and `evaluation`/`report` CMake sources.

Key decisions:

* Evaluation now computes and stores `ClockTreeEvaluationState`/`CTSStatistics`; it does not write report files.
* `report/statistics` owns `.rpt` and schema table output through `StatisticsReport` and `CTSStatisticsWriter`.
* `evaluation` has no dependency on `report`; report depends on evaluation data as the final output layer.

## Phase 11: Stage and clock-tree-view final positioning

Status: `completed`

### Development scope

Remove or reposition `stage/` and `clock_tree_view/` so they no longer read as peer business architecture folders beside `setup`, `synthesis`, `instantiation`, `evaluation`, and `report`.

Target current files:

* `stage/CTSClockDataLoadStep.*`
* `stage/CTSClockTreeSynthesisStep.*`
* remaining synthesis files still under `stage/`
* `clock_tree_view/ClockTreeView.*`
* `clock_tree_view/ClockTreeViewBuilder.*`
* `clock_tree_view/ClockTreeViewSynthesisInput.hh`
* `clock_tree_view/ClockTreeVisualizationModel.*`

### Goal

Delete unnecessary transition wrappers and move required diagnostic/visual adapter code into explicit recommended architecture boundaries, preferably `report/visualization`.

### Acceptance criteria

* [x] `stage/` is removed or reduced to no active business source directory.
* [x] Data-load and synthesis wrappers are either deleted or moved behind final architecture entries.
* [x] `clock_tree_view` is moved or renamed as a report visualization adapter boundary, not a peer flow layer.
* [x] Any unavoidable adapter surface is documented as final adapter surface, not a pending cleanup.
* [x] Include paths, CMake sources, and affected tests are updated.
* [x] Checklist is updated before moving to Phase 12.

### Implementation notes

Completed as final positioning of remaining top-level transition directories.

Changed files:

* Removed the old generic stage source directory.
* Inlined the old `CTSClockDataLoadStep::run()` behavior into the flow read-data lifecycle action without a misleading `stage` directory.
* Removed synthesis wrappers in `stage`; the flow owner already calls `CTSSynthesis` directly and tests now use synthesis/distribution/trace/topology includes.
* Moved the old peer diagnostic view model files to `src/operation/iCTS/source/flow/report/visualization/model/`.
* Added `icts_source_flow_report_visualization_model` as the explicit diagnostic visualization model/adapter target.
* Updated include paths and CMake links in flow, synthesis, instantiation, report, and tests.

Final retained boundary rationale:

* `ClockTreeView` remains a behavior-preserving diagnostic model name, but its physical boundary is now `report/visualization/model`; it is no longer a top-level flow architecture directory.
* Synthesis still populates this diagnostic model as a trace/report side channel until a deeper `database/design/ClockTree` producer migration is implemented. The boundary is explicit through the `icts_source_flow_report_visualization_model` target, not a peer flow folder.
* No old generic stage source directory remains.

## Phase 12: Public struct contract convergence

Status: `completed`

### Development scope

Reduce or justify remaining public header structs after directory convergence.

Focus areas:

* H-tree pattern-search candidate/frontier/search structs.
* Synthesis construction transfer records and `BuildResult` exposure.
* Report visualization adapter structs.
* Evaluation/report statistics boundary records.
* `database/design/ClockTree` stable semantic records.

### Goal

Keep public structs only when they are stable boundary records or explicit submodule-local contracts. Avoid exposing high-level flow/report/evaluation code to detailed H-tree search or construction internals.

### Acceptance criteria

* [x] Candidate/frontier/search structs are confined to `pattern_search` boundaries or explicitly justified there.
* [x] Construction object-transfer structs remain under construction/synthesis-to-instantiation boundaries and are not exposed to high-level flow/report/evaluation.
* [x] Public evaluation/report/visualization structs are narrow output or adapter contracts.
* [x] Remaining `BuildResult`-style records have a documented temporary behavior-preserving reason or are narrowed.
* [x] `database/design/ClockTree` remains free of algorithm candidates, temporary ownership, report formatting, and iDB commit logic.
* [x] Checklist records the remaining public structs' rationale.
* [x] Checklist is updated before moving to Phase 13.

### Implementation notes

Completed as public-contract classification after directory convergence.

Remaining public struct rationale:

* `synthesis/topology/htree/pattern_search/*`: candidate/frontier/depth/search/profile structs remain public only inside the H-tree pattern-search submodule. They are not included by `Flow`, `Synthesis`, `Instantiation`, `Evaluation`, or `Report` facades.
* `synthesis/topology/htree/construction/*`: temporary construction records such as `SourceToRootSegmentBuilder::BuildResult`, `HTreeClockTopologyBuildContext`, and `BufferCreation` remain construction-local transfer contracts for temporary `Inst`/`Pin`/`Net` products before instantiation.
* `synthesis/topology/ClockSynthesis.hh`: `BuildResult` and `SourceToRootBuildResult` remain behavior-preserving synthesis-to-construction/instantiation transfer records. They are now confined below `synthesis/topology` and consumed by `trace`/transaction helpers, not by high-level flow/report/evaluation interfaces.
* `synthesis/trace/SynthesisTrace.hh`: `SynthesisTraceSummary` and `SynthesisTraceStatusRecord` are stable synthesis trace boundary records. They contain status/count/selected-topology summaries and do not own temporary objects or final QoR metrics.
* `evaluation/metrics/*`: `ClockTreeSummary`, `ClockTreeEvaluationState`, and `CTSStatistics` are readonly metric-result contracts consumed by flow/report/API. Report file writing is no longer part of evaluation.
* `report/statistics/*`, `report/export/*`, and `report/visualization/*`: remaining structs are output/report adapter contracts, file-format records, or generated-output status records scoped to report responsibilities.
* `report/visualization/model/*`: `ClockTreeView*` records are retained as a diagnostic visualization adapter model. They are no longer a peer flow architecture directory and are explicitly scoped under report visualization.
* `database/design/ClockTree.hh`: public records are limited to stable semantic records (`DomainRecord`, `InstRecord`, `NetRecord`, `TopologyRecord`) over design-owned objects. The model has no H-tree candidate/search state, no temporary ownership bundles, no report formatting fields, and no iDB commit methods.

No additional public structs were promoted during this phase.

## Phase 13: Non-ECC final verification record

Status: `completed`

### Development scope

Run only non-ECC, non-CTS-binary verification and record the result.

Allowed examples:

* `git diff --check`
* CMake configure or target build without running `./bin/icts_*`
* static searches for forbidden old architecture paths/names
* source tree shape checks

Forbidden in this phase by user instruction:

* `.trellis/ecc_dev_tools/check.py`
* IWYU/ECC checker entry points
* CTS binaries or CTS test executable runs

### Goal

Record a truthful final verification result for this convergence pass without claiming ECC coverage.

### Acceptance criteria

* [x] Non-ECC final verification completed and recorded.
* [x] No `.trellis/ecc_dev_tools/check.py`, IWYU/ECC, CTS binary, or CTS test executable was run.
* [x] Static searches confirm active source/test paths no longer include old top-level architecture paths for peer H-tree, setup, net-editing, or visualization folders.
* [x] Static searches confirm legacy DB-output wording is not used as an architecture-level name.
* [x] Checklist global gates state: "non-ECC final verification completed; ECC deferred by user instruction."

### Implementation notes

Completed.

Verification run:

* `git diff --check` passed.
* Static search passed for old top-level architecture paths in active iCTS source/test files covering peer H-tree, setup, net-editing, visualization, generic stage, and diagnostic-view folders.
* Static search passed for obsolete architecture names covering legacy DB-output wording, old setup/net-edit/stage step classes, final-QoR trace labels, selection-only topology labels, and repeated-topology labels.
* Source tree shape checks passed:
  * `synthesis/` root contains only `CMakeLists.txt`, `Synthesis.cc`, and `Synthesis.hh`.
  * `evaluation/` root contains only `CMakeLists.txt`, `Evaluation.cc`, and `Evaluation.hh`.
  * flow top-level directories are `evaluation`, `instantiation`, `report`, `setup`, and `synthesis`.
  * H-tree tests now live under `test/flow/synthesis/topology/htree`.
  * H-tree source CMake is nested under `synthesis/topology/htree` and the source target is `icts_source_flow_synthesis_topology_htree`; it is no longer exposed as the old peer H-tree boundary.
* Reviewer convergence fixes were applied during final quality check:
  * Updated source CMake wiring so `synthesis/CMakeLists.txt` owns `add_subdirectory(topology/htree)`.
  * Updated backend directory/database specs so they describe `setup -> synthesis -> instantiation -> evaluation -> report` and no longer document the old top-level flow folders as current architecture.
  * Updated `prd.md` with explicit Phase 9-13 convergence scope.
* `cmake --build build --target icts_source_flow -j2` passed. CMake reconfigured the existing `build/` directory and built the iCTS flow target; no CTS binary or test executable was run.

Final gate statement:

* non-ECC final verification completed; ECC deferred by user instruction.

## Phase 14: Flow entry convergence

Status: `completed`

### Development scope

Remove the remaining separate flow lifecycle owner and make `Flow` the only owner of CTS flow state, orchestration, summary output, and reset behavior.

Target current files:

* `source/flow/Flow.hh`
* `source/flow/Flow.cc`
* former flow-owner implementation files
* `source/flow/CMakeLists.txt`
* `test/flow/FlowTest.cc`
* `test/flow/CMakeLists.txt`

### Goal

Converge the root flow entry completely: `FLOW_INST` is the only active flow lifecycle singleton entry, and `Flow` directly owns lifecycle state instead of forwarding to another class.

### Acceptance criteria

* [x] `Flow` privately owns `_run_summary`, `_clock_tree_view`, `_evaluation_state`, `_instantiation_result`, `_runtime_setup_emitted`, and `_evaluation_ready`.
* [x] `Flow` directly implements `runCTS`, `readData`, `run`, `instantiate`, `evaluate`, `report`, `outputRuntimeSetup`, `emitKeyResults`, `outputSummary`, `outputRunSummary`, and `reset`.
* [x] The former manager header/source files are deleted.
* [x] The former manager singleton macro is deleted.
* [x] `source/flow/CMakeLists.txt` builds only `Flow.cc` for the flow-root target.
* [x] Flow-root tests include/use `flow/Flow.hh` and `FLOW_INST`.
* [x] Flow-root test file/target names use `Flow`, not the old manager name.
* [x] No transitional facade, alternate entry, or forwarding layer is retained for the deleted lifecycle owner.
* [x] Source tree shape confirms the flow root contains only `Flow.hh`, `Flow.cc`, `CMakeLists.txt`, `setup`, `synthesis`, `instantiation`, `evaluation`, and `report`.

### Implementation notes

Completed as direct flow-entry convergence with no behavior-intent change.

Changed files:

* Moved the lifecycle state and orchestration implementation into `src/operation/iCTS/source/flow/Flow.hh` and `Flow.cc`.
* Removed the former manager header and source.
* Updated `src/operation/iCTS/source/flow/CMakeLists.txt` so `icts_source_flow` compiles `Flow.cc` only.
* Renamed the flow-root test file to `src/operation/iCTS/test/flow/FlowTest.cc`.
* Updated the flow-root test fixture and assertions to use `FLOW_INST`.
* Renamed the flow-root test CMake target to `icts_test_flow`.

Verification recorded for this phase:

* Static source/test search found no remaining deleted lifecycle-owner class name, macro, old test name, or old test target name in active iCTS code.
* Static task/spec search found no deleted lifecycle-owner class name, macro, old test name, or old test target name in active non-research task/spec docs.
* Flow-root tree shape contains only the current recommended root files/folders.
* `git diff --check` passed.
* `cmake --build build --target icts_source_flow -j2` passed. CMake reconfigured the existing `build/` directory and built the iCTS flow target; no CTS binary, CTS script, ECC check, or iCTS test executable was run.
* Post-ECC cleanup: the main-thread first ECC run reported in-scope format findings and CMake link-visibility warnings; follow-up formatting and CMake visibility fixes were applied, with ECC rerun deferred to the main thread.

## Phase 15: Final unified verification

Status: `completed`

### Development scope

Run the final verification requested after architecture convergence. This phase is verification-only except for fixing concrete findings reported by build/script/ECC checks.

Required commands:

* `cmake --build build --target iEDA -j2`
* from `scripts/design/ics55_dev`: `./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
* `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`

### Goal

Confirm the converged CTS flow architecture builds, runs the full iCTS dev script, and passes ECC dev checks without compatibility layers.

### Acceptance criteria

* [x] `iEDA` build passes.
* [x] Full iCTS dev script exits successfully.
* [x] Full iCTS dev script reports `iCTS run successfully.`
* [x] ECC dev check passes after any required in-scope fixes.
* [x] Checklist and PRD final verification status are updated after ECC completion.

### Implementation notes

Completed.

Completed verification:

* `cmake --build build --target iEDA -j2` passed.
* `cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` passed and reported `iCTS run successfully.`
* First ECC dev check reported in-scope format findings and CMake link-visibility warnings. Formatting and CMake visibility fixes were applied, `git diff --check` passed, and `cmake --build build --target icts_source_flow -j2` passed.
* Second ECC dev check passed with 0 in-scope findings for format, tidy, headers, CMake, and IWYU.
* Final `cmake --build build --target iEDA -j2` passed after ECC fixes.
* Final full iCTS dev script passed after ECC fixes, reported `iCTS run successfully.`, finished read-data/synthesis/instantiation/evaluation/report, and generated statistics plus SVG/GDS visualization reports.
* Final `git diff --check` passed.
