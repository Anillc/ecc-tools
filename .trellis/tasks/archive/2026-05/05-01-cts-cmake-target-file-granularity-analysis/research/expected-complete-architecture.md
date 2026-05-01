# Expected Complete CTS Flow Architecture

Date: 2026-05-01

## Architecture Rule

Use a two-shape directory model.

### Facade folder

Use for orchestration and non-leaf responsibilities.

```text
<folder>/
  CMakeLists.txt
  <MainEntry>.hh
  <MainEntry>.cc
  <business-subfolder>/
```

The folder target compiles only the main entry and links child targets. Direct implementation files other than the main entry are not allowed in a facade folder.

### Domain leaf folder

Use for a cohesive CTS business concept with limited internal surface.

```text
<folder>/
  CMakeLists.txt
  <DomainConcept>.hh
  <DomainConcept>.cc
  <CloselyRelatedConcept>.hh
  <CloselyRelatedConcept>.cc
```

A leaf folder may have multiple files only when all direct files are stable domain concepts and no additional dependency boundary is needed. Generic names such as `Logging`, `Common`, `Helper`, `Utility`, `Manager`, `Handler`, `Processor`, `Context`, `Profile`, `Registry`, `Transaction`, `Editor`, and `Writeback` are not allowed as folder/file concepts.

## Expected Source Tree

```text
src/operation/iCTS/source/
  database/
    design/
      ClockTree.hh
      ClockTree.cc

  flow/
    CMakeLists.txt
    Flow.hh
    Flow.cc

    setup/
      CMakeLists.txt
      Setup.hh
      Setup.cc

    synthesis/
      CMakeLists.txt
      Synthesis.hh
      Synthesis.cc

      distribution/
        CMakeLists.txt
        ClockDistribution.hh
        ClockDistribution.cc

      topology/
        CMakeLists.txt
        Topology.hh
        Topology.cc

        sink/
          CMakeLists.txt
          SinkBranch.hh
          SinkBranch.cc
          SinkLoadPreparation.hh
          SinkLoadPreparation.cc

        trunk/
          CMakeLists.txt
          SourceTrunk.hh
          SourceTrunk.cc
          SourceTrunkSegment.hh
          SourceTrunkSegment.cc

        buffer/
          CMakeLists.txt
          BufferInsertion.hh
          BufferInsertion.cc

      htree/
        CMakeLists.txt
        HTree.hh
        HTree.cc

        characterization/
          CMakeLists.txt
          Characterization.hh
          Characterization.cc

          wirelength/
            CMakeLists.txt
            WirelengthGrid.hh
            WirelengthGrid.cc

          library/
            CMakeLists.txt
            CharacterizationLibrary.hh
            CharacterizationLibrary.cc

        pattern/
          CMakeLists.txt
          PatternSearch.hh
          PatternSearch.cc

          boundary/
            CMakeLists.txt
            BoundaryConstraints.hh
            BoundaryConstraints.cc

          level/
            CMakeLists.txt
            LevelPlan.hh
            LevelPlan.cc
            DepthSearch.hh
            DepthSearch.cc

          segment/
            CMakeLists.txt
            PatternLibrary.hh
            SegmentFrontier.hh
            SegmentFrontier.cc
            BufferStrengthTable.hh

          sink_load/
            CMakeLists.txt
            SinkLoadRegion.hh
            SinkLoadRegion.cc

          selection/
            CMakeLists.txt
            CandidateSelection.hh
            CandidateSelection.cc
            SelectionSummary.hh
            SelectionSummary.cc

        embedding/
          CMakeLists.txt
          Embedding.hh
          Embedding.cc
          EmbeddingState.hh
          BufferPortTable.hh

      trace/
        CMakeLists.txt
        SynthesisTrace.hh
        SynthesisTrace.cc

        status/
          CMakeLists.txt
          SynthesisStatusTable.hh
          SynthesisStatusTable.cc

        metrics/
          CMakeLists.txt
          SynthesisMetrics.hh
          SynthesisMetrics.cc

        view/
          CMakeLists.txt
          SynthesisViewAdapter.hh
          SynthesisViewAdapter.cc

        distance/
          CMakeLists.txt
          TopologyDistanceReport.hh
          TopologyDistanceReport.cc

    instantiation/
      CMakeLists.txt
      Instantiation.hh
      Instantiation.cc

      design_conversion/
        CMakeLists.txt
        DesignConversion.hh
        DesignConversion.cc

      idb_conversion/
        CMakeLists.txt
        IdbConversion.hh
        IdbConversion.cc

    evaluation/
      CMakeLists.txt
      Evaluation.hh
      Evaluation.cc

      metrics/
        CMakeLists.txt
        ClockTreeEvaluator.hh
        ClockTreeEvaluator.cc
        CTSStatistics.hh

    report/
      CMakeLists.txt
      Report.hh
      Report.cc

      summary/
        CMakeLists.txt
        Summary.hh
        Summary.cc

      statistics/
        CMakeLists.txt
        StatisticsReport.hh
        StatisticsReport.cc
        CTSStatisticsWriter.hh
        CTSStatisticsWriter.cc

      export/
        CMakeLists.txt
        ResultExport.hh
        ResultExport.cc

      visualization/
        CMakeLists.txt
        ClockTreeVisualization.hh
        ClockTreeVisualization.cc

        view/
          CMakeLists.txt
          ClockTreeView.hh
          ClockTreeView.cc
          ClockTreeViewBuilder.hh
          ClockTreeViewBuilder.cc
          ClockTreeViewSynthesisInput.hh

        drawing/
          CMakeLists.txt
          ClockTreeDrawing.hh
          ClockTreeDrawing.cc

        svg/
          CMakeLists.txt
          ClockTreeSvgVisualization.hh
          ClockTreeSvgVisualization.cc

        gds/
          CMakeLists.txt
          ClockTreeGdsVisualization.hh
          ClockTreeGdsVisualization.cc

          writer/
            CMakeLists.txt
            ClockTreeGdsWriter.hh
            ClockTreeGdsWriter.cc

          layer/
            CMakeLists.txt
            ClockTreeGdsLayerPolicy.hh
            ClockTreeGdsLayerPolicy.cc
```

## Responsibility Boundaries

### `flow`

Owns the CTS lifecycle only:

```text
setup -> synthesis -> instantiation -> evaluation -> report
```

It must not own H-tree, visualization, report-format, or design-conversion details.

### `setup`

Owns runtime readiness:
- output directory readiness
- schema/log setup
- adapter/config readiness

It must not synthesize topology, instantiate design objects, evaluate QoR, or emit final reports.

### `synthesis`

Owns CTS algorithm execution before committed iDB materialization:
- per-clock / per-sink-domain synthesis orchestration
- sink-domain distribution
- topology formation
- H-tree algorithm execution
- synthesis trace/status/metrics collection

It must not own final report output or committed iDB conversion.

### `synthesis/distribution`

Owns clock source, sink partition, and sink-domain root preparation. It should not own topology clustering. The current `TopologySinkClustering` belongs under `topology/sink` because it prepares H-tree sink loads for topology formation.

### `synthesis/topology`

Owns logical CTS topology formation:
- sink branch H-tree invocation
- source trunk construction
- temporary buffer/net object creation needed for topology synthesis

It may create flow-local CTS objects, but final committed iDB conversion stays in `instantiation`.

### `synthesis/htree`

Owns the H-tree topology-family algorithm:
- characterization
- pattern search
- embedding synthesized patterns into flow-local objects

`HTree.hh/.cc` is the only public entry for callers outside the H-tree folder.

### `synthesis/htree/characterization`

Owns CharBuilder preparation, characterization library reuse, and wirelength-grid resolution.

`WirelengthGrid` is the boundary for:
- requested topology level length collection
- runtime wirelength unit interpretation
- auto-derived grid fallback
- direct characterization length-index selection

This folder must not include `pattern/PatternSearch.hh`.

### `synthesis/htree/pattern`

Owns pattern search only. It is expected to be a facade folder because the current folder already contains multiple independent algorithm concepts.

Child responsibilities:
- `boundary`: top input slew and boundary feasibility constraints.
- `level`: H-tree level planning and topology-depth candidate search.
- `segment`: segment pattern library, segment frontier synthesis, buffer-strength ranking.
- `sink_load`: real sink-load region coverage and legality filtering.
- `selection`: global candidate selection and selected result summary emission.

`PatternSearch.hh` should expose only the pattern-search entry contract needed by `HTree.cc`, not characterization, embedding, or logging helpers.

### `synthesis/htree/embedding`

Owns conversion from selected H-tree pattern to flow-local inserted inst/net/pin objects:
- segment object construction
- buffer port lookup
- root driver sizing validation/application
- redundant leaf-buffer pruning

It should expose `Embedding.hh` instead of exporting functions through `pattern/PatternSearch.hh`.

### `synthesis/trace`

Owns synthesis-time observability:
- structured trace summary
- status table
- synthesis metrics
- visualization view adapter
- topology distance report generated during synthesis

It should be a facade folder because the current direct files are separate observability concepts.

### `instantiation`

Owns materialization into committed design/iDB state.

Child responsibilities:
- `design_conversion`: flow-local CTS result to design database objects.
- `idb_conversion`: design database objects to iDB representation.

### `evaluation`

Owns readonly QoR/metric computation over committed CTS results.

`metrics` owns clock-tree evaluator and CTS statistics data types.

### `report`

Owns final user-facing output after synthesis/instantiation/evaluation:
- summary
- statistics report
- result file export
- clock-tree visualization

It must not mutate synthesis state or instantiate committed design objects.

### `report/visualization`

Owns final visualization output. It is a facade folder because view construction, drawing model generation, SVG, and GDS output have different responsibilities and dependencies.

## CMake Target Pattern

Every folder owns exactly one target. Examples:

```text
flow/synthesis/htree/pattern            -> icts_source_flow_synthesis_htree_pattern
flow/synthesis/htree/pattern/level      -> icts_source_flow_synthesis_htree_pattern_level
flow/synthesis/htree/pattern/selection  -> icts_source_flow_synthesis_htree_pattern_selection
flow/report/visualization/gds/writer    -> icts_source_flow_report_visualization_gds_writer
```

Parent targets:
- compile only their direct main entry `.cc`
- link child targets
- expose child dependencies as `PUBLIC` only when the parent public header exposes child types

Leaf targets:
- compile only direct `.cc` files in that leaf folder
- use `INTERFACE` only when the folder is header-only

## Expected Moves From Current Tree

- Move `TopologySinkClustering.hh/.cc` from `synthesis/distribution` to `synthesis/topology/sink/SinkLoadPreparation.hh/.cc`.
- Move characterization-grid helpers from `htree/pattern/LevelPlan.cc` into `htree/characterization/wirelength/WirelengthGrid.hh/.cc`.
- Remove `htree/pattern/Logging.cc`.
- Move `ToCharGridSourceName` next to `CharGridSource` / `WirelengthGrid`.
- Replace `LogInfoTable` with direct schema calls or private file-local helpers.
- Merge `DepthEvaluation.hh/.cc` into `pattern/level/DepthSearch.hh/.cc`.
- Rename `pattern/Summary.cc` to `pattern/selection/SelectionSummary.cc`.
- Move candidate selection helpers from `PatternSearch.cc` into `pattern/selection/CandidateSelection.cc` if `PatternSearch.cc` remains broad after extracting child responsibilities.
- Add `htree/embedding/Embedding.hh` and stop exposing embedding helpers through `PatternSearch.hh`.
- Split `synthesis/trace` into status, metrics, view, and distance subfolders.
- Move `report/visualization/model/ClockTreeVisualizationModel.*` to `report/visualization/drawing/ClockTreeDrawing.*` if the implementation is only the renderable drawing model.
- Split GDS writer/layer policy under `report/visualization/gds/writer` and `report/visualization/gds/layer` if strict facade shape is applied to visualization outputs.

## Acceptance Criteria For This Architecture

- Every non-empty folder has `CMakeLists.txt`.
- Every folder has exactly one corresponding CMake target.
- Every facade folder contains only `CMakeLists.txt`, one main `.hh/.cc` pair, and child folders.
- Parent targets do not list child `.cc` files.
- `Characterization.cc` does not include `pattern/PatternSearch.hh`.
- `Embedding.cc` does not rely on declarations exported through `PatternSearch.hh`.
- `PatternSearch.hh` does not declare characterization, embedding, logging, or selected-summary helpers.
- No direct file under `htree/pattern` is named `Logging`, `Summary`, `Common`, `Helper`, or `Utility`.
- Leaf folder direct files are business concepts, not implementation mechanics.
