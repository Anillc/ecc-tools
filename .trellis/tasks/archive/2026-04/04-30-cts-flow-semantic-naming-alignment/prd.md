# CTS Flow Semantic Naming Alignment

## Goal

Align iCTS flow directory and file names with EDA/CTS business semantics. The first implementation target is the already-identified subflow directory naming mismatch: `session`, `report_data`, and `report`. The second target is a researched naming audit for files/classes under each flow submodule, especially names that sound like generic program mechanics instead of physical-design or CTS flow concepts.

The user approved **Option C** on 2026-04-30: perform the full naming cleanup batch. Implementation may proceed, but the validation order is explicit: implement first, review the diff, then run compile checks, then run the iEDA binary validation, and finally run the full `ecc_dev_tools` check.

## What I Already Know

- The user wants semantic directory cleanup first:
  - `source/flow/session/` is too generic. It owns CTS run setup: config, work directory, log/schema setup, and adapter initialization. Candidate target: `source/flow/run_setup/`.
  - `source/flow/report_data/` is engineering-style wording. It owns readonly clock-tree result/view data: clock, sink domain, inst/net role, route/flyline segment, synthesis phase, topology level, and visualization model. Candidate target: `source/flow/clock_tree_view/`.
  - `source/flow/report/` is too broad. It mostly owns SVG/GDS visualization writers and layer policy; textual statistics are elsewhere. Candidate target: `source/flow/visualization/`.
- The user wants an industry-terminology audit before implementation for files such as `HTreeActualLoad` and `HTreeMaterialization`.
- Public `CTSAPI` must remain stable.
- Implementation must preserve the flow order:

```text
read data -> synthesis/writeback -> evaluation -> report
```

- Recent previous task passed:
  - full `src/operation/iCTS` `ecc_dev_tools`
  - `scripts/design/ics55_dev` iEDA Tcl binary validation
- Current active source directories under `src/operation/iCTS/source/flow/` are:
  - `evaluation`
  - `htree`
  - `netlist`
  - `report`
  - `report_data`
  - `session`
  - `stage`
  - `synthesis`

## Research Summary

See:

- `research/industry-cts-terminology.md`
- `research/current-flow-naming-audit.md`

Key findings from public EDA/CTS references:

- Industry-facing docs consistently use `clock tree synthesis`, `clock tree`, `clock roots`, `root buffer`, `tree buffer`, `sink clustering`, `macro tree`, `register tree`, `clock subnets`, `clock gates`, `clock buffers`, `sinks`, `clock insertion delay`, `clock skew`, `routing convergence`, `H-tree`, and `clock-tree debugger`.
- `session`, `report_data`, `report`, `actual load`, `materialization`, `adapter cache`, `result accounting`, and `side-effect guard` are software-mechanics terms. Some may remain as local implementation mechanics, but directory/file boundaries should prefer CTS flow concepts.
- `visualization` is a better fit than `report` for SVG/GDS visual writers.
- `clock_tree_view` is a better fit than `report_data` for readonly shared clock-tree result/view state.
- `run_setup` is a better fit than `session` for runtime config/work-dir/log/adapter initialization.

## Approved Implementation Plan

### Phase 1: Directory Rename Pass

Rename these directories and all include/CMake/test references in one mechanical pass:

| Current | Proposed | Reason |
|---|---|---|
| `source/flow/session/` | `source/flow/run_setup/` | Run setup is the actual CTS boundary: config, work dir, logging/schema setup, adapter initialization. |
| `source/flow/report_data/` | `source/flow/clock_tree_view/` | The contents are readonly clock-tree result/view models, not just report payloads. |
| `source/flow/report/` | `source/flow/visualization/` | Current files are SVG/GDS visualization writers and GDS layer policy; textual statistics are not owned there. |

Expected touched areas:

- Source includes such as `session/...`, `report_data/...`, `report/...`
- CMake subdirectories and target names
- Source target links where CMake target names change
- Tests and helper includes
- Backend spec directory-structure references
- Task notes and PRD references

Recommended implementation style:

- Use a single rename patch for each directory, then compile before the next rename if possible.
- Prefer changing CMake target names to match the new directories unless target aliasing is needed to stage the migration.
- Avoid compatibility include shim headers unless the blast radius is larger than expected; this is internal source code, so direct migration is preferable.

### Phase 2: File/Class Naming Audit

After the directory rename compiles, apply file/class renames in batches by subflow. Do not rename every generic local helper blindly; rename only boundaries where the new name improves CTS semantics.

High-priority candidates:

| Current | Candidate | Reason |
|---|---|---|
| `HTreeActualLoad.*` | `HTreeSinkLoadProfile.*` or `HTreeLoadEnvelope.*` | `ActualLoad` is vague. The file appears to model feasible/observed sink-load boundary data used by H-tree characterization/candidate selection. |
| `HTreeMaterialization.*` | `HTreeClockTopologyCommit.*` or `HTreeClockTreeObjectBuilder.*` | `Materialization` is software-generic. The code creates CTS inst/pin/net topology objects for the selected H-tree. |
| `HTreeMaterializationContext.hh` | `HTreeClockTopologyBuildContext.hh` | Context remains acceptable only if it is narrow; the file should say it belongs to topology object construction. |
| `HTreeAdapterCaches.hh` | `HTreeCharacterizationCache.hh` or `HTreeTechModelCache.hh` | `AdapterCaches` does not expose the physical-design data being cached. |
| `HTreeBuildSummary.cc` | `HTreeSynthesisSummary.cc` | Align with CTS/H-tree synthesis result vocabulary. |
| `HTreeComposition.cc` | `HTreeTopologyAssembly.cc` | Composition is generic; topology assembly better describes structure construction. |
| `HTreeDepthCandidateEvaluation.cc` | `HTreeTopologyDepthEvaluation.cc` | Clarifies that depth candidates are topology choices. |
| `HTreeDepthExploration.cc` | `HTreeTopologyDepthSearch.cc` | Search/exploration is for candidate topology depths. |
| `HTreeSegmentFrontier.cc` | `HTreeSegmentCandidateFrontier.cc` | Frontier is acceptable algorithm vocabulary, but candidate frontier clarifies role. |
| `SegmentBuilder.*` | `ClockBranchSegmentBuilder.*` or `SourceToRootSegmentBuilder.*` | Current name is too broad; it builds buffered clock segments/trunks. |
| `ClockSynthesisNetEditor.*` | `ClockTreeTempNetlistBuilder.*` or `ClockTreeNetlistPatch.*` | Current name is better than manager, but still generic. The role is temporary CTS netlist object construction/side-effect restoration. |
| `ClockSynthesisResultAccounting.*` | `ClockTreeSynthesisMetrics.*` | Accounting is generic business wording; the code records synthesis counters/topology metadata. |
| `ClockSynthesisReportAdapter.*` | `ClockTreeViewAdapter.*` | Once `report_data` becomes `clock_tree_view`, this adapter should describe conversion into the clock-tree view. |
| `ClockTreeReportData.*` | `ClockTreeView.*` or `ClockTreeResultView.*` | If the directory becomes `clock_tree_view`, the primary model should stop saying report-only. |
| `ClockTreeReportDataBuilder.*` | `ClockTreeViewBuilder.*` | Builder creates the readonly clock-tree view consumed by visualization/report. |
| `ClockTreeReportSynthesisData.hh` | `ClockTreeViewSynthesisInput.hh` | Better describes narrow synthesis-to-view input. |
| `CTSVisualizationReport.*` | `ClockTreeSvgVisualization.*` | It emits SVG visualization, not a generic report. |
| `CTSGdsReport.*` | `ClockTreeGdsVisualization.*` | It emits GDS/LYP visualization. |
| `CTSGdsWriter.*` | `GdsLayoutWriter.*` or `ClockTreeGdsWriter.*` | If kept CTS-specific, include clock-tree scope. |
| `ClockTreeVisualizationLayerPolicy.*` | keep | Good semantic name. |
| `CTSRunEnvironment.*` | `CTSRunSetup.*` or `CTSRunInitializer.*` | If directory becomes `run_setup`, class name should match its setup role. |

Names likely acceptable:

- `ClockTreeEvaluator`
- `CTSStatisticsWriter`
- `ClockSinkDomainBuilder`
- `ClockTreeSynthesisTransaction`
- `ClockTreeSynthesisStatusTable`
- `ClockTreeSynthesisDriver`
- `ClockSinkTreeSynthesizer`
- `ClockSourceRootSynthesizer`
- `ClockSynthesis`
- `ClockSynthesisHtreeOptions`
- `ClockSynthesisSinkClustering`
- `ClockNetEditor`
- `CharacterizationLibrary`
- `HTreeBuilder`
- `HTreeLevelPlan`
- `HTreePatternRegistry`

Approved Option C change surface:

1. Rename directories:
   - `source/flow/session/` -> `source/flow/run_setup/`
   - `source/flow/report_data/` -> `source/flow/clock_tree_view/`
   - `source/flow/report/` -> `source/flow/visualization/`
2. Rename view/visualization boundary files and types:
   - `ClockTreeReportData*` -> `ClockTreeView*`
   - `ClockTreeReportSynthesisData` -> `ClockTreeViewSynthesisInput`
   - `ClockSynthesisReportAdapter` -> `ClockTreeViewAdapter`
   - `CTSVisualizationReport` -> `ClockTreeSvgVisualization`
   - `CTSGdsReport` -> `ClockTreeGdsVisualization`
   - `CTSGdsWriter` -> `ClockTreeGdsWriter`
   - `CTSRunEnvironment` -> `CTSRunSetup`
3. Rename high-priority H-tree and synthesis internals when the target name matches the actual role:
   - `HTreeActualLoad` -> `HTreeSinkLoadProfile` unless source inspection shows `HTreeLoadEnvelope` is more accurate.
   - `HTreeMaterialization` -> `HTreeClockTreeObjectBuilder` unless source inspection shows commit ownership makes `HTreeClockTopologyCommit` more accurate.
   - `HTreeMaterializationContext` -> `HTreeClockTopologyBuildContext`.
   - `HTreeAdapterCaches` -> `HTreeCharacterizationCache`.
   - `HTreeBuildSummary` -> `HTreeSynthesisSummary`.
   - `HTreeComposition` -> `HTreeTopologyAssembly`.
   - `HTreeDepthCandidateEvaluation` -> `HTreeTopologyDepthEvaluation`.
   - `HTreeDepthExploration` -> `HTreeTopologyDepthSearch`.
   - `HTreeSegmentFrontier` -> `HTreeSegmentCandidateFrontier`.
   - `SegmentBuilder` -> a role-specific segment name after source inspection; prefer `ClockBranchSegmentBuilder` if it builds general buffered clock branches, or `SourceToRootSegmentBuilder` if it is source-to-root specific.
   - `ClockSynthesisResultAccounting` -> `ClockTreeSynthesisMetrics`.
   - `ClockSynthesisNetEditor` -> `ClockTreeTempNetlistBuilder` only if source inspection confirms it is limited to temporary synthesis netlist objects; otherwise keep and document the reason.
4. Preserve stable names where the audit already considers them CTS-semantic, including `ClockTreeEvaluator`, `ClockSinkDomainBuilder`, `ClockTreeSynthesisTransaction`, `ClockTreeSynthesisStatusTable`, `ClockTreeSynthesisDriver`, `ClockSynthesis`, `ClockSinkTreeSynthesizer`, `ClockSourceRootSynthesizer`, `ClockSynthesisSinkClustering`, `ClockNetEditor`, `CharacterizationLibrary`, `HTreeBuilder`, `HTreeLevelPlan`, and `HTreePatternRegistry`.

### Phase 3: Decide Structural Reorganization

Only reorganize beyond renames when names reveal a real boundary issue.

Potential structural moves:

- Move visualization model/view files from the current report-data area into `clock_tree_view`.
- Keep format writers under `visualization`.
- Keep textual statistics under `evaluation` unless a separate `statistics` directory becomes necessary.
- Keep `netlist/ClockNetEditor` separate from synthesis if it remains the committed clock-net edit/writeback boundary.
- Keep H-tree algorithm internals under `htree`; do not move algorithm code into `synthesis` just because it is called by synthesis.

## Implementation Pass Status - 2026-04-30

Implementation completed the approved Option C naming cleanup without running compile, iEDA binary validation, tests, or `ecc_dev_tools`.
That stop point is intentional: the user-specified validation order is implementation first, review by another agent, then compile checks, then the iEDA binary, then final `ecc_dev_tools`.

Applied directory and boundary renames:

- `source/flow/session/` -> `source/flow/run_setup/`, with `CTSRunEnvironment` renamed to `CTSRunSetup`.
- `source/flow/report_data/` -> `source/flow/clock_tree_view/`, with `ClockTreeReportData*` renamed to `ClockTreeView*` and `ClockTreeReportSynthesisData` renamed to `ClockTreeViewSynthesisInput`.
- `source/flow/report/` -> `source/flow/visualization/`, with `CTSVisualizationReport` renamed to `ClockTreeSvgVisualization`, `CTSGdsReport` renamed to `ClockTreeGdsVisualization`, and `CTSGdsWriter` renamed to `ClockTreeGdsWriter`.
- `ClockSynthesisReportAdapter` -> `ClockTreeViewAdapter`.

Applied H-tree and synthesis naming decisions:

- `HTreeActualLoad` -> `HTreeSinkLoadProfile`. Source inspection showed the code evaluates sink/load groups at H-tree buffered boundaries, fanout/cap/routing legality, and cap distribution. This is a sink-load profile, not a broad load envelope.
- `HTreeMaterialization` -> `HTreeClockTreeObjectBuilder`. Source inspection showed it builds temporary CTS inst/pin/net objects into `HTreeBuilder::BuildResult`; commit remains owned by the synthesis transaction and clock-net edit/writeback path.
- `HTreeMaterializationContext` -> `HTreeClockTopologyBuildContext`, including the internal context type `HTreeClockTopologyBuildContext`.
- `HTreeAdapterCaches` -> `HTreeCharacterizationCache`.
- `HTreeBuildSummary` -> `HTreeSynthesisSummary`.
- `HTreeComposition` -> `HTreeTopologyAssembly`.
- `HTreeDepthCandidateEvaluation` -> `HTreeTopologyDepthEvaluation`.
- `HTreeDepthExploration` -> `HTreeTopologyDepthSearch`.
- `HTreeSegmentFrontier` -> `HTreeSegmentCandidateFrontier`.
- `SegmentBuilder` -> `SourceToRootSegmentBuilder`. Source inspection showed it builds the single source-to-root segment between the clock source and the selected root input, not general buffered clock branches.
- `ClockSynthesisResultAccounting` -> `ClockTreeSynthesisMetrics`.

Intentional deferrals and retained names:

- `ClockSynthesisNetEditor` was kept. It does more than build temporary netlist objects: it also reconnects existing nets and owns `RootNetSideEffectGuard` / `SourceNetSideEffectGuard` rollback helpers. `ClockTreeTempNetlistBuilder` would hide those side-effect responsibilities.
- `CTSClockTreeReportStep` and `CTSClockTreeReportResult` were kept because this remains the report-stage coordinator that emits statistics and visualization artifacts.
- `HTreePatternRegistry::materialize()` was kept because it expands an abstract topology pattern, not CTS inst/pin/net objects. The CTS object-building boundary is now named `HTreeClockTreeObjectBuilder`.
- Public `CTSAPI` entry point names and external Tcl/Python/tool-manager contracts were not renamed.

## Validation Order

1. Implementation agent finishes the rename and reports changed paths without running final validation.
2. Review agent performs a semantic/code review of the diff before compile checks.
3. After review passes, run focused compile/tests.
4. Run the requested iEDA binary validation:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

5. Run the final full iCTS checker:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Acceptance Criteria

- [x] Task PRD and research are accepted by the user before implementation begins.
- [x] Directory rename plan is approved.
- [x] File/class rename scope is approved, with explicit keep/rename/defer decisions.
- [x] No public `CTSAPI` symbols change.
- [x] No behavioral CTS decisions are introduced during naming migration.
- [x] CMake targets and include paths are updated consistently.
- [ ] Focused build/test targets pass after each migration batch.
- [ ] Final full `src/operation/iCTS` `ecc_dev_tools` check passes.
- [ ] Final iEDA Tcl binary validation passes.

## Out of Scope

- No algorithm behavior changes.
- No changes to external Tcl/Python/tool-manager API contracts.
- No broad module extraction beyond renaming unless the user approves a specific structural change.
