# Current CMake Target and File Granularity Map

Date: 2026-05-01

Scope:
- `src/operation/iCTS/source/flow`
- Related CTS flow / characterization tests were checked only for context.

## CMake Coverage

Existing source-flow `CMakeLists.txt` files:

```text
src/operation/iCTS/source/flow/CMakeLists.txt
src/operation/iCTS/source/flow/evaluation/CMakeLists.txt
src/operation/iCTS/source/flow/instantiation/CMakeLists.txt
src/operation/iCTS/source/flow/report/CMakeLists.txt
src/operation/iCTS/source/flow/setup/CMakeLists.txt
src/operation/iCTS/source/flow/synthesis/CMakeLists.txt
src/operation/iCTS/source/flow/synthesis/htree/CMakeLists.txt
```

Source-flow folders without a local `CMakeLists.txt` / target:

```text
src/operation/iCTS/source/flow/evaluation/metrics
src/operation/iCTS/source/flow/instantiation/design
src/operation/iCTS/source/flow/instantiation/idb
src/operation/iCTS/source/flow/report/export
src/operation/iCTS/source/flow/report/statistics
src/operation/iCTS/source/flow/report/summary
src/operation/iCTS/source/flow/report/visualization
src/operation/iCTS/source/flow/report/visualization/gds
src/operation/iCTS/source/flow/report/visualization/model
src/operation/iCTS/source/flow/report/visualization/svg
src/operation/iCTS/source/flow/synthesis/distribution
src/operation/iCTS/source/flow/synthesis/htree/characterization
src/operation/iCTS/source/flow/synthesis/htree/embedding
src/operation/iCTS/source/flow/synthesis/htree/pattern
src/operation/iCTS/source/flow/synthesis/topology
src/operation/iCTS/source/flow/synthesis/topology/buffer
src/operation/iCTS/source/flow/synthesis/topology/sink
src/operation/iCTS/source/flow/synthesis/topology/trunk
src/operation/iCTS/source/flow/synthesis/trace
```

Diagnosis:
- `flow/synthesis/CMakeLists.txt` directly compiles files from `distribution`, `topology/*`, and `trace`.
- `flow/synthesis/htree/CMakeLists.txt` directly compiles files from `characterization`, `embedding`, and `pattern`.
- `flow/report/CMakeLists.txt` directly compiles files from `summary`, `statistics`, `export`, `visualization/*`, and separately defines `report_visualization_model` in the parent report folder.
- `flow/evaluation/CMakeLists.txt` directly compiles `metrics/ClockTreeEvaluator.cc`.
- `flow/instantiation/CMakeLists.txt` directly compiles `design/DesignConversion.cc` and `idb/IdbConversion.cc`.

Recommendation:
- Each folder target compiles only direct `.cc` files in that folder.
- Parent folder CMake files call `add_subdirectory()` for children and link child targets.
- Use real library targets for folders with `.cc` files and `INTERFACE` targets for header-only folders.
- Preserve current include root `${ICTS_FLOW}` unless target-level include reduction is intentionally handled in a later cleanup.
- Prefer facade-shaped non-leaf folders: direct files should be the main folder entry pair plus `CMakeLists.txt`; remaining implementation should move into business-named child folders.
- Do not force facade shape onto every leaf folder. Cohesive leaf domains may keep multiple direct files when the file names are real CTS concepts and no additional dependency boundary is needed.

## Direct File Distribution

```text
dir|cmake|cc|hh|loc_cc|loc_hh|direct_files
src/operation/iCTS/source/flow|True|1|1|192|76|Flow.cc,Flow.hh
src/operation/iCTS/source/flow/evaluation|True|1|1|68|47|Evaluation.cc,Evaluation.hh
src/operation/iCTS/source/flow/evaluation/metrics|False|1|2|528|171|CTSStatistics.hh,ClockTreeEvaluator.cc,ClockTreeEvaluator.hh
src/operation/iCTS/source/flow/instantiation|True|1|1|43|48|Instantiation.cc,Instantiation.hh
src/operation/iCTS/source/flow/instantiation/design|False|1|1|541|63|DesignConversion.cc,DesignConversion.hh
src/operation/iCTS/source/flow/instantiation/idb|False|1|1|70|46|IdbConversion.cc,IdbConversion.hh
src/operation/iCTS/source/flow/report|True|1|1|80|48|Report.cc,Report.hh
src/operation/iCTS/source/flow/report/export|False|1|1|73|46|ResultExport.cc,ResultExport.hh
src/operation/iCTS/source/flow/report/statistics|False|2|2|215|81|CTSStatisticsWriter.cc,CTSStatisticsWriter.hh,StatisticsReport.cc,StatisticsReport.hh
src/operation/iCTS/source/flow/report/summary|False|1|1|41|38|Summary.cc,Summary.hh
src/operation/iCTS/source/flow/report/visualization|False|1|1|43|47|ClockTreeVisualization.cc,ClockTreeVisualization.hh
src/operation/iCTS/source/flow/report/visualization/gds|False|3|3|790|217|ClockTreeGdsVisualization.cc,ClockTreeGdsVisualization.hh,ClockTreeGdsWriter.cc,ClockTreeGdsWriter.hh,ClockTreeVisualizationLayerPolicy.cc,ClockTreeVisualizationLayerPolicy.hh
src/operation/iCTS/source/flow/report/visualization/model|False|3|4|842|396|ClockTreeView.cc,ClockTreeView.hh,ClockTreeViewBuilder.cc,ClockTreeViewBuilder.hh,ClockTreeViewSynthesisInput.hh,ClockTreeVisualizationModel.cc,ClockTreeVisualizationModel.hh
src/operation/iCTS/source/flow/report/visualization/svg|False|1|1|453|42|ClockTreeSvgVisualization.cc,ClockTreeSvgVisualization.hh
src/operation/iCTS/source/flow/setup|True|1|1|101|39|Setup.cc,Setup.hh
src/operation/iCTS/source/flow/synthesis|True|1|1|153|40|Synthesis.cc,Synthesis.hh
src/operation/iCTS/source/flow/synthesis/distribution|False|3|3|411|183|ClockDistribution.cc,ClockDistribution.hh,ClockTreeSynthesisDriver.cc,ClockTreeSynthesisDriver.hh,TopologySinkClustering.cc,TopologySinkClustering.hh
src/operation/iCTS/source/flow/synthesis/htree|True|1|1|300|163|HTree.cc,HTree.hh
src/operation/iCTS/source/flow/synthesis/htree/characterization|False|2|2|279|138|Characterization.cc,Characterization.hh,CharacterizationLibrary.cc,CharacterizationLibrary.hh
src/operation/iCTS/source/flow/synthesis/htree/embedding|False|1|2|565|113|BufferPortTable.hh,Embedding.cc,EmbeddingState.hh
src/operation/iCTS/source/flow/synthesis/htree/pattern|False|9|6|1958|854|BoundaryConstraints.cc,BoundaryConstraints.hh,BufferStrengthTable.hh,DepthEvaluation.cc,DepthEvaluation.hh,DepthSearch.cc,LevelPlan.cc,Logging.cc,PatternLibrary.hh,PatternSearch.cc,PatternSearch.hh,SegmentFrontier.cc,SinkLoadRegion.cc,SinkLoadRegion.hh,Summary.cc
src/operation/iCTS/source/flow/synthesis/topology|False|1|1|338|147|Topology.cc,Topology.hh
src/operation/iCTS/source/flow/synthesis/topology/buffer|False|1|1|252|91|BufferInsertion.cc,BufferInsertion.hh
src/operation/iCTS/source/flow/synthesis/topology/sink|False|1|1|115|32|SinkBranch.cc,SinkBranch.hh
src/operation/iCTS/source/flow/synthesis/topology/trunk|False|2|2|604|117|SourceTrunk.cc,SourceTrunk.hh,SourceTrunkSegment.cc,SourceTrunkSegment.hh
src/operation/iCTS/source/flow/synthesis/trace|False|5|5|559|241|ClockTreeSynthesisMetrics.cc,ClockTreeSynthesisMetrics.hh,ClockTreeSynthesisStatusTable.cc,ClockTreeSynthesisStatusTable.hh,ClockTreeViewAdapter.cc,ClockTreeViewAdapter.hh,SynthesisTrace.cc,SynthesisTrace.hh,TopologyReporter.cc,TopologyReporter.hh
```

## Pattern Folder Detail

Direct file LOC:

```text
50 BoundaryConstraints.hh
51 Logging.cc
63 BoundaryConstraints.cc
69 DepthSearch.cc
98 DepthEvaluation.cc
108 BufferStrengthTable.hh
113 PatternSearch.hh
115 SinkLoadRegion.hh
117 DepthEvaluation.hh
165 Summary.cc
317 LevelPlan.cc
351 PatternLibrary.hh
357 SegmentFrontier.cc
410 PatternSearch.cc
428 SinkLoadRegion.cc
```

Observations:
- `Logging.cc` contains only `ToCharGridSourceName` and `LogInfoTable`.
  - `ToCharGridSourceName` is a characterization-grid display helper.
  - `LogInfoTable` is a thin schema table wrapper and should not justify a `pattern/Logging.cc` file.
- `Summary.cc` contains `LogSynthesisSummary`.
  - It is not a general report module; it formats the selected H-tree pattern result.
  - It should be renamed around selected pattern reporting or folded into the HTree owner if that keeps `HTree.cc` readable.
- `DepthSearch.cc` only owns the search loop.
- `DepthEvaluation.cc` owns candidate evaluation and global candidate collection.
  - The current split is likely finer than necessary after characterization-grid extraction.
  - A single `DepthSearch.hh/.cc` can own depth candidates, candidate evaluation, and selected-depth summaries.
- `BoundaryConstraints`, `SinkLoadRegion`, `SegmentFrontier`, `PatternLibrary`, and `BufferStrengthTable` are stable domain concepts and should not be merged only because some files are small.
- `PatternSearch.hh` is currently an umbrella header that declares:
  - characterization-grid helpers
  - logging helpers
  - level planning
  - depth search/evaluation
  - pattern assembly
  - sink-load legality
  - embedding helpers
  - root-driver sizing helpers
  This is the highest-impact readability issue in `pattern`, because it hides responsibility boundaries and creates unnecessary include coupling.
- `Characterization.cc` includes `pattern/PatternSearch.hh` only to access grid and logging helpers, which reverses the intended dependency direction.

## Recommended Pattern Responsibility Boundaries

Keep:
- `PatternSearch`: top-level H-tree pattern search and global entry selection.
- `PatternLibrary`: pattern data collections and ID-based access.
- `SegmentFrontier`: segment composition frontier and required-length entry synthesis.
- `SinkLoadRegion`: legality checking for real sink-load region coverage.
- `BoundaryConstraints`: top input slew and boundary feasibility constraints.
- `BufferStrengthTable`: buffer-strength ordering used by pattern construction.
- `LevelPlan`: topology level length alignment and candidate level slicing.

Move out:
- Characterization wirelength-grid resolution -> `htree/characterization`.
- Characterization grid source string conversion -> `htree/characterization`.
- Generic schema table wrapper -> call schema logging directly or keep a private helper local to the file that emits the rows.

Consolidate:
- `DepthEvaluation.hh/.cc` into `DepthSearch.hh/.cc` if the resulting file remains cohesive.
- `Logging.cc` into characterization-local/private helpers and direct schema calls.
- `Summary.cc` to `SelectionSummary.cc` if it remains separate.

## Proposed Implementation Order

1. Add CMake target-per-folder structure without moving `.cc/.hh` files.
2. Move characterization-grid ownership from `pattern` to `characterization`.
3. Remove `Logging.cc` and direct `LogInfoTable` wrapper usage.
4. Consolidate depth-search files.
5. Rename or fold the selected H-tree pattern summary source.
6. Trim `PatternSearch.hh` to the true pattern-search entry contract.
7. Build and run focused tests; defer ECC/dev checks unless explicitly requested.
