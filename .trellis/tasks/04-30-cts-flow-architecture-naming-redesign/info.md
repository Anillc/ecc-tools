# CTS flow architecture and naming proposal

## Revision status

The original proposal below recommended `pipeline/`, `clock_tree/`, `topology/htree/`, `netlist/`, `view/`, `analysis/`, and `report/` as directories. It is now superseded by the user's clarified design principles:

* Cross-module database belongs in `src/operation/iCTS/source/database`.
* Internal data should not be split into many tiny public structs; prefer module-owned data managers/contexts.
* The architecture should prioritize high cohesion and low coupling.
* Avoid `flow/flow` and avoid awkward substitutes such as `clock_tree_flow`.
* Use `synthesis` for the CTS algorithm body.
* Use `instantiation` for materializing CTS algorithm results into committed design/iDB objects. Do not use legacy DB-output wording as an architecture name.
* Keep `setup`, `synthesis`, `instantiation`, `evaluation`, and `report` as the high-level flow layers.
* Each folder should expose one readable primary entry file aligned with the folder name.
* Do not add a redundant `database/cts` folder under iCTS; put shared CTS design data under existing database concepts.

The final recommendation is therefore: keep industry CTS vocabulary, make `synthesis` the cohesive algorithm module, make `instantiation` the post-algorithm materialization layer, and use names that are recognizable to CTS developers while still reflecting the real algorithm internals. The confirmed names are `distribution`, `topology/htree/characterization`, `topology/htree/pattern_search`, `topology/htree/construction`, `trace`, `instantiation/design`, `instantiation/idb`, and report children `summary`, `statistics`, `export`, and `visualization`.

## Current recommended architecture

This is the current target architecture after the independent subflow research pass and user naming convergence:

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

### Why this is the preferred shape

* `setup` is narrow and small today. A single `Setup.hh/.cc` is enough; adding `config/`, `paths/`, or `adapters/` would duplicate database/config/adapter ownership and create empty architecture.
* `synthesis` is the CTS algorithm body. Its second-level names are CTS/EDA business responsibility domains:
  * `distribution`: clock source/root/sink organization, source-to-root strategy, domain sequencing, sink grouping, root anchors, clustering inputs, and electrical constraints prepared for synthesis. The entry file remains `ClockDistribution.hh/.cc` to keep the clock-specific business meaning explicit.
  * `topology`: topology-family dispatch and public topology concepts. H-tree is currently the only major family, so H-tree internals live below `topology/htree`.
  * `topology/htree/characterization`: grid/cache/CharBuilder adaptation and reusable characterization data.
  * `topology/htree/pattern_search`: H-tree internal algorithm over segment/topology patterns, including pattern registry, candidate/frontier construction, depth search, legality filtering, Pareto/fallback handling, and best-pattern resolution.
  * `topology/htree/construction`: selected pattern materialized into temporary synthesis products, not committed design/iDB objects.
  * `trace`: synthesis execution trace, including selected pattern/depth summaries, success/failure reasons, fallback notes, inserted-object counts, and algorithm-owned trace records. This is not final QoR evaluation and not report formatting.
* `instantiation` is the materialization boundary. It owns the transition from synthesis output to committed design/iDB state:
  * `design`: validation and conversion into `Design` objects, clock membership updates, rollback cleanup. The entry file is `DesignConversion.hh/.cc`.
  * `idb`: conversion/projection of committed CTS design objects into iDB through `Wrapper`. The entry file is `IdbConversion.hh/.cc`.
* `evaluation` should stay small at the root first. It owns metric computation and timing/physical evaluation. Do not split it until extraction produces real code boundaries. If the current statistics writer remains evaluation-owned temporarily, place it under `evaluation/statistics/`; the cleaner long-term target is for evaluation to compute results and `report/statistics` to write report files.
* `report` is a final-output subsystem, not a visualization wrapper:
  * `summary`: report mode, final status, runtime/status summaries, consolidated report tables.
  * `statistics`: `.rpt` files and schema/log tables produced from evaluation statistics.
  * `export`: result export policy, save-dir/report-root resolution, generated output manifest, and export status. `ResultExport` should describe the export of report results, not own all report generation.
  * `visualization`: SVG/GDS clock-tree design and flyline views. `ClockTreeVisualization` is acceptable here because visualization is one report child among `summary`, `statistics`, and `export`, so the report layer no longer reads as visualization-only.

### Names explicitly avoided

Avoid second-level folders such as:

* `sink_domain/`: too close to an implementation partition; use `distribution/` and keep sink-domain types inside that responsibility.
* `per_clock/`: describes a loop axis, not a module responsibility; use `distribution/` or the root `Synthesis` entry depending on code size.
* `candidate/`: describes a data shape; use `pattern_search/`.
* selection-only or repeated-topology folder names: they make the H-tree internals look like only final selection; use `pattern_search/` for pattern/frontier/depth/legality algorithms.
* `planning/`, `preparation/`, `exploration/`, `assembly/`, `diagnostics/`, `qor/`: too abstract or too easily confused with final evaluation when used as final folder names; prefer CTS terms such as `distribution`, `pattern_search`, `construction`, and `trace`.
* generic execution-log class names: too broad; use `SynthesisTrace` under `synthesis/trace`.
* `context/`, `types/`, `data/`, `utils/`, `helper/`: these tend to become struct/helper dumping grounds.
* `artifacts/`, `result_files/`, and `visual_diagnostics/`: too computer-generic or file-shape-driven for report architecture; prefer `export` for save/export responsibility and `visualization` for SVG/GDS clock-tree views.
* `visualization/` as the only report child: it makes the report subflow look narrower than it is. `visualization/` is acceptable only as one sibling under `report` beside `summary`, `statistics`, and `export`.

## Subflow research conclusions

The final structure above is based on the per-subflow research artifacts:

| Subflow | Research artifact | Decision |
|---|---|---|
| `setup` | `research/subflow-setup-architecture.md` | Single `Setup.hh/.cc`; no secondary folders justified today. |
| `synthesis` | `research/subflow-synthesis-architecture.md` | Use final responsibility names: `distribution`, `topology`, H-tree `characterization`/`pattern_search`/`construction`, and `trace/SynthesisTrace`. |
| `instantiation` | `research/subflow-instantiation-architecture.md` | Own both `Design` materialization and iDB projection; split into `design/DesignConversion` and `idb/IdbConversion` when implementation size justifies it. |
| `evaluation` | `research/subflow-evaluation-architecture.md` | Root `Evaluation.hh/.cc`; keep statistics writer transitional, but avoid broad premature splits. |
| `report` | `research/subflow-report-architecture.md` | Use `summary`, `statistics`, `export`, and `visualization` so report is not mistaken for visualization-only output and avoids computer-generic names. |
| `database/design/ClockTree` | `research/database-clocktree-boundary.md` | Stable semantic index over committed CTS objects; not a synthesis result bag, instantiation service, evaluation summary, or report model. |

## Prior revised architecture (superseded by subflow research)

The section below was an intermediate design after the first naming pass. It is superseded by the "Current recommended architecture" section above.

Recommended high-level structure:

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
      SynthesisContext.hh
      SinkDomainManager.hh
      SinkDomainManager.cc
      PerClockSynthesis.hh
      PerClockSynthesis.cc
      topology/
        Topology.hh
        Topology.cc
        htree/
          HTree.hh
          HTree.cc
          internal/

    instantiation/
      Instantiation.hh
      Instantiation.cc
      ClockTreeInstantiator.hh
      ClockTreeInstantiator.cc

    evaluation/
      Evaluation.hh
      Evaluation.cc
      CTSStatisticsWriter.hh
      CTSStatisticsWriter.cc

    report/
      Report.hh
      Report.cc
      visualization/
        Visualization.hh
        Visualization.cc
```

`flow/Flow.hh` is the top-level flow entry. Each subdirectory has a matching primary entry: `setup/Setup.hh`, `synthesis/Synthesis.hh`, `instantiation/Instantiation.hh`, `evaluation/Evaluation.hh`, `report/Report.hh`, `report/visualization/Visualization.hh`, `synthesis/topology/Topology.hh`, and `synthesis/topology/htree/HTree.hh`.

Layer responsibility:

| Layer | Responsibility | Should own | Should not own |
|---|---|---|---|
| `database/design` | Stable cross-module CTS design data | Clock-tree model, CTS roles/domains, committed/logical CTS design objects consumed by synthesis, instantiation, evaluation, and report | Algorithm-local search state, temporary candidate structs |
| `setup` | Runtime environment | Config/work dir/logging/wrapper/STA readiness | Clock-tree algorithm, instantiation, reports |
| `synthesis` | CTS algorithm body | Per-clock synthesis, sink-domain management, H-tree invocation, algorithm result assembly | iDB object materialization, report formatting, public database object definitions |
| `instantiation` | Materialize algorithm result into design/iDB | Convert synthesis result/clock-tree model into committed `Design` and iDB objects | H-tree search, sink-domain algorithm policy, evaluation/report logic |
| `evaluation` | Readonly QoR/timing/physical evaluation | Metrics/statistics over committed CTS topology | Design mutation except explicit timing/RC refresh side effects |
| `report` | Human/machine-readable artifacts | Log/report tables, SVG/GDS visualization, report schema | Algorithm decisions or database commit |

## Primary entry file rule

Each directory should be understandable from one main file:

| Directory | Main file | Main class | Purpose |
|---|---|---|---|
| `flow/` | `Flow.hh` | `CTSFlow` | Owns the full CTS lifecycle and delegates to layer entries. |
| `setup/` | `Setup.hh` | `CTSSetup` | Initializes runtime config, work directory, logging, and adapters. |
| `synthesis/` | `Synthesis.hh` | `CTSSynthesis` | Runs the CTS algorithm and produces a cohesive synthesis result/model. |
| `synthesis/topology/` | `Topology.hh` | `ClockTreeTopology` | Abstracts topology-family dispatch and common topology concepts. |
| `synthesis/topology/htree/` | `HTree.hh` | `HTree` | Public H-tree algorithm entry; hides candidate/search internals. |
| `instantiation/` | `Instantiation.hh` | `CTSInstantiation` | Creates/materializes CTS design and iDB objects from synthesis output. |
| `evaluation/` | `Evaluation.hh` | `CTSEvaluation` | Computes QoR/timing/physical summaries from instantiated topology. |
| `report/` | `Report.hh` | `CTSReport` | Emits log/schema/statistics and delegates visual outputs. |
| `report/visualization/` | `Visualization.hh` | `CTSVisualization` | Owns SVG/GDS visualization entry points. |

Secondary files are allowed, but readers should be able to start from the main file in every folder.

## Folder decomposition rule

For every flow-layer folder, use this rule:

* The folder root should contain the primary entry pair aligned with the folder name, for example `synthesis/Synthesis.hh` and `synthesis/Synthesis.cc`.
* If the implementation needs more files, those files should live in responsibility-based subfolders, not as many peer files beside the primary entry.
* If a helper is small, local, and not independently meaningful, keep it as a private function/class in the primary `.cc` instead of creating a file or subfolder.
* If a helper grows into a separately understandable responsibility, create a subfolder with its own primary entry pair.
* Apply the same pattern recursively for large subfolders.
* `CMakeLists.txt` is the expected build-file exception.

Concrete implications:

```text
synthesis/
  Synthesis.hh
  Synthesis.cc
  context/
    Context.hh
    Context.cc
  sink_domain/
    SinkDomain.hh
    SinkDomain.cc
  per_clock/
    PerClock.hh
    PerClock.cc
  topology/
    Topology.hh
    Topology.cc
    htree/
      HTree.hh
      HTree.cc
      characterization/
      candidate/
      construction/
```

Avoid this pattern:

```text
synthesis/
  Synthesis.hh
  Synthesis.cc
  SynthesisContext.hh
  SinkDomainManager.hh
  SinkDomainManager.cc
  PerClockSynthesis.hh
  PerClockSynthesis.cc
  HTreeOptionAdapter.hh
  HTreeOptionAdapter.cc
  Metrics.hh
  Metrics.cc
```

The second layout recreates the current readability problem: a reader sees many peer files and cannot tell which are entry points, subdomains, internal helpers, or data carriers.

Use code volume as a guardrail:

| Case | Recommended treatment |
|---|---|
| 1-2 small helpers, single responsibility | Keep private inside the main `.cc`. |
| Several helpers serving one coherent subdomain | Create one subfolder with a primary entry file for that subdomain. |
| Many files or multiple independent responsibilities | Split into multiple subfolders by responsibility, not by type suffix. |
| Types exist only to support one `.cc` implementation | Keep them local to that `.cc` or an `internal/` subfolder. |
| Types become stable module contracts | Promote only coarse contracts to the subfolder primary header. |

## `database/design/ClockTree` responsibility boundary

`database/design/ClockTree` should be the stable CTS clock-tree design model shared by synthesis, instantiation, evaluation, and report. It should describe what clock-tree topology exists or is intended to exist in iCTS semantic terms. It should not describe how the topology was searched, scored, instantiated, reported, or serialized.

### Owns

`ClockTree` should own stable, cross-module CTS design facts:

* Clock-tree identity: clock name, clock net name, source pin/source net references, design DBU information.
* Per-clock tree records: roots, sink domains, source-to-root connectivity, downstream/sink-tree connectivity.
* Semantic roles: clock source, source-to-root net, downstream net, sink-tree net, root buffer, tree buffer, clock load.
* Sink-domain classification: hard macro, regular, source-to-root.
* Logical tree topology metadata needed after synthesis: topology level/depth, parent/child relationships where needed, inserted object membership, routed/flyline segment records if they are stable design facts.
* Coarse status/provenance that downstream modules need, such as whether synthesis result has been instantiated into design objects.

### Does not own

`ClockTree` should not own:

* H-tree candidate search state, frontier sets, characterization grids, legality signatures, depth-window exploration, or selected-pattern internals.
* Temporary `unique_ptr<Inst/Pin/Net>` ownership used before instantiation commits objects into `Design`.
* Instantiation side effects: creating buffers/nets/pins, reconnecting nets, committing objects to `Design`, or pushing objects into iDB.
* Evaluation results such as timing slack, skew, latency, wirelength statistics, cell distribution, or report summary fields. Those belong to `evaluation` result models unless they become stable design attributes.
* Report formatting concepts such as SVG/GDS layer keys, colors, table rows, report status rows, or file paths.
* Runtime/logging state, failure diagnostics for internal algorithm attempts, or debug-only counters.

### Relationship to existing classes

`Clock` currently owns the basic clock identity, source, loads, and committed CTS membership. `Design` owns all committed `Clock`, `Inst`, `Pin`, and `Net` objects. `ClockTree` should not replace these. It should provide the CTS semantic graph over those existing database objects.

That means:

* `Clock` remains the clock membership owner.
* `Design` remains the committed object owner.
* `ClockTree` becomes the semantic clock-tree model that explains how committed or to-be-instantiated CTS objects relate to clock-tree roles/domains/topology.
* `synthesis` builds a `ClockTree` or a coarse synthesis result containing a `ClockTree`.
* `instantiation` consumes that model and materializes missing design/iDB objects.
* `evaluation` and `report` read the model; they do not mutate it except through explicit, narrow status updates if needed.

### Design shape

The public `ClockTree.hh` should avoid dozens of tiny public structs. Prefer a manager-style model with narrow nested/coarse records:

```cpp
class ClockTree
{
 public:
  struct ClockRecord;
  struct NetRecord;
  struct InstRecord;
  struct SegmentRecord;

  auto reset() -> void;
  auto ensureClock(...) -> ClockRecord&;
  auto addNet(...) -> void;
  auto addInst(...) -> void;
  auto addSegment(...) -> void;
  auto findClock(...) const -> const ClockRecord*;
  auto get_clocks() const -> const std::vector<ClockRecord>&;
};
```

If a record starts accumulating behavior, promote that behavior into a sub-manager or implementation file. Do not let `ClockTree.hh` become a dump of every CTS algorithm intermediate.

## Struct proliferation assessment

The current issue is not that `struct` exists; CTS algorithms need options, results, candidates, keys, and metrics. The issue is that many tiny structs are public headers and become de facto cross-module contracts without a module owner.

Current examples:

* `ClockTreeView` is a useful manager-like abstraction, but its roles, segments, nets, insts, and clock records are report/visualization-facing diagnostics. Under the user's rule, stable cross-module clock-tree design data should move toward `database/design/ClockTree.hh`, not a new `database/cts` folder.
* `ClockSynthesis::BuildResult` exposes H-tree result internals, cluster metadata, inserted object ownership, topology levels, and final object vectors as one public result bag. This couples synthesis callers to H-tree internals.
* `HTreeBuilder::BuildResult` is a very large public aggregate containing topology, characterization, search statistics, load profile data, selected candidates, object ownership, root pins/nets, and logging context. Most of this is algorithm-internal and should be hidden behind an H-tree result manager or internal context.
* `ClockSinkDomainContext`, `ClockSinkDomainPartition`, and `ClockSinkDomainRootBufferSpec` are reasonable concepts, but together they reveal preparation details across stage/transaction/view boundaries. They should be owned by a `SinkDomainManager` or `CTSFlowContext`.
* `ClockTreeSummary`, `ClockTreeEvaluationState`, and `CTSStatistics` are close to stable evaluation data. If consumed outside evaluation/report internals, they should be wrapped by a coarse evaluation result model; do not create a redundant `database/cts` namespace just for them.

Recommended struct policy:

| Data category | Rule | Examples |
|---|---|---|
| Stable cross-module CTS design data | Put in `source/database/design`; keep names semantic and behavior-neutral | `ClockTree`, CTS roles/domains, clock-tree segment/net/inst records |
| Module boundary DTO | Allow 1-2 coarse option/result types per module boundary | `CTSSynthesis::Options`, `CTSSynthesis::Result`, `CTSInstantiation::Result` |
| Internal mutable state | Encapsulate in a manager/context class, preferably not copied through function chains | `SynthesisContext`, `SinkDomainManager`, `HTreeBuildContext`, `HTreeCandidateRepository` |
| Algorithm-local candidate/key structs | Keep in `.cc` or `internal/`; do not expose through public headers | H-tree candidate keys, frontier entries, legality signatures |
| Report-only formatting records | Keep private to `report/` unless persisted/shared | GDS writer tables, SVG status rows |

Practical target: each major module should expose one main class and one coarse result type. Detailed structs should be private implementation detail unless another module truly needs the semantic data.

## Fit of current design against user principles

| Principle | Current fit | Assessment |
|---|---|---|
| Cross-module database in `source/database` | Partial | Core `Design`, `Clock`, `Inst`, `Net`, `Pin`, config, characterization, spatial/routing/timing types are correctly under `database`. But `ClockTreeView`, CTS roles/domains, and some summary/result data behave like shared cross-module design data while living under `flow`; move stable design data toward `database/design`, without adding `database/cts`. |
| Avoid over-atomic internal data | Weak in algorithm internals | `htree/` and `synthesis/` expose many fine-grained public structs. They help algorithm implementation but make directory reading harder and leak implementation details. |
| High cohesion, low coupling | Partial | CMake boundaries exist and many responsibilities are conceptually separated, but headers leak details across `stage`, `synthesis`, `htree`, `netlist`, `clock_tree_view`, `evaluation`, and `visualization`. |
| Main layers: setup, synthesis, instantiation, evaluation, report | Partial | Current `run_setup`, `evaluation`, and report/visualization are close. Current `stage + synthesis + htree` should be reorganized into cohesive algorithm-body `synthesis`. Current netlist commit/iDB materialization should become `instantiation`. |

## Naming decision for `synthesis` and `instantiation`

Use `synthesis` in its standard CTS sense: the algorithmic construction of the clock tree. This aligns with industry/open-source expectations and avoids `flow/flow`.

Use `instantiation` for the layer that turns synthesis output into actual design/iDB objects. This is more semantic than DB-output wording: it says the layer materializes object instances and nets, not merely that it writes data somewhere.

Recommended class names:

* `CTSSynthesis`: algorithm entry.
* `SynthesisContext`: module-owned internal data manager.
* `SinkDomainManager`: owns sink-domain partition/preparation data.
* `HTree`: public H-tree topology entry.
* `CTSInstantiation`: materialization layer entry.
* `ClockTreeInstantiator`: creates/commits clock-tree buffers, nets, and connectivity.

## Superseded material

Earlier alternate layouts from the planning pass are intentionally omitted from this active design. The current recommendation is the `Flow`-owned `setup -> synthesis -> instantiation -> evaluation -> report` architecture shown above. `Flow` is the only CTS flow lifecycle owner; no alternate lifecycle manager or transitional entry is retained in the current design.
