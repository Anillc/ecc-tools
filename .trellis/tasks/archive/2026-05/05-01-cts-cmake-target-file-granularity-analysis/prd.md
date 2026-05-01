# brainstorm: CTS CMake Target and File Granularity Refinement

## Goal

Refine the current CTS flow refactor so the source tree is readable from both the filesystem and the build graph. Every meaningful `source/flow` folder should own a local `CMakeLists.txt` and a corresponding target, while over-fragmented implementation files should be consolidated or renamed around CTS/EDA business concepts instead of generic software terms.

## What I Already Know

- The current accepted CTS flow architecture is `setup -> synthesis -> instantiation -> evaluation -> report`.
- The current source tree already has a clearer first-level flow structure, but CMake is still coarse in several places.
- Existing `source/flow` CMake coverage is limited to:
  - `flow`
  - `flow/setup`
  - `flow/synthesis`
  - `flow/synthesis/htree`
  - `flow/instantiation`
  - `flow/evaluation`
  - `flow/report`
- The following `source/flow` subfolders currently do not own a local `CMakeLists.txt` / target:
  - `evaluation/metrics`
  - `instantiation/design`
  - `instantiation/idb`
  - `report/export`
  - `report/statistics`
  - `report/summary`
  - `report/visualization`
  - `report/visualization/gds`
  - `report/visualization/model`
  - `report/visualization/svg`
  - `synthesis/distribution`
  - `synthesis/topology`
  - `synthesis/topology/sink`
  - `synthesis/topology/trunk`
  - `synthesis/topology/buffer`
  - `synthesis/trace`
  - `synthesis/htree/characterization`
  - `synthesis/htree/embedding`
  - `synthesis/htree/pattern`
- `synthesis/htree/pattern` has 15 direct `.cc/.hh` files and about 2812 LOC. The problem is not only file count; the bigger issue is that some files expose generic implementation concerns (`Logging.cc`, `Summary.cc`) and an umbrella header (`PatternSearch.hh`) declares contracts for many separate responsibilities.
- `synthesis/htree/characterization/Characterization.cc` currently depends on `synthesis/htree/pattern/PatternSearch.hh` to access characterization-grid helpers and logging helpers. That is a boundary smell because characterization should not depend on pattern search internals.

## Requirements

- Each non-empty `src/operation/iCTS/source/flow` folder must have its own `CMakeLists.txt`.
- Each such folder must have a corresponding CMake target.
- Parent targets must compile only their own folder's direct `.cc` files and link child-folder targets instead of listing child-folder source files directly.
- Header-only folders must use an `INTERFACE` target; folders with `.cc` files must use a real library target.
- Target names must follow the repository's hierarchical convention, for example `icts_source_flow_synthesis_htree_pattern`.
- CMake visibility must be strict:
  - default to `PRIVATE`
  - use `PUBLIC` only when dependency headers appear in public headers
  - avoid duplicated include-path wiring when a target link can express the dependency
- File cleanup must be semantic, not mechanical:
  - keep files that own stable CTS concepts or substantial algorithms
  - remove or fold files that are only generic logging wrappers, one-function adapters, or implementation labels
  - avoid new `Common`, `Helper`, `Utility`, `Logging`, `Manager`, `Handler`, `Processor`, `Context`, `Profile`, `Registry`, `Transaction`, `Editor`, or `Writeback` style names
- `htree/characterization` must own characterization-grid concepts.
- `htree/pattern` must own H-tree pattern search concepts only: level candidates, segment frontier, sink-load legality, boundary constraints, and selected pattern result assembly.
- The accepted implementation target is the complete architecture in `research/expected-complete-architecture.md`, including:
  - `instantiation/design_conversion` and `instantiation/idb_conversion`;
  - facade-shaped `htree/pattern`, `synthesis/trace`, and `report/visualization`;
  - `htree/characterization/wirelength` for characterization-grid resolution;
  - `htree/characterization/library` for characterization library ownership;
  - `htree/embedding/Embedding.hh` as the embedding boundary;
  - `synthesis/distribution` as a leaf for `ClockDistribution` only.
- Final naming convergence must remove internal `CTS*`, `ClockTree*`, `ClockTreeSynthesis*`, and `clock_tree_*` names from `source/flow` and shared flow/database models. `CTSAPI` remains the external API boundary unless a separate API-breaking task is created.
- Shared clock topology semantics must use `ClockNetwork` under `source/database/design`.
- Shared clock physical layout/projection data must use `ClockLayout` under `source/database/design`; do not use `View` or `Snapshot` naming for this shared model.
- Flow-local output and evaluation data must use EDA business wording:
  - QoR data lives in `source/database/qor`.
  - Evaluation computation lives under `flow/evaluation/qor`.
  - Report output lives under `flow/report/qor`.
  - Avoid `statistics`, `metrics`, `summary`, `selection`, `preparation`, and `pattern` as new flow directory names.
- HTree topology search internals must be expressed as algorithm responsibilities:
  - `constraint`
  - `plan`
  - `segment_pruning`
  - `region`
  - `topology_pruning`
  - `solution`
- Do not introduce generic software terms such as `Manager`, `Handler`, `Processor`, `Context`, `Profile`, `Registry`, `Transaction`, `Editor`, `Writeback`, `View`, `Snapshot`, `Statistics`, `Metrics`, `Selection`, `Preparation`, or `Pattern` for new internal flow files/classes unless they are part of an existing external API contract.

## Directory Shape Policy

Use two directory shapes instead of applying one rigid rule everywhere.

### Facade Directories

Non-leaf folders that coordinate child responsibilities should contain only:

```text
<FolderName>.hh
<FolderName>.cc
CMakeLists.txt
<business-subfolder>/
```

Rules:
- The root pair is the readable main entry for that folder.
- All non-entry implementation belongs in business-named child folders.
- The folder target compiles only the root pair and links child targets.
- This shape applies to flow-stage and orchestration folders such as `flow`, `synthesis`, `topology`, `htree`, `instantiation`, `evaluation`, `report`, and `visualization` when they have child responsibilities.

### Domain Leaf Directories

Leaf folders may contain multiple files when those files all belong to one coherent CTS concept and further subfolders would be artificial.

Rules:
- Keep direct files when they are stable CTS concepts, for example `BoundaryConstraints`, `SinkLoadRegion`, or `SegmentFrontier`.
- Split a leaf folder when direct files mix different business responsibilities, need different dependency directions, or force generic names such as `Logging`, `Summary`, `Common`, `Helper`, or `Utility`.
- Do not create a child folder only to hold one tiny file unless that folder represents a real business boundary and is expected to grow.
- The folder target remains the build boundary even when the folder contains several cohesive direct files.

This policy keeps the filesystem readable without replacing file fragmentation with directory fragmentation.

## Proposed Phases

### Phase 1: CMake Target Boundary Normalization

Development scope:
- Add missing `CMakeLists.txt` files under every non-empty `source/flow` folder.
- Split existing coarse targets into folder-local targets.
- Wire parent `add_subdirectory()` and `target_link_libraries()` relationships.
- Keep source file placement unchanged in this phase.

Target outcome:
- The CMake graph mirrors the accepted flow architecture and its subfolder boundaries.
- A developer can inspect one folder and immediately find its build boundary.

Acceptance criteria:
- Every non-empty `source/flow` folder has `CMakeLists.txt`.
- Every folder-local `CMakeLists.txt` creates exactly the target for that folder, except parent files that also call `add_subdirectory()` for children.
- No parent target directly lists child-folder `.cc` files.
- The regular iCTS build target compiles.

### Phase 2: HTree Characterization/Pattern Boundary Cleanup

Development scope:
- Move characterization-grid helpers out of `pattern/LevelPlan.cc` / `PatternSearch.hh` and into `htree/characterization`.
- Remove `pattern/Logging.cc` by placing formatting/conversion next to the owning data and using schema logging directly where appropriate.
- Stop `Characterization.cc` from including `pattern/PatternSearch.hh`.

Target outcome:
- `characterization` no longer depends on `pattern` internals.
- Characterization-grid adaptation is visible as a characterization responsibility.

Acceptance criteria:
- `synthesis/htree/characterization/Characterization.cc` does not include `synthesis/htree/pattern/PatternSearch.hh`.
- Characterization-grid functions and types are declared by characterization-owned headers.
- `pattern/Logging.cc` is removed or fully emptied from the build graph.
- Focused build passes.

### Phase 3: Pattern File Granularity Cleanup

Development scope:
- Consolidate overly small or generic files in `synthesis/htree/pattern`.
- Replace broad umbrella declarations in `PatternSearch.hh` with a smaller module entry contract and local subdomain headers only where cross-file contracts require them.
- Merge `DepthEvaluation.cc` into the depth-search owner if the implementation stays cohesive after the characterization-grid extraction.
- Rename or relocate `Summary.cc` so the file name reflects selected H-tree pattern reporting, not a generic report concept.

Target outcome:
- `pattern` reads as a compact set of H-tree pattern-search responsibilities rather than a bag of helper files.
- The file list communicates business concepts before implementation mechanics.

Acceptance criteria:
- No `Logging.cc` remains under `htree/pattern`.
- The remaining `pattern` files each map to a recognizable CTS/HTree concept.
- `PatternSearch.hh` no longer declares characterization-grid helpers, embedding helpers, and unrelated logging helpers.
- Focused HTree and characterization tests compile and pass where available.

### Phase 4: Validation and PRD Update

Development scope:
- Build the affected iCTS targets.
- Run focused tests that cover HTree, characterization, and flow synthesis.
- Update this PRD with phase completion notes after each phase.
- Build the full `iEDA` binary.
- Run `scripts/design/ics55_dev` with `./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`.
- After the Tcl flow passes, run full iCTS ECC/dev validation and keep fixing in-scope findings until the check passes.

Acceptance criteria:
- Each completed phase records status and any deviation in this PRD.
- The final implementation has no known CMake target drift against the recommended architecture.
- Full build, iCTS dev Tcl run, and ECC/dev validation pass.

### Phase 5: Final Naming and Data-Boundary Convergence

Development scope:
- Rename the shared database model from `ClockTree` to `ClockNetwork`.
- Rename the shared visualization/report projection from `ClockTreeView` to `ClockLayout`.
- Move the shared clock layout model out of report visualization ownership and into `source/database/design`.
- Move evaluation/report QoR result data out of flow-local statistics/metrics ownership and into `source/database/qor`.
- Rename flow facade classes and result structs to remove internal `CTS*` prefixes, including setup, synthesis, instantiation, evaluation, report, and run result types.
- Rename synthesis trace/status/result files to remove `ClockTreeSynthesis*` and generic `metrics/view` folders.
- Replace `flow/synthesis/htree/pattern` with the accepted HTree algorithm boundary:
  - `constraint`
  - `plan`
  - `segment_pruning`
  - `region`
  - `topology_pruning`
  - `solution`
- Rename `SinkLoadPreparation` to a CTS-business term such as `SinkLoadClustering`.
- Clean report naming so overview, QoR, export, and visualization responsibilities are readable without `statistics`, `metrics`, `summary`, or `view` ambiguity.
- Update CMake targets and test references for every moved or renamed folder.
- Update this PRD with phase completion notes after implementation.

Target outcome:
- Source tree names describe CTS/physical-design responsibilities rather than legacy prefixes or generic software roles.
- Shared data boundaries are explicit:
  - `ClockNetwork` owns clock-network semantic roles and lifecycle.
  - `ClockLayout` owns physical/geometric layout data used by visualization/export/report.
  - `Qor` owns stable evaluation result data shared by evaluation and report.
- HTree internals read as an algorithm pipeline instead of a generic pattern folder.

Acceptance criteria:
- No internal file, class, enum, struct, or CMake target under `src/operation/iCTS/source/flow` or shared flow/database models contains `CTS*`, `ClockTree*`, `ClockTreeSynthesis*`, or `clock_tree_*`, excluding comments/log strings where the literal product name is user-facing and excluding external `api/CTSAPI`.
- No flow folder remains named `view`, `statistics`, `metrics`, `selection`, `preparation`, or `pattern`.
- `source/database/design` contains `ClockNetwork.hh/.cc` and `ClockLayout.hh/.cc`.
- `source/database/qor` contains the shared QoR data model consumed by evaluation and report.
- HTree topology search folders are `constraint`, `plan`, `segment_pruning`, `region`, `topology_pruning`, and `solution`.
- Every non-empty new or renamed folder owns a local `CMakeLists.txt` and folder-local target.
- The regular build, iCTS dev Tcl flow, and final ECC/dev validation pass.

## Recommended Naming Scheme

Source target names:

```text
icts_source_flow
icts_source_flow_setup
icts_source_flow_synthesis
icts_source_flow_synthesis_distribution
icts_source_flow_synthesis_topology
icts_source_flow_synthesis_topology_sink
icts_source_flow_synthesis_topology_trunk
icts_source_flow_synthesis_topology_buffer
icts_source_flow_synthesis_trace
icts_source_flow_synthesis_trace_domain_status
icts_source_flow_synthesis_trace_distance
icts_source_flow_synthesis_htree
icts_source_flow_synthesis_htree_characterization
icts_source_flow_synthesis_htree_characterization_wirelength
icts_source_flow_synthesis_htree_characterization_library
icts_source_flow_synthesis_htree_constraint
icts_source_flow_synthesis_htree_plan
icts_source_flow_synthesis_htree_segment_pruning
icts_source_flow_synthesis_htree_region
icts_source_flow_synthesis_htree_topology_pruning
icts_source_flow_synthesis_htree_solution
icts_source_flow_synthesis_htree_embedding
icts_source_flow_instantiation
icts_source_flow_instantiation_design_conversion
icts_source_flow_instantiation_idb_conversion
icts_source_flow_evaluation
icts_source_flow_evaluation_qor
icts_source_flow_report
icts_source_flow_report_overview
icts_source_flow_report_qor
icts_source_flow_report_export
icts_source_flow_report_visualization
icts_source_flow_report_visualization_drawing
icts_source_flow_report_visualization_svg
icts_source_flow_report_visualization_gds
icts_source_flow_report_visualization_gds_writer
icts_source_flow_report_visualization_gds_layer
icts_source_database_design
icts_source_database_qor
```

Accepted HTree topology search shape after final naming convergence:

```text
htree/
  HTree.hh
  HTree.cc
  constraint/
    Constraint.hh
    Constraint.cc
  plan/
    Plan.hh
    Plan.cc
    DepthPlan.hh
    DepthPlan.cc
  segment_pruning/
    SegmentPruning.hh
    SegmentPruning.cc
    SegmentLibrary.hh
    SegmentLibrary.cc
    BufferStrength.hh
  region/
    Region.hh
    Region.cc
    SinkLoadRegion.hh
    SinkLoadRegion.cc
  topology_pruning/
    TopologyPruning.hh
    TopologyPruning.cc
    TopologyLibrary.hh
    TopologyLibrary.cc
  solution/
    Solution.hh
    Solution.cc
    SolutionReport.hh
    SolutionReport.cc
```

Accepted characterization shape:

```text
characterization/
  CMakeLists.txt
  Characterization.hh
  Characterization.cc
  wirelength/
    WirelengthGrid.hh
    WirelengthGrid.cc
  library/
    CharacterizationLibrary.hh
    CharacterizationLibrary.cc
```

`WirelengthGrid` is preferred over generic `GridPlan` because it names the CTS characterization lattice being resolved. If implementation review shows it should remain internal to `Characterization.cc`, keep it private instead of adding files.

## Out of Scope

- Changing the accepted first-level CTS flow architecture.
- Reintroducing compatibility aliases for previous target or file names.
- Moving stable shared database types into flow-local folders.

## Technical Notes

- Detailed scan results are stored in `research/current-cmake-target-and-file-granularity-map.md`.
- The expected complete target/file architecture is stored in `research/expected-complete-architecture.md`.
- Relevant specs:
  - `.trellis/spec/backend/directory-structure.md`
  - `.trellis/spec/backend/quality-guidelines.md`
  - `.trellis/spec/backend/logging-guidelines.md`
  - `.trellis/spec/guides/code-reuse-thinking-guide.md`

## Phase Completion Log

- Phase 0 research: completed on 2026-05-01. No source code changed.
- Phase 1 CMake target boundary normalization: implemented on 2026-05-01. Every non-empty `source/flow` folder now owns a local `CMakeLists.txt` and target, and parent targets compile direct folder sources while linking child targets.
- Phase 2 HTree characterization/pattern boundary cleanup: implemented on 2026-05-01. Characterization-grid logic moved under `htree/characterization/wirelength`, characterization library moved under `htree/characterization/library`, `Characterization.cc` no longer depends on `pattern/PatternSearch.hh`, `Logging.cc` was removed, and embedding now exposes `Embedding.hh`.
- Phase 3 pattern/file granularity cleanup: implemented on 2026-05-01. `htree/pattern` is now a facade with `boundary`, `level`, `segment`, `sink_load`, and `selection` child targets; depth evaluation was merged into `level/DepthSearch`; selected-result summary moved to `selection/SelectionSummary`.
- Additional architecture convergence: implemented on 2026-05-01. `synthesis/trace` and `report/visualization` were facade-split; `instantiation/design` and `instantiation/idb` were renamed to `design_conversion` and `idb_conversion`; `distribution` now contains only `ClockDistribution`, with per-clock synthesis coordination folded into `Synthesis.cc`.
- Phase 4 validation: completed on 2026-05-01. Full configure/build and flow validation passed:
  - `cmake -S . -B build`
  - `cmake --build build --target icts_source_flow -j4`
  - `cmake --build build --target iEDA -j4`
  - `cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
  - `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
  Final ECC/dev validation reported zero in-scope findings for `format`, `tidy`, `headers`, `cmake`, and `iwyu`. During validation, CMake link visibility was tightened so public dependencies now reflect public-header exposure, `SegmentFrontier` links directly to `icts_source_module_characterization`, and `ClockDistribution.hh` no longer exposes synthesis-status-table internals.
- Phase 5 final naming and data-boundary convergence: implemented on 2026-05-01. Shared topology data now lives in `database/design/ClockNetwork.*`, shared layout projection in `database/design/ClockLayout*`, and shared QoR data in `database/qor/Qor.hh`; flow evaluation/report use `evaluation/qor/QorEvaluation.*` and `report/qor/QorReport.*` / `QorFiles.*`. Report `summary` is now `overview`, visualization files are `Visualization`, `Drawing`, `SvgVisualization`, `GdsVisualization`, `LayerPolicy`, and `GdsStream`, HTree topology search folders are `constraint`, `plan`, `segment_pruning`, `region`, `topology_pruning`, and `solution`, sink-load preparation is now `SinkLoadClustering`, and synthesis trace data is split into `domain_status`, `topology_result`, `layout`, and `distance`. Focused file/folder/type/target scans were added to validation for the forbidden internal naming forms, and `cmake --build build --target icts_source_flow -j4` passed.
- Phase 5 validation closure: completed on 2026-05-01. `ClockLayoutBuilder.*` and `ClockLayoutSynthesisInput.hh` were moved from `database/design` into `flow/synthesis/trace/layout` because they construct synthesis layout projection and call routing; `database/design` now remains a stable data-model target without a dependency on `source/module/routing`. CMake visibility was tightened for flow/report QoR and HTree solution targets, the Phase 5 files were formatted, and `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` reported zero in-scope findings for `format`, `tidy`, `headers`, `cmake`, and `iwyu`.
- Final validation: completed on 2026-05-01. `cmake --build build --target iEDA -j4` passed, `cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` completed with `iCTS run successfully` and report generation finished for statistics, SVG, and GDS outputs, and the final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` pass reported zero in-scope findings.
