# CTS Engineering Refactor

## Goal

Refactor iCTS into an engineering-grade CTS implementation with stable external behavior and clearer EDA semantics. `CTSAPI` remains the external singleton facade; Tcl, Python, tool-manager, feature-summary, and existing binary callers must keep working without public API changes.

This PRD is the implementation mainline for the current phase. The priority is to remove misleading intermediate abstractions, make CTS role/domain/stage boundaries typed instead of name-string based, and keep synthesis, report, visualization, evaluation, and database writeback responsibilities aligned with CTS business semantics.

## Current Phase Rules

- Do not run `ecc_dev_tools` in this phase.
- Run focused builds/tests while converging.
- After implementation and PRD review, run the requested binary validation:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- If a validation command cannot run, report the exact blocker.

## Stable External Contract

The following public CTS API contract must remain source-compatible:

- `CTS_API_INST`
- `icts::CTSAPI::getInst()`
- `CTSAPI::init(const std::string&, const std::string&)`
- `CTSAPI::runCTS()`
- `CTSAPI::report(const std::string&)`
- `CTSAPI::resetAPI()`
- `CTSAPI::outputSummary()`

The following external call sites must not require API-level changes:

- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`
- `src/interface/python/py_icts/py_icts.cpp`
- `src/feature/builder/feature_builder_tool.cpp`

## Required Flow Semantics

- Initialization resets `Config`, `Design`, `Wrapper`, `FlowManager`, and schema/log state in the established order.
- `runCTS()` performs read-data, synthesis, evaluation, runtime summary, and key-result reporting in the established order.
- `report()` reuses evaluation state only when available and otherwise rebuilds readonly evaluation data before writing report files.
- Synthesis is the only stage that commits CTS-created topology into `Design` and writes final CTS objects back through `Wrapper`/iDB.
- Evaluation is readonly. It evaluates committed CTS results and must not write back to iDB as a side effect.
- Report and visualization are readonly consumers. They may read structured clock-tree report data and readonly `Wrapper` geometry, but must not infer CTS semantics from object-name substrings.
- Failed per-clock synthesis restores source-net/load topology and clears CTS membership for that clock.
- Characterization-library sharing between downstream H-tree synthesis and source-to-root synthesis must remain behavior-compatible.

## Output Contract

- Config no longer carries unused `output_def_dir` or `gds_file` fields if they are confirmed unused.
- Config exposes:
  - `visualization_dir`, initialized as `work_dir / "visualization"`
  - `statistics_dir`, initialized as `work_dir / "statistics"`
- Runtime logs include both derived paths.
- SVG report files are written under `visualization/svg/`.
- GDS/LYP report files are written under `visualization/gds/`.
- Statistics reports are written under `statistics/`.
- GDS report generates:
  - `cts_design.gds`: committed/routed CTS geometry, including FLUTE/routing result segments where available.
  - `cts_flyline.gds`: flyline geometry.
  - `cts_layers.lyp`: only the required semantic layers.
- Required visualization layers:
  - `logic cells` from readonly iDB geometry through `Wrapper`
  - `<clock> regular sinks`
  - `<clock> macro sinks`
  - `<clock> root buffers`
  - `<clock> htree_buffer level <i>`
  - route/flyline layers separated by clock, sink domain, role, and algorithm topology level
- Obsolete or generic layers such as broad text/debug/idb duplicates must be removed.
- Use structured clock-tree report data for clock, synthesis phase, sink domain, instance role, route role, and algorithm level. Do not classify by parsing names.

## Naming Semantics

CTS internal names must use EDA backend terminology instead of generic backend or internet-service terminology. Names should describe clock tree concepts and physical/timing roles such as clock tree, clock tree spec/plan, skew group or sink domain when applicable, source/root/sink, source-to-root trunk, downstream tree, leaf/sink branch, clock path/network, clock cell/buffer/inverter, route/flyline, topology level/depth, committed clock topology, and report database/view. This vocabulary aligns with public Innovus/CCOpt terminology where clock trees are derived from SDC clocks, CTS uses a clock-tree specification/plan as the implementation cookbook, and CCOpt/CTS reporting is organized around clock trees, source pins, sinks, skew groups, source groups, convergence, propagated clocks, CTS cells, power domains, and clock-tree subhierarchy buffers.

## P0 Implementation

P0 items are required for this phase and must be completed before binary validation.

1. Remove wrapper instance snapshots.
   - Remove `WrapperInstKind`, `WrapperInstSnapshot`, and `Wrapper::collectInstSnapshots()`.
   - Replace them with narrow readonly `Wrapper` query APIs that expose the exact geometry/type information report needs.
   - Do not add report-time raw iDB access outside `Wrapper`.

2. Remove synthesis snapshots.
   - Remove `CTSInsertedInstSnapshot`, `CTSInsertedNetSnapshot`, `CTSSynthesisSnapshot`, `ClockTreeReportClock::snapshots`, `ClockTreeReportData::addSnapshot`, and producer/merge logic.
   - Use committed design objects plus typed clock-tree report data for report/evaluation needs.

3. Replace string behavioral boundaries.
   - Remove string sink-domain sentinels such as `kTopSinkDomain = "top"` and string domain fields used for branching.
   - Replace string inputs such as `makeSinkDomainPrefix(..., std::string sink_domain)` with typed CTS concepts.
   - Strings may remain only for object names, log text, file names, or final display labels.

4. Replace H-tree string status/error coding.
   - `HTreeActualLoad` must use typed result/violation values for behavioral decisions.
   - Human-readable strings are diagnostics only.

5. Fix GDS/flyline role semantics.
   - Downstream root route/flyline paths must not be emitted as regular/macro sink instance layers.
   - Regular/macro sink layers are reserved for sink instance geometry.
   - Downstream H-tree root flight paths must use route/flyline path-role layers.

6. Finish output directory/config cleanup.
   - Remove confirmed-unused `output_def_dir` and `gds_file` config plumbing.
   - Add `visualization_dir` and `statistics_dir` initialization and logging.
   - Move SVG/GDS/statistics output paths to the new directory contract.

## P1 Implementation

P1 items are expected unless a blocker is found; any deferred P1 item must be explicitly justified in the final check.

1. Complete a CTS naming audit and rename pass before other P1 module splits.
   - Replace or explicitly justify generic names when they describe CTS domain objects instead of local implementation mechanics.
   - Audit names such as `Artifact`, `RunCoordinator`, `Recorder`, `Session`, `Stage`, `Manager`, `Descriptor`, `Registry`, `Context`, `Result`, and `Options`.
   - Prefer EDA/CTS vocabulary from the Naming Semantics section, including clock tree, clock tree spec/plan, skew group, sink domain, source/root/sink, source-to-root trunk, downstream tree, leaf/sink branch, clock path/network, clock cell/buffer/inverter, route/flyline, topology level/depth, committed clock topology, and report database/view.
   - Do not rename public `CTSAPI` symbols or external Tcl/Python/tool-manager/feature-summary contracts.
2. Make `ClockTreeReportData` lookup clock-scoped and typed instead of global name-only scans. Adopt the naming vocabulary above for any renamed report-data structures.
3. Remove or isolate compatibility enum aliases such as legacy top/source-to-root aliases and sink-domain downstream aliases.
4. Consolidate routed/flyline/fallback segment recording into one typed CTS route/flyline builder or recorder whose name reflects its clock-tree reporting role.
5. Reduce `ClockTreeSynthesisDriver.cc` by extracting sink-domain planning, synthesis commit/rollback, clock-tree route/flyline recording, and summary-row assembly, using CTS domain names for the extracted modules.
6. Consolidate duplicated connectivity/temp-object logic across `ClockSynthesisNetEditor`, `ClockNetEditor`, `SegmentBuilder`, and `HTreeMaterialization` while preserving the algorithm-temp versus final-design commit boundary.
7. Split or narrow over-wide result structs such as `ClockSynthesis::BuildResult` and `HTreeBuilder::BuildResult` when doing so improves semantic clarity without breaking behavior.
8. Keep evaluation-internal semantics clean; compatibility fields for `ClockTreeSummary` should live at API/report adapter boundaries.

### P1 Naming Audit Deferrals

- `FlowManager` remains the established internal singleton facade for this pass. Rename it only with a dedicated facade-migration step because tests and source-layer callers use the name directly.
- `CTSClockDataLoadStep`, `CTSClockTreeSynthesisStep`, `CTSClockTreeWritebackStep`, `CTSClockTreeEvaluationStep`, and `CTSClockTreeReportStep` keep static wrapper shape for now. P2 already tracks converting static stage wrappers to namespaces or owned objects.
- `ClockSynthesisNetEditor`, `SegmentBuilder`, and `HTreeMaterialization` remain for P1 item 6 consolidation because they cross algorithm-temp and final-design commit boundaries.
- `BuildResult`, `BuildOptions`, narrow `Context` structs, and characterization/pattern `Registry` types are accepted as local implementation mechanics unless they become cross-module CTS domain objects.

## P2 Backlog

P2 items are documented follow-ups. Implement them only when they are naturally touched and low risk.

1. Sessionize or explicitly document `ClockTreeEvaluator` latest-state storage.
2. Convert purely static stage wrapper classes to namespaces or owned stage objects where that simplifies lifecycle semantics.
3. Split wide routing facade responsibilities into readonly route/RC builders and side-effecting pin legalizers.
4. Replace sentinel fields in run summaries with `std::optional` where ABI/test risk is acceptable.
5. Retire or isolate legacy config `max_length`.
6. Move `ClockSynthesisReporter` output responsibility toward report/statistics layers when the report contract can own it cleanly.

## Spec Update Requirements

- Update existing backend specs only.
- Do not create a new Flow Architecture spec.
- Remove obsolete spec content that describes snapshots as a preferred architecture.
- Keep spec changes high-level and actionable: architecture boundaries, ownership/writeback rules, typed CTS semantic boundaries, output path contract, and validation expectations.

## Acceptance Criteria

- [x] Public `CTSAPI` interface and external call sites remain source-compatible.
- [x] P0 items are implemented or blocked with a concrete reason.
- [x] P1 items are implemented or explicitly deferred with a concrete reason.
- [x] P1 naming audit is completed, or any remaining generic CTS-domain names are explicitly deferred with concrete reasons.
- [x] Report/evaluation do not perform iDB writeback.
- [x] No CTS behavioral decision depends on parsing object-name strings.
- [x] Wrapper and synthesis snapshot structures listed in P0 are removed.
- [x] GDS/SVG/statistics outputs follow the new visualization/statistics directory contract.
- [x] GDS/LYP layers distinguish logic cells, regular sinks, macro sinks, root buffers, H-tree buffer levels, route roles, flyline roles, clocks, and algorithm levels.
- [x] Focused CTS build/test targets pass after implementation.
- [x] The requested `iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` binary validation passes.
- [x] `ecc_dev_tools` is not run in this phase.

## Out of Scope

- Changing public CTS API names, signatures, singleton macro, or external caller contracts.
- Rewriting CTS algorithms for different optimization results.
- Touching unrelated iEDA modules except when required to keep existing callers compiling.
- Running `ecc_dev_tools` during this phase.
- Replacing iDB/iSTA adapter contracts.
- Changing default runtime behavior outside the requested directory/config/report cleanup.

## Reference Material

- `research/api-flow-orchestration.md`
- `research/synthesis-htree-characterization.md`
- `research/database-netlist-build.md`
