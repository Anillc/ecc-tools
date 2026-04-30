# Industry CTS Terminology Research

## Scope

This note summarizes public industrial and open-source EDA terminology relevant to iCTS flow directory/file naming. It focuses on naming vocabulary, not algorithm correctness.

## Sources

- OpenROAD Clock Tree Synthesis documentation: https://openroad.readthedocs.io/en/latest/main/src/cts/README.html
- mflowgen Innovus CTS node documentation: https://mflowgen.readthedocs.io/en/latest/stdlib-innovus-cts.html
- Cadence Innovus Implementation System datasheet: https://login.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/digital-design-signoff/innovus-implementation-system-ds.pdf
- Cadence Clock Tree Debugger public support page: https://support1.cadence.com/public/docs/content/20485864.html
- Synopsys IC Compiler II public product page: https://www.synopsys.com/implementation-and-signoff/physical-implementation/ic-compiler.html

## Observed Terms

### OpenROAD / TritonCTS

OpenROAD names the module and command `clock_tree_synthesis`. Its public options use these domain terms:

- `root_buffer`
- `tree_buf`
- `sink_clustering`
- `macro_clustering`
- `register tree`
- `macro tree`
- `clock roots`
- `clock subnets`
- `buffers inserted`
- `sinks`
- `clock net repair`
- `non-default rule`
- `sink insertion delay`
- `branching point`
- `levels`

This supports names like `ClockTreeSynthesis`, `ClockTreeRootBuffer`, `SinkCluster`, `MacroTree`, `RegisterTree`, `ClockSubnet`, `ClockNetRepair`, and `TopologyLevel`.

### Cadence Innovus / CCOpt

Public Cadence material emphasizes:

- `clock-tree synthesis (CTS)`
- `clock concurrent optimization`
- `propagated clocks`
- `clock gates`
- `inter-clock paths`
- `clock-tree debugger`
- `FlexH`
- `H-tree`
- `tree structures`
- `insertion delay`
- `power`
- `skew`
- `routing and interconnect optimization`
- `routing convergence`

mflowgen's Innovus CTS documentation also describes the GUI `Clock Tree Debugger` as showing clock gates, buffers, and sinks. This supports `clock_tree_view` / `clock_tree_visualization` as better names than `report_data` / `report` for data and files used to inspect clock-tree structure.

### Synopsys ICC2 / Fusion Compiler Vocabulary

The Synopsys IC Compiler II public page lists `clock tree synthesis`, `routing convergence`, and `concurrent clock and data optimization` in the place-and-route context. This supports keeping terms tied to physical implementation stages rather than generic runtime/session/service names.

## Naming Principles Inferred

- Prefer names that answer "what CTS/physical-design object or flow stage is this?"
- Use `clock tree`, `clock network`, `clock root`, `root buffer`, `sink`, `sink cluster`, `macro tree`, `register tree`, `clock subnet`, `topology`, `level/depth`, `branch`, `segment`, `trunk`, `insertion delay`, `skew`, `routing`, and `visualization`.
- Avoid directory-level names that only describe software mechanics:
  - `session`
  - `report_data`
  - `report`
  - `actual load`
  - `materialization`
  - `adapter cache`
  - `result accounting`
  - `side effect`
- Keep generic terms only for narrow local implementation details when a domain name would be misleading or too long.

## Current-To-Recommendation Table

| Current | Recommended | Rationale |
|---|---|---|
| `source/flow/session/` | `source/flow/run_setup/` | The directory owns setup for one CTS run, not a generic application session. |
| `source/flow/report_data/` | `source/flow/clock_tree_view/` | The data is a readonly view of committed/synthesized clock-tree structure. |
| `source/flow/report/` | `source/flow/visualization/` | Current files are SVG/GDS visualization writers and layer policy. |
| `CTSRunEnvironment` | `CTSRunSetup` or `CTSRunInitializer` | More direct setup semantics; `Environment` is acceptable but broad. |
| `ClockTreeReportData` | `ClockTreeView` or `ClockTreeResultView` | It feeds visualization/report but is not report-only. |
| `ClockTreeReportDataBuilder` | `ClockTreeViewBuilder` | Builder creates the readonly clock-tree view. |
| `ClockTreeReportSynthesisData` | `ClockTreeViewSynthesisInput` | Narrow conversion input from synthesis into the clock-tree view. |
| `ClockSynthesisReportAdapter` | `ClockTreeViewAdapter` | Converts synthesis result into the shared view, not a report-specific artifact. |
| `CTSVisualizationReport` | `ClockTreeSvgVisualization` | Emits SVG visualization; not a generic report. |
| `CTSGdsReport` | `ClockTreeGdsVisualization` | Emits GDS/LYP visualization; not a generic report. |
| `CTSGdsWriter` | `ClockTreeGdsWriter` or `GdsLayoutWriter` | Keep CTS scope if only used by clock-tree visualization. |
| `HTreeActualLoad` | `HTreeSinkLoadProfile` or `HTreeLoadEnvelope` | `ActualLoad` is vague; source behavior appears load-boundary/profile oriented. |
| `HTreeMaterialization` | `HTreeClockTopologyCommit` or `HTreeClockTreeObjectBuilder` | The code builds/commits CTS inst/net topology objects. |
| `HTreeAdapterCaches` | `HTreeCharacterizationCache` | Names the physical/timing data being cached. |
| `HTreeComposition` | `HTreeTopologyAssembly` | Composition is generic; topology assembly is CTS/geometry specific. |
| `HTreeDepthExploration` | `HTreeTopologyDepthSearch` | Clarifies that explored depth is topology depth. |
| `HTreeDepthCandidateEvaluation` | `HTreeTopologyDepthEvaluation` | Clarifies candidate evaluation target. |
| `SegmentBuilder` | `ClockBranchSegmentBuilder` or `SourceToRootSegmentBuilder` | Current name is too broad for buffered clock-tree segments. |
| `ClockSynthesisResultAccounting` | `ClockTreeSynthesisMetrics` | `Accounting` sounds non-EDA; file records synthesis counters/metrics. |

## Recommended First Implementation Scope

Do not attempt all renames in one broad patch unless the user explicitly asks for a large migration.

Recommended first batch:

1. Rename flow directories:
   - `session` -> `run_setup`
   - `report_data` -> `clock_tree_view`
   - `report` -> `visualization`
2. Rename the primary report-data/view model family:
   - `ClockTreeReportData*` -> `ClockTreeView*`
   - `ClockTreeReportSynthesisData` -> `ClockTreeViewSynthesisInput`
   - `ClockSynthesisReportAdapter` -> `ClockTreeViewAdapter`
3. Defer H-tree internals to a second batch after review because names such as `HTreeActualLoad` and `HTreeMaterialization` need file-level behavior confirmation before final names are chosen.

## Implementation Outcome - 2026-04-30

The user approved the large Option C migration, so the implementation applied the directory, view/visualization, H-tree, and synthesis naming cleanup in one batch instead of stopping after the first recommended batch.

Final selected names aligned with the researched CTS vocabulary:

| Previous | Implemented |
|---|---|
| `source/flow/session/` | `source/flow/run_setup/` |
| `source/flow/report_data/` | `source/flow/clock_tree_view/` |
| `source/flow/report/` | `source/flow/visualization/` |
| `CTSRunEnvironment` | `CTSRunSetup` |
| `ClockTreeReportData*` | `ClockTreeView*` |
| `ClockTreeReportSynthesisData` | `ClockTreeViewSynthesisInput` |
| `ClockSynthesisReportAdapter` | `ClockTreeViewAdapter` |
| `CTSVisualizationReport` | `ClockTreeSvgVisualization` |
| `CTSGdsReport` | `ClockTreeGdsVisualization` |
| `CTSGdsWriter` | `ClockTreeGdsWriter` |
| `HTreeActualLoad` | `HTreeSinkLoadProfile` |
| `HTreeMaterialization` | `HTreeClockTreeObjectBuilder` |
| `HTreeMaterializationContext` | `HTreeClockTopologyBuildContext` |
| `HTreeAdapterCaches` | `HTreeCharacterizationCache` |
| `HTreeBuildSummary` | `HTreeSynthesisSummary` |
| `HTreeComposition` | `HTreeTopologyAssembly` |
| `HTreeDepthCandidateEvaluation` | `HTreeTopologyDepthEvaluation` |
| `HTreeDepthExploration` | `HTreeTopologyDepthSearch` |
| `HTreeSegmentFrontier` | `HTreeSegmentCandidateFrontier` |
| `SegmentBuilder` | `SourceToRootSegmentBuilder` |
| `ClockSynthesisResultAccounting` | `ClockTreeSynthesisMetrics` |

Chosen-name rationale:

- `HTreeSinkLoadProfile` was selected instead of `HTreeLoadEnvelope` because the code works with concrete sink-load boundary groups, fanout/cap/routing legality, and cap distribution profiles.
- `HTreeClockTreeObjectBuilder` was selected instead of `HTreeClockTopologyCommit` because the file builds temporary CTS objects; commit ownership remains in the synthesis transaction and netlist writeback layers.
- `SourceToRootSegmentBuilder` was selected instead of `ClockBranchSegmentBuilder` because the builder is source-to-root specific.

Retained or deferred names:

- `ClockSynthesisNetEditor` remains because it also reconnects existing nets and owns rollback guards; `ClockTreeTempNetlistBuilder` would be too narrow.
- `HTreePatternRegistry::materialize()` remains a pattern-expansion term, distinct from CTS clock-tree object building.
