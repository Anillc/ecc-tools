# Current iCTS Flow Naming Audit

## Directory Audit

| Directory | Current Role | Issue | Recommendation |
|---|---|---|---|
| `source/flow/session/` | Initializes config, work dir, logging/schema, adapter setup. | `session` is runtime-software wording. | Rename to `run_setup`. |
| `source/flow/report_data/` | Holds clock-tree view/data, visualization model, synthesis-to-view input. | `report_data` is report-specific and engineering-generic. | Rename to `clock_tree_view`. |
| `source/flow/report/` | SVG/GDS visualization writers, GDS writer, layer policy. | `report` is broader than current contents. | Rename to `visualization`. |
| `source/flow/stage/` | Flow-stage coordination and per-clock synthesis transaction. | Acceptable as a flow orchestration term. | Keep for now. |
| `source/flow/synthesis/` | Clock-tree synthesis, sink/source-root synthesis, temporary netlist construction. | Semantically good, but some file names inside need review. | Keep directory. |
| `source/flow/htree/` | H-tree topology, characterization, candidate selection, object construction. | Semantically good, but several files use generic implementation terms. | Keep directory; rename internal files in later batch. |
| `source/flow/evaluation/` | Readonly post-synthesis evaluation and statistics output. | Semantically good. | Keep. |
| `source/flow/netlist/` | Clock-net editing and writeback helpers. | Acceptable if it remains clock-net focused. | Keep; consider `clock_network` only if scope broadens. |

## High-Priority File Naming Issues

### `report_data` / Visualization Boundary

These should move with the directory rename because the directory name and type names reinforce each other.

| Current | Recommended |
|---|---|
| `ClockTreeReportData.*` | `ClockTreeView.*` |
| `ClockTreeReportDataBuilder.*` | `ClockTreeViewBuilder.*` |
| `ClockTreeReportSynthesisData.hh` | `ClockTreeViewSynthesisInput.hh` |
| `ClockTreeVisualizationModel.*` | keep |
| `ClockSynthesisReportAdapter.*` | `ClockTreeViewAdapter.*` |
| `CTSVisualizationReport.*` | `ClockTreeSvgVisualization.*` |
| `CTSGdsReport.*` | `ClockTreeGdsVisualization.*` |
| `ClockTreeVisualizationLayerPolicy.*` | keep |

### H-tree Internals

These need behavior-level confirmation before implementation. Candidate names:

| Current | Candidate | Priority |
|---|---|---|
| `HTreeActualLoad.*` | `HTreeSinkLoadProfile.*` or `HTreeLoadEnvelope.*` | P1 |
| `HTreeMaterialization.*` | `HTreeClockTreeObjectBuilder.*` or `HTreeClockTopologyCommit.*` | P1 |
| `HTreeMaterializationContext.hh` | `HTreeClockTopologyBuildContext.hh` | P1 |
| `HTreeAdapterCaches.hh` | `HTreeCharacterizationCache.hh` | P2 |
| `HTreeBuildSummary.cc` | `HTreeSynthesisSummary.cc` | P2 |
| `HTreeComposition.cc` | `HTreeTopologyAssembly.cc` | P2 |
| `HTreeDepthCandidateEvaluation.cc` | `HTreeTopologyDepthEvaluation.cc` | P2 |
| `HTreeDepthExploration.cc` | `HTreeTopologyDepthSearch.cc` | P2 |
| `HTreeSegmentFrontier.cc` | `HTreeSegmentCandidateFrontier.cc` | P2 |
| `SegmentBuilder.*` | `ClockBranchSegmentBuilder.*` or `SourceToRootSegmentBuilder.*` | P1/P2 depending on source-to-root scope |

### Synthesis Internals

| Current | Candidate | Priority |
|---|---|---|
| `ClockSynthesisNetEditor.*` | `ClockTreeTempNetlistBuilder.*` or `ClockTreeNetlistPatch.*` | P2 |
| `ClockSynthesisResultAccounting.*` | `ClockTreeSynthesisMetrics.*` | P2 |
| `ClockSynthesisReporter.*` | Move/rename toward statistics/reporting boundary if still needed | P2 |
| `ClockSynthesisHtreeOptions.*` | keep | Acceptable |
| `ClockSynthesisSinkClustering.*` | keep | Acceptable |
| `ClockSinkTreeSynthesizer.*` | keep | Acceptable |
| `ClockSourceRootSynthesizer.*` | keep | Acceptable |

### Stage/Run Setup

| Current | Candidate | Priority |
|---|---|---|
| `CTSRunEnvironment.*` | `CTSRunSetup.*` or `CTSRunInitializer.*` | P1 with directory rename |
| `FlowManager.*` | `CTSFlowController.*` or `CTSFlowFacade.*` | P2; defer because it is an internal singleton facade with broad caller churn |
| static step wrappers | namespace or owned stage objects | P2; lifecycle cleanup, not only naming |

## Recommended Batch Plan

Batch 1:
- Directory rename: `session` -> `run_setup`, `report_data` -> `clock_tree_view`, `report` -> `visualization`.
- Rename primary view/visualization types that would otherwise conflict semantically with new directories.
- Compile `icts_source_flow`, `icts_source_flow_stage`, visualization/report target, and `icts_test_flow_manager`.

Batch 2:
- H-tree file/class renames after inspecting each candidate file's actual behavior.
- Prefer smaller clusters:
  - load characterization/profile files
  - topology assembly/depth search files
  - inserted object construction files

Batch 3:
- Synthesis internal mechanics and facade cleanup:
  - temp netlist builder names
  - metrics/reporting names
  - `FlowManager` facade if still needed

## Implementation Decisions - 2026-04-30

The approved Option C implementation completed the directory rename and the high-priority file/type rename batch.
Validation intentionally stopped before compile or tests because the required validation order is review first, then compile checks.

Directory outcomes:

| Previous | Implemented |
|---|---|
| `source/flow/session/` | `source/flow/run_setup/` |
| `source/flow/report_data/` | `source/flow/clock_tree_view/` |
| `source/flow/report/` | `source/flow/visualization/` |

View and visualization boundary outcomes:

| Previous | Implemented |
|---|---|
| `CTSRunEnvironment` | `CTSRunSetup` |
| `ClockTreeReportData` | `ClockTreeView` |
| `ClockTreeReportDataBuilder` | `ClockTreeViewBuilder` |
| `ClockTreeReportSynthesisData` | `ClockTreeViewSynthesisInput` |
| `ClockSynthesisReportAdapter` | `ClockTreeViewAdapter` |
| `CTSVisualizationReport` | `ClockTreeSvgVisualization` |
| `CTSGdsReport` | `ClockTreeGdsVisualization` |
| `CTSGdsWriter` | `ClockTreeGdsWriter` |

H-tree and synthesis outcomes:

| Previous | Implemented | Decision basis |
|---|---|---|
| `HTreeActualLoad` | `HTreeSinkLoadProfile` | The code evaluates real sink/load groups at buffered H-tree boundaries, including fanout, capacitance, routing, and cap distribution legality. `HTreeLoadEnvelope` was too broad. |
| `HTreeMaterialization` | `HTreeClockTreeObjectBuilder` | The code builds temporary CTS inst/pin/net objects in `HTreeBuilder::BuildResult`; commit/writeback is owned outside this file. |
| `HTreeMaterializationContext` | `HTreeClockTopologyBuildContext` | The context owns object naming and characterization cache access for H-tree topology object construction. |
| `HTreeAdapterCaches` | `HTreeCharacterizationCache` | The cache stores characterization-derived buffer strength and port data. |
| `HTreeBuildSummary` | `HTreeSynthesisSummary` | The output summarizes selected H-tree synthesis results. |
| `HTreeComposition` | `HTreeTopologyAssembly` | The code assembles topology frontier entries from segment candidates. |
| `HTreeDepthCandidateEvaluation` | `HTreeTopologyDepthEvaluation` | The code evaluates one topology-depth candidate. |
| `HTreeDepthExploration` | `HTreeTopologyDepthSearch` | The code searches candidate topology depths and collects global frontier pools. |
| `HTreeSegmentFrontier` | `HTreeSegmentCandidateFrontier` | The code builds and composes segment candidate frontier sets. |
| `SegmentBuilder` | `SourceToRootSegmentBuilder` | The builder is limited to a clock-source-to-root-input segment; it is not a general branch builder. |
| `ClockSynthesisResultAccounting` | `ClockTreeSynthesisMetrics` | The file records synthesis counters and transfers temporary object ownership. |

Intentional deferral:

- `ClockSynthesisNetEditor` remains unchanged. Source inspection showed it creates temporary CTS objects, reconnects existing nets, and owns `RootNetSideEffectGuard` / `SourceNetSideEffectGuard` rollback helpers. Renaming it to `ClockTreeTempNetlistBuilder` would understate the side-effect restoration role.

Retained by role:

- `CTSClockTreeReportStep` remains the report-stage coordinator for statistics and visualization artifact emission.
- `HTreePatternRegistry::materialize()` remains as a pattern-expansion API; the CTS object construction boundary is now `HTreeClockTreeObjectBuilder`.
