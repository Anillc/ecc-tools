# CTS flow refactor plan

## Purpose

Refactor `src/operation/iCTS/source/flow` so the directory structure communicates the CTS business flow directly:

```text
setup -> synthesis -> instantiation -> evaluation -> report
```

The refactor is primarily an architectural and naming cleanup. It must preserve CTS behavior unless an implementation step uncovers an explicit bug that is fixed in a separate, clearly scoped change.

The target design solves these current problems:

* `stage`, `synthesis`, and `htree` contain many peer files and too many public structs, making it hard to see main interfaces.
* `stage` mixes flow orchestration, per-clock coordination, synthesis transaction handling, materialization, evaluation, and report wrappers.
* `synthesis` and `htree` expose algorithm internals as broad result structs and implicit cross-module contracts.
* DB-output/materialization naming is too direct and leaks implementation mechanics.
* report output is split across `clock_tree_view`, `visualization`, evaluation statistics, and report stage code in a way that makes ownership unclear.
* stable CTS semantic data is mixed with report/visualization-only view data.

## Target architecture

```text
src/operation/iCTS/source/
  database/
    design/
      ClockTree.hh
      ClockTree.cc

  flow/
    Flow.hh
    Flow.cc

    setup/
      Setup.hh
      Setup.cc

    synthesis/
      Synthesis.hh
      Synthesis.cc
      distribution/
        ClockDistribution.hh
        ClockDistribution.cc
      topology/
        Topology.hh
        Topology.cc
        htree/
          HTree.hh
          HTree.cc
          characterization/
          pattern_search/
          construction/
      trace/
        SynthesisTrace.hh
        SynthesisTrace.cc

    instantiation/
      Instantiation.hh
      Instantiation.cc
      design/
        DesignConversion.hh
        DesignConversion.cc
      idb/
        IdbConversion.hh
        IdbConversion.cc

    evaluation/
      Evaluation.hh
      Evaluation.cc

    report/
      Report.hh
      Report.cc
      summary/
        Summary.hh
        Summary.cc
      statistics/
        StatisticsReport.hh
        StatisticsReport.cc
      export/
        ResultExport.hh
        ResultExport.cc
      visualization/
        ClockTreeVisualization.hh
        ClockTreeVisualization.cc
        svg/
        gds/
```

Root folder rule:

* Each flow folder root exposes only its primary entry pair and build metadata.
* Extra implementation files move into responsibility-based subfolders when code volume justifies it.
* Tiny helpers can stay private in the `.cc` file; do not create empty architecture.
* Do not create dumping-ground folders named `context`, `types`, `data`, `utils`, `helper`, or `internal` at the flow root.

## Problem to solution mapping

| Existing problem | Refactor solution | Red line |
|---|---|---|
| Too many peer files in `stage`, `synthesis`, and `htree` | Introduce one primary entry per folder and move implementation into responsibility subfolders | Do not leave dozens of helper files beside `Synthesis.hh` or `HTree.hh` |
| Too many small public structs | Replace scattered public records with module-owned managers/results and private helper records | Fine-grained structs must not become cross-module contracts unless they model stable business semantics |
| `stage` mixes orchestration and business behavior | Split responsibilities into `setup`, `synthesis`, `instantiation`, `evaluation`, and `report` | Flow orchestration must not own H-tree search, iDB conversion, or report formatting |
| DB-output wording is too mechanical | Use `instantiation` as the materialization layer | Do not expose legacy DB-output wording as an architecture-level directory/class name |
| Synthesis trace naming could be confused with final QoR | Use `trace/SynthesisTrace` for synthesis execution records | Synthesis trace must not compute final skew, latency, routed wirelength, or area |
| Selection-only topology naming repeats the parent and narrows the meaning | Use `pattern_search` for H-tree pattern/frontier/depth/legality algorithms | Do not use repeated-topology or broad `candidate` folders |
| Report looks like visualization-only output | Split report into `summary`, `statistics`, `export`, and `visualization` | Visualization must be one report child, not the report architecture |
| Stable ClockTree semantics are mixed with view/report records | Move stable clock-tree semantic model to `database/design/ClockTree` | `ClockTree` must not become a synthesis result bag, report model, or instantiation service |

## Module contracts and red lines

### `database/design/ClockTree`

Owns stable CTS semantic facts over committed or to-be-instantiated design objects:

* clock tree identity and clock membership
* source-to-root, downstream, and sink-tree relationships
* net and instance roles
* sink-domain classification
* topology level/depth indexes that remain meaningful after instantiation

Red lines:

* No H-tree candidate/frontier/search state.
* No temporary `unique_ptr<Inst>`, `unique_ptr<Pin>`, or `unique_ptr<Net>` ownership.
* No iDB commit, reconnect, or conversion operations.
* No final timing/QoR/evaluation summary.
* No SVG/GDS/report formatting model.

### `flow/Flow`

Owns the high-level CTS lifecycle:

```text
setup -> synthesis -> instantiation -> evaluation -> report
```

Red lines:

* `Flow` is the only CTS flow lifecycle owner.
* Do not introduce a second manager, facade, forwarding layer, or alternate entry for the same lifecycle state.
* No H-tree search implementation.
* No direct iDB object construction.
* No report file formatting.
* No broad data structs that duplicate subflow results.

### `setup/Setup`

Owns runtime readiness:

* config readiness
* work directory, log file, statistics directory, visualization directory setup
* schema/log initialization
* iDB `Wrapper` and STA adapter initialization
* design/clock input discovery needed before synthesis

Red lines:

* No synthesis decisions.
* No topology construction.
* No evaluation/report computation.

### `synthesis/Synthesis`

Owns the CTS algorithm body:

* coordinates distribution, topology family dispatch, H-tree algorithm, and synthesis trace
* returns synthesis products ready for instantiation
* keeps algorithm temporary data local to synthesis

Red lines:

* No committed `Design` ownership changes.
* No iDB projection.
* No final QoR metrics.
* No report file output.

### `synthesis/distribution/ClockDistribution`

Owns clock distribution setup for synthesis:

* clock source/root/sink organization
* source-to-root strategy
* sink grouping and sink-domain handling
* root anchors and clustering inputs
* electrical constraints prepared for H-tree synthesis

Red lines:

* Do not implement H-tree pattern search here.
* Do not commit generated objects.
* Do not expose `sink_domain` as a directory just because the loop is organized by domains.

### `synthesis/topology/Topology`

Owns topology-family dispatch:

* selects the topology family entry point
* keeps public topology concepts visible
* currently routes to H-tree

Red lines:

* Do not make this a generic helper folder.
* Do not put H-tree candidate/frontier internals directly under `topology/`.

### `synthesis/topology/htree/HTree`

Owns the H-tree topology family entry:

* coordinates characterization, pattern search, and construction
* hides most H-tree internals behind the H-tree facade

Red lines:

* The `htree/` root must not remain a flat list of every H-tree helper file.
* H-tree internals should not leak as flow-level contracts.

### `synthesis/topology/htree/characterization`

Owns characterization inputs and caches:

* characterization library/cache
* grid and wirelength lattice adaptation
* CharBuilder setup and reusable segment characterization data

Red lines:

* No depth search or pattern selection policy.
* No clock-tree object construction.

### `synthesis/topology/htree/pattern_search`

Owns H-tree internal search algorithms:

* segment pattern registry
* topology pattern registry
* segment candidate frontier construction
* depth candidate search/evaluation
* sink-load-profile legality checks
* feasible/candidate pool filtering
* Pareto and fallback best-pattern resolution

Red lines:

* No committed object construction.
* No report formatting.
* No broad public `Candidate*` data bags outside the H-tree boundary.
* Do not use a selection-only name; this module is more than final selection.

### `synthesis/topology/htree/construction`

Owns temporary H-tree product construction:

* converts the selected pattern into temporary synthesis products
* builds temporary buffer/net/pin objects required by instantiation
* records topology levels for temporary objects

Red lines:

* These are not committed `Design` objects yet.
* No iDB calls.
* No final report/evaluation output.

### `synthesis/trace/SynthesisTrace`

Owns synthesis execution records:

* per-clock and per-domain synthesis status
* selected pattern/depth summaries
* fallback and failure reasons
* inserted buffer/net counts from synthesis
* algorithm-owned trace tables consumed by report

Red lines:

* Do not call this QoR.
* Do not compute final skew, latency, area, or routed clock-tree wirelength.
* Do not own logger/schema writer formatting details.
* Do not own temporary object lifetimes.

### `instantiation/Instantiation`

Owns the materialization boundary:

* consumes synthesis products
* validates materialization inputs
* invokes design conversion and iDB conversion in order
* handles rollback/cleanup policy for failed materialization

Red lines:

* No H-tree pattern search.
* No final QoR evaluation.
* Do not expose legacy DB-output wording as module naming.

### `instantiation/design/DesignConversion`

Owns conversion into iCTS design state:

* committed `Design` object insertion
* `Clock` membership update
* `ClockTree` semantic index update
* rollback cleanup for partially materialized design objects

Red lines:

* No iDB calls.
* No topology search.
* No report file output.

### `instantiation/idb/IdbConversion`

Owns projection into iDB through `Wrapper`:

* creates or updates iDB objects from committed CTS design state
* keeps iDB API usage behind the wrapper boundary
* reports conversion status to `Instantiation`

Red lines:

* No direct synthesis result mutation.
* No algorithm decision making.
* No evaluation/report generation.

### `evaluation/Evaluation`

Owns readonly final metrics over committed CTS topology:

* timing/latency/skew queries
* physical wirelength/area/buffer count summaries
* statistics data consumed by report

Red lines:

* No design topology mutation except explicit timing/RC refresh side effects that already belong to adapters.
* No `.rpt` writing as the long-term target.
* No SVG/GDS generation.

### `report/Report`

Owns final output orchestration:

* summary
* statistics report files
* result export
* visualization

Red lines:

* No synthesis decisions.
* No instantiation.
* No evaluation metric computation beyond requesting or formatting existing evaluation results.

### `report/summary/Summary`

Owns summary output:

* report mode
* final status
* runtime/status summary
* consolidated report tables

Red lines:

* No detailed statistics file writer.
* No SVG/GDS.

### `report/statistics/StatisticsReport`

Owns statistics report output:

* `.rpt` files
* schema/log statistics tables based on evaluation data

Red lines:

* Do not own metric computation long-term.
* Do not own export path policy beyond receiving output roots.

### `report/export/ResultExport`

Owns output export policy:

* save directory and report root resolution
* output manifest
* generated result status
* export-oriented status tables

Red lines:

* Do not generate all report content.
* Do not become a replacement for `summary`, `statistics`, or `visualization`.

### `report/visualization/ClockTreeVisualization`

Owns clock-tree visual output:

* SVG design/flyline output
* GDS design/flyline output
* visualization layer policy and visual model adaptation

Red lines:

* Do not own stable CTS semantic data.
* Do not mutate design or iDB.
* Do not compute final QoR.

## Data and struct strategy

The refactor must reduce public scattered structs instead of only moving files.

Rules:

* Cross-module persistent CTS semantics belong in `database/design/ClockTree`.
* Module-local temporary state belongs to module-owned managers or compact result objects.
* Fine-grained records for candidates, frontiers, legality signatures, and pattern refs should stay private to `pattern_search` unless another module has a stable business need for them.
* A public header should expose behavior and stable contracts, not every intermediate algorithm record.
* Prefer one compact subflow result per boundary over many small structs shared across folders.
* Avoid broad snapshots that duplicate `Design`, `Clock`, `ClockTree`, `Evaluation`, or report data.

Expected boundary records:

| Boundary | Allowed public data shape |
|---|---|
| `Setup -> Flow` | setup status and initialized runtime paths |
| `Synthesis -> Instantiation` | compact synthesis products and `SynthesisTrace` reference/summary |
| `Instantiation -> Evaluation` | committed materialization status and design/iDB conversion status |
| `Evaluation -> Report` | evaluation summary/statistics data |
| `Report -> API/user` | report/export status |

## Current struct problem analysis

The current `flow` tree has about 91 visible `struct` declarations from a direct source scan. Most of them are in headers rather than `.cc` files:

| Current area | Header structs | Main issue |
|---|---:|---|
| `htree` | about 35 | Algorithm internals such as candidates, frontiers, search results, pattern registries, construction products, and trace summaries are exposed as broad public contracts. |
| `clock_tree_view` | about 14 | Stable CTS semantic roles are mixed with report/visualization view records. |
| `stage` | about 12 | Flow orchestration structs mix run summary, sink-domain context, materialization result, evaluation result, and report result. |
| `visualization` | about 11 | Mostly file-format and visualization records; acceptable if confined to report visualization. |
| `synthesis` | about 8 | `ClockSynthesis::BuildResult` mixes H-tree internals, trace fields, clustering metadata, and temporary object ownership. |
| `evaluation` | about 8 | Evaluation summary/statistics are legitimate boundary data, but statistics writing should move to report. |

The problem is not the existence of structs. The problem is that many structs are public because directory boundaries are unclear. The final architecture solves this only if each struct is reclassified by ownership.

### Struct ownership matrix

| Current struct family | Examples | Target ownership | Action |
|---|---|---|---|
| Stable CTS semantics | `CTSNetRole`, `CTSInstRole`, sink-domain classification, topology level/depth facts, clock-tree net/inst relationships | `database/design/ClockTree` | Promote into stable design model only when the fact remains meaningful after instantiation. |
| Synthesis algorithm trace | selected depth/pattern summaries, fallback reason, synthesis failure reason, inserted count summaries | `synthesis/trace/SynthesisTrace` | Keep compact and readonly; do not include final QoR or temporary object ownership. |
| H-tree pattern search internals | `CandidateBuildEvaluation`, `CandidateCharRef`, `HTreeTopologyDepthSearchResult`, `SegmentCandidateFrontierSet`, legality signatures, pattern registry nodes | `synthesis/topology/htree/pattern_search` | Make private to pattern search unless the H-tree facade has a stable reason to expose a reduced summary. |
| H-tree characterization internals | `CharacterizationGridPlan`, `ResolvedBuildOptions`, characterization request/cache records | `synthesis/topology/htree/characterization` | Keep module-local; expose only characterization service/facade outputs needed by H-tree. |
| Temporary construction products | inserted object ownership vectors, inserted inst/net level records, object build context | `synthesis/topology/htree/construction` and the compact `Synthesis -> Instantiation` product | Separate object ownership from trace summaries; do not expose construction internals to report/evaluation. |
| Clock distribution input state | `ClockSinkDomainPartition`, `ClockSinkDomainContext`, root buffer specs | `synthesis/distribution` | Encapsulate behind `ClockDistribution`; do not make `sink_domain/` a top-level folder or cross-flow contract. |
| Materialization results | external-DB projection result, commit status, rollback status | `instantiation/Instantiation`, `design/DesignConversion`, `idb/IdbConversion` | Use instantiation naming and split design vs iDB status. |
| Evaluation outputs | `ClockTreeSummary`, `ClockTreeEvaluationState`, `CTSStatistics` | `evaluation/Evaluation`; report receives readonly values | Keep as legitimate boundary data, but move `.rpt` writing responsibility to `report/statistics`. |
| Report/export/visual records | report result, SVG/GDS result, GDS writer records, visualization model records | `report/summary`, `report/statistics`, `report/export`, `report/visualization` | Keep file-format and visualization structs local to report. Stable roles should come from `ClockTree`, not view-only structs. |

### What this framework fixes

The proposed architecture fixes the messy-struct problem in four ways:

1. It creates real ownership homes. A struct has to belong to one of `ClockTree`, `distribution`, `pattern_search`, `construction`, `SynthesisTrace`, `Instantiation`, `Evaluation`, or `Report`.
2. It separates lifecycle boundaries. Synthesis products, instantiation status, evaluation summaries, and report export status become separate contracts instead of one stage-level data chain.
3. It distinguishes stable business semantics from algorithm state. Clock-tree roles and topology facts can move toward `ClockTree`; candidate pools and frontier entries stay private to `pattern_search`.
4. It separates trace from ownership. `SynthesisTrace` records what happened; construction products own temporary objects; instantiation commits them.

### What this framework does not automatically fix

Directory movement alone will not fix struct sprawl. The implementation must also:

* remove broad includes that force public struct visibility, such as exposing H-tree candidate/search types through synthesis headers;
* collapse tiny pass-through records when a module-owned manager can keep the state private;
* replace all-in-one `BuildResult` bags with smaller boundary records;
* move private algorithm structs from `.hh` files to `.cc` files where practical;
* keep public headers limited to primary entry facades and stable boundary data.

### Recommended public boundary records after refactor

Keep the public boundary small:

| Boundary | Recommended record |
|---|---|
| `setup -> flow` | `SetupResult` or equivalent setup status; no algorithm state. |
| `synthesis -> instantiation` | `SynthesisProduct` or equivalent temporary-product handle plus `SynthesisTrace`; no H-tree candidate/frontier internals. |
| `synthesis trace -> report` | `SynthesisTrace` readonly summary; no temporary object ownership. |
| `instantiation -> evaluation/report` | `InstantiationResult` with design/iDB conversion status; no legacy DB-output naming. |
| `evaluation -> report/API` | `EvaluationSummary` and statistics values; no file writer state. |
| `report -> caller` | `ReportResult` and `ResultExport` status; no synthesis/evaluation internals. |

Any additional struct must pass this test before being public:

1. Is it a stable business concept users or another module must understand?
2. Does it remain valid outside the module that creates it?
3. Is the receiving module supposed to depend on its fields, or only on a higher-level behavior/result?

If any answer is "no", keep it private to the subfolder implementation.

## Current-to-target mapping

| Current area | Target home | Notes |
|---|---|---|
| Former flow-manager lifecycle orchestration | `flow/Flow` | `Flow` directly owns lifecycle state and orchestration; no secondary manager remains. |
| Legacy setup entry | `setup/Setup` | Runtime setup remains narrow. |
| clock data loading stage pieces | `setup` or `flow/Flow` setup sequence | Keep import/readiness separate from synthesis decisions. |
| `stage/ClockTreeSynthesisDriver` | `synthesis/Synthesis` plus `distribution` | Driver loops become synthesis orchestration, not generic stage code. |
| `stage/ClockSinkDomainBuilder` | `synthesis/distribution` | Sink-domain handling stays under distribution, not as a top-level folder. |
| `stage/ClockTreeSynthesisTransaction` | split across `synthesis`, `instantiation`, and `trace` | Algorithm run, materialization, and status records must separate. |
| `synthesis/ClockSynthesis*` | `synthesis/Synthesis`, `distribution`, `trace` | Keep algorithm entry readable and hide implementation records. |
| `synthesis/ClockTreeSynthesisMetrics` | `trace` and `construction` | Trace owns summaries; construction owns temporary object transfer details. |
| `synthesis/ClockSynthesisReporter` | `trace` or `report/summary` depending on content | Algorithm status records stay trace; formatting belongs to report. |
| `htree/CharacterizationLibrary`, `HTreeCharacterization*` | `topology/htree/characterization` | Characterization data and cache boundary. |
| `htree/HTreePatternRegistry`, `HTreeSegmentCandidateFrontier`, `HTreeTopologyDepthSearch`, `HTreeTopologyDepthEvaluation`, `HTreeTopologyAssembly`, `HTreeSinkLoadProfile*` | `topology/htree/pattern_search` | Pattern/frontier/depth/legality algorithms. |
| `htree/HTreeClockTreeObjectBuilder`, build context | `topology/htree/construction` | Temporary products only. |
| `htree/HTreeSynthesisSummary`, `HTreeLogging` | `synthesis/trace` or H-tree trace helpers | Do not call this final QoR. |
| Legacy design-net commit helper | `instantiation/design` | Commit to iCTS design objects and Clock/ClockTree semantics. |
| Legacy external-DB projection step | `instantiation/idb` and `Instantiation` | Architecture name becomes instantiation. |
| `evaluation/ClockTreeEvaluator` | `evaluation/Evaluation` | Metric computation stays readonly. |
| `evaluation/CTSStatisticsWriter` | long-term `report/statistics` | Evaluation computes; report writes. Transitional placement is allowed during staged migration. |
| Legacy report step | `report/Report`, `summary`, `statistics`, `export`, `visualization` | Split orchestration and output-specific writers. |
| `clock_tree_view/ClockTreeView` stable roles/segments | `database/design/ClockTree` where stable | View-only transformation stays under report/visualization. |
| `visualization/ClockTreeSvgVisualization`, `ClockTreeGdsVisualization`, GDS writer/layer policy | `report/visualization` | Keep readonly artifact generation. |

## Migration phases

### Phase 0: introduce facades and build skeleton

Goal: create the target entry points without moving behavior yet.

Actions:

* Add `Flow`, `Setup`, `Synthesis`, `Instantiation`, `Evaluation`, `Report` facade names.
* Add target CMake skeletons for the new folders.
* Keep old classes temporarily as implementation delegates.

Verification:

* Build succeeds.
* CTS public API behavior unchanged.

### Phase 1: flow and setup split

Goal: remove generic stage ownership from runtime setup.

Actions:

* Move setup/readiness code under `setup/Setup`.
* Keep clock data loading separate from synthesis decisions.
* Make `Flow` call setup explicitly before synthesis.

Verification:

* Runtime paths, log setup, schema setup, Wrapper, and STA adapter initialization remain unchanged.

### Phase 2: synthesis facade and distribution boundary

Goal: make `synthesis/Synthesis` the algorithm entry and isolate distribution setup.

Actions:

* Move per-clock/domain synthesis orchestration into `Synthesis`.
* Move sink grouping, root handling, source-to-root preparation, and clustering inputs into `distribution/ClockDistribution`.
* Replace scattered public option/context structs with compact module-owned state where possible.

Verification:

* Same clocks and sink domains are synthesized.
* Failure handling remains equivalent.

### Phase 3: H-tree decomposition

Goal: split H-tree internals by algorithm responsibility.

Actions:

* Move characterization files to `topology/htree/characterization`.
* Move pattern registry, frontier, depth search/evaluation, topology assembly, and sink-load legality to `topology/htree/pattern_search`.
* Move temporary object builders to `topology/htree/construction`.
* Keep `HTree.hh/.cc` as the only H-tree root entry pair.

Verification:

* H-tree selected patterns, inserted temporary objects, fallback behavior, and failure reasons are unchanged.

### Phase 4: synthesis trace cleanup

Goal: replace ambiguous diagnostics/QoR/status scatter with `SynthesisTrace`.

Actions:

* Move algorithm status records to `trace/SynthesisTrace`.
* Separate trace records from temporary object ownership.
* Ensure report consumes trace read-only.

Verification:

* Existing log/status information remains available.
* No final evaluation metrics move into synthesis trace.

### Phase 5: instantiation split

Goal: isolate materialization from synthesis.

Actions:

* Move committed design-object conversion to `instantiation/design/DesignConversion`.
* Move iDB projection through `Wrapper` to `instantiation/idb/IdbConversion`.
* Rename public architecture around instantiation.
* Preserve rollback behavior.

Verification:

* Inserted buffers/nets/pins are committed the same way.
* iDB output is equivalent.
* Rollback/cleanup behavior is preserved.

### Phase 6: ClockTree semantic database boundary

Goal: separate stable clock-tree semantics from report-only view data.

Actions:

* Introduce `database/design/ClockTree` for stable roles, relationships, domains, and topology levels.
* Migrate stable parts of `ClockTreeView` toward `ClockTree`.
* Keep visualization-only model construction inside report visualization.

Verification:

* Evaluation and report can read stable CTS semantics without depending on synthesis internals.
* `ClockTree` does not accumulate algorithm candidates or report formatting data.

### Phase 7: evaluation and report ownership cleanup

Goal: make final metric computation and output generation explicit.

Actions:

* Keep metric computation in `evaluation/Evaluation`.
* Move statistics file writing to `report/statistics/StatisticsReport`.
* Move report path/export status to `report/export/ResultExport`.
* Move SVG/GDS output to `report/visualization/ClockTreeVisualization`.

Verification:

* Existing `.rpt`, SVG, GDS, and report status outputs are preserved.
* Report remains readonly over design/evaluation/trace data.

### Phase 8: legacy wrapper cleanup

Goal: remove obsolete names and broad public headers.

Actions:

* Remove legacy materialization, generic stage, diagnostic-view, and old visualization target names when no longer referenced.
* Remove legacy forwarding headers after all internal includes are migrated.
* Update tests, docs, and CMake target names.

Verification:

* No stale includes reference old directories.
* Build graph shows the intended dependencies.

## Dependency direction

Allowed high-level direction:

```text
flow
  -> setup
  -> synthesis
  -> instantiation
  -> evaluation
  -> report

synthesis -> database/design for readonly inputs
instantiation -> database/design and Wrapper
evaluation -> database/design, ClockTree, STA adapter
report -> evaluation results, SynthesisTrace, ClockTree, config paths
```

Forbidden dependency direction:

* `synthesis` must not depend on `report`.
* `synthesis` must not depend on `instantiation`.
* `evaluation` must not depend on `report`.
* `database/design/ClockTree` must not depend on flow modules.
* `report/visualization` must not mutate `Design`, `ClockTree`, or iDB.

## Verification gates for implementation

Every migration patch should pass at least:

* CMake configure and build for iCTS-related targets.
* Existing CTS tests or smoke flow available in the repository.
* Include hygiene check for stale old paths.
* A behavior comparison for generated CTS reports/log sections when the patch is intended to be behavior-preserving.

For staged file movement patches:

* Prefer one conceptual boundary per patch.
* Do not combine broad directory movement with algorithm changes.

For final convergence patches:

* Do not keep transitional wrappers for architecture names that the target design has retired.
* `Flow.cc` is the only source compiled for the flow root; lifecycle state must live in `Flow`.

## Non-goals

* No CTS algorithm redesign in this task.
* No replacement of H-tree synthesis with a different topology engine.
* No report output schema redesign unless required by the architecture rename.
* No broad database redesign beyond `database/design/ClockTree` semantic boundary.
