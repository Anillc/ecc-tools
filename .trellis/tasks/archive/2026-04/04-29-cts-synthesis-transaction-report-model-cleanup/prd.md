# CTS Synthesis Transaction and Report Model Cleanup

## Goal

Continue the iCTS engineering refactor by tightening the synthesis transaction boundary, layering synthesis results, unifying visualization data, making evaluation state explicit, and cleaning remaining legacy config/facade naming issues. The change is a deep cleanup task, not an algorithm behavior change: public CTS entry points, generated clock-tree semantics, iDB writeback ownership, and existing Tcl/Python/tool-manager callers must remain compatible.

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

## Architectural Constraints

- Preserve the physical-design stage order:

```text
read data -> synthesis/writeback -> evaluation -> report
```

- Synthesis/writeback is the only stage that may commit CTS-created topology into `Design` or write final CTS objects through `Wrapper`/iDB.
- Evaluation, report, and visualization are readonly consumers of committed CTS results and typed report metadata.
- Do not classify CTS behavior by object-name substrings. Strings may be used for object names, log text, display labels, file names, and diagnostics only.
- Keep raw iDB pointers inside `Wrapper`; report/visualization code may use narrow readonly `Wrapper` geometry queries.
- Avoid broad snapshots that duplicate data already available from `Design`, `Clock`, `Inst`, `Net`, report metadata, or narrow `Wrapper` queries.
- Use EDA/CTS terms for domain code: clock tree, sink domain, source-to-root, downstream tree, root buffer, inserted insts/nets, routing segment, flyline segment, topology level/depth, committed topology.
- Do not run `ecc_dev_tools` during the implementation loop. Use compile/focused tests while converging; reserve full `src/operation/iCTS` `ecc_dev_tools` validation for finish-work.

## Current Code Analysis

- `ClockTreeSynthesisDriver` still owns several responsibilities in one translation unit: sink-domain construction, root-buffer/downstream-net setup, downstream synthesis, source-to-root synthesis, commit/rollback, run-summary accounting, and flow-row assembly.
- `ClockSynthesis::BuildResult` mixes algorithm data, materialized temporary CTS objects, counters, diagnostics, and report-specific metadata. `SourceToRootBuildResult` has the same pattern.
- `HTreeBuilder::BuildResult` is still a wide H-tree carrier that mixes topology/characterization results, diagnostics, temporary materialized CTS objects, and report/rendering metadata.
- `ClockTreeReportDataBuilder` publicly depends on `synthesis/ClockSynthesis.hh` because it consumes `ClockSynthesis::BuildResult` and `SourceToRootBuildResult` directly.
- SVG and GDS report code each prepare their own view/fallback/layer logic. They should consume a shared normalized clock-tree visualization model.
- `ClockTreeEvaluator` stores latest summary/statistics in static function-local state. This makes report-time evaluation reuse implicit instead of session-owned.
- `FlowManager` remains a generic internal facade name from the previous pass. It may stay if a rename causes broad churn, but the task must evaluate whether a CTS-domain facade name is now practical.
- `max_length` remains as a legacy compatibility config field. It should be isolated or removed only after confirming it is not part of the active characterization lattice contract.

## Implementation Mainline

### 1. Synthesis Transaction Split

Extract explicit synthesis-stage ownership from `ClockTreeSynthesisDriver` without changing generated CTS behavior.

Required work:

- Extract `ClockSinkDomainBuilder` or an equivalent CTS-domain type that prepares per-clock sink domains.
- The sink-domain builder owns:
  - partitioned regular/macro sink domain preparation input,
  - root-buffer insertion for a sink domain,
  - downstream net creation for that sink domain,
  - typed sink-domain context output.
- Extract `ClockTreeSynthesisTransaction` or an equivalent CTS-domain transaction owner.
- The transaction owner owns:
  - per-clock rollback boundaries,
  - temporary synthesized object commit order,
  - report-data merge only after successful commit,
  - source-to-root commit failure handling,
  - clear failure ownership for prepared-but-uncommitted CTS objects.
- Extract the flow row/status table builder from synthesis logic.
- Flow rows must use typed status/phase/domain inputs internally and stringify only at schema output.
- Preserve current failure recovery semantics: failed per-clock synthesis restores source-net/load topology and clears CTS membership for that clock.

Required narrow tests:

- Root-buffer insertion failure rolls back the clock and records the expected failed sink-domain status.
- Downstream net creation failure rolls back the clock and records the expected failed sink-domain status.
- Inserted-object commit failure restores source/load topology and does not merge pending report data.
- Source-to-root failure restores downstream preparation/synthesis side effects for the clock and records the expected source-to-root failure status.

### 2. BuildResult Layering

Split wide synthesis result contracts by lifetime and ownership boundary.

Required work:

- Separate algorithm result from materialized objects, diagnostics, and report metadata.
- Keep algorithm-local temporary `Inst`, `Pin`, and `Net` ownership inside materialization/transaction result objects until commit.
- Define a narrow report-data input model so `ClockTreeReportDataBuilder` no longer publicly depends on `ClockSynthesis::BuildResult`.
- Keep `ClockSynthesis` public result contracts source-compatible only where tests and internal callers still need transition time; prefer adding typed views/adapters before removing fields.
- For source-to-root, preserve the distinction between segment and H-tree synthesis phase with typed enums.
- Keep H-tree topology level/depth data available to report/visualization before it becomes hard to reconstruct from committed objects.

Target layering:

- Algorithm result: selected H-tree depth/level plan, topology, characterization choice, cluster result, selected pattern/route metadata.
- Materialized objects: temporary inserted insts, pins, nets, root inst/pin/net links, inserted inst/net topology levels.
- Diagnostics: success/failure, failure level/length index, fallback use/reason, candidate counts, summary counters.
- Report metadata: sink domain, synthesis phase, net role, inst role, route/flyline role, topology level/depth, clock index/name labels.

### 3. Unified Visualization Model

Make SVG and GDS consume the same normalized report model.

Required work:

- Introduce `ClockTreeVisualizationModel` or an equivalent normalized model under the report/report-data boundary.
- Centralize fallback segment handling in one place.
- Split palette/layer policy from geometry/model construction.
- SVG and GDS must share clock, sink-domain, inst-role, net-role, route-role, flyline-role, and topology-level classification.
- Keep iDB logic-cell geometry acquisition behind readonly `Wrapper` APIs.
- Keep output layout unchanged from the previous task:
  - SVG under `visualization/svg/`
  - GDS and LYP under `visualization/gds/`
  - statistics under `statistics/`
- GDS visualization emits paired files per view:
  - `visualization/gds/cts_design.gds`
  - `visualization/gds/cts_design.lyp`
  - `visualization/gds/cts_flyline.gds`
  - `visualization/gds/cts_flyline.lyp`
- `cts_design.lyp` describes the semantic layers registered while building `cts_design.gds`; `cts_flyline.lyp` describes the semantic layers registered while building `cts_flyline.gds`. Do not collapse the two views back into a single shared `cts_layers.lyp` contract.

Required visualization layer semantics:

- `logic cells` from iDB geometry through `Wrapper`, using a muted visual style.
- `<clock> regular sinks`
- `<clock> macro sinks`
- `<clock> root buffers`
- `<clock> htree_buffer level <i>`
- Routed/flyline layers distinguished by clock, sink domain, route/flyline role, synthesis phase, and topology level where available.

### 4. Sessionize Evaluation

Remove implicit static latest-state storage from evaluation.

Required work:

- Replace `ClockTreeEvaluator` static latest summary/statistics state with an explicit evaluation session/state object.
- The run/report flow must explicitly own or pass evaluation state.
- `report()` must either reuse available evaluation state from the active run session or rebuild readonly evaluation state in a clearly named path.
- `ClockTreeSummary` compatibility fields may remain at API/report adapter boundaries, but core evaluation code should prefer CTS-semantic fields.
- Statistics writing must consume explicit evaluation state rather than querying hidden static state.

### 5. Legacy Config and Facade Naming Cleanup

Clean up remaining compatibility and naming debt after the structural work is stable.

Required work:

- Isolate or delete `max_length` after confirming whether it is still required for config compatibility or tests.
- Clean default config fields that no longer reflect the active output contract.
- Evaluate renaming `FlowManager` to a CTS-domain facade name. If deferred, record the concrete migration reason.
- Convert purely static step wrappers to namespaces or owned stage objects where it improves lifecycle semantics. If deferred, record the concrete reason.
- Continue the naming policy from the previous task: avoid generic service/backend names for CTS-domain concepts.

## Testing and Validation Plan

During implementation:

- Build focused iCTS targets after structural splits and CMake changes.
- Add narrow tests for the four synthesis transaction failure modes listed above before broad refactors rely on the new boundary.
- Add or update report/visualization tests so SVG/GDS use the same normalized model and layer classification.
- Add or update evaluation/report tests for explicit evaluation state reuse/rebuild behavior.
- Do not run `ecc_dev_tools` during the edit loop.

Final validation:

- Run the relevant focused build/test set and fix in-scope failures.
- Run the requested binary validation after compile is stable:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- In finish-work, run the full iCTS checker required by project constraints:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Stabilization Status - 2026-04-29

This stabilization pass converged the partially edited diff to a focused compile/test handoff before `ecc_dev_tools` or Tcl binary validation.

Completed in this stabilization pass:

- `ClockTreeReportDataBuilder.hh` no longer publicly includes or depends on `synthesis/ClockSynthesis.hh`; synthesis result conversion now goes through `ClockSynthesisReportAdapter` and the narrow report input structs in `ClockTreeReportSynthesisData.hh`.
- `ClockTreeSynthesisDriver` delegates sink-domain preparation to `ClockSinkDomainBuilder`, transaction commit/rollback and source-to-root ownership to `ClockTreeSynthesisTransaction`, and row assembly to `ClockTreeSynthesisStatusTable`.
- New synthesis transaction split files are wired in the flow-stage CMake target, and `ClockSynthesisReportAdapter` is wired in the synthesis CMake target.
- Evaluation state now flows explicitly through `FlowManager`, `CTSClockTreeEvaluationStep`, `CTSClockTreeReportStep`, and `ClockTreeEvaluator`; report reuse/rebuild is based on the owned `ClockTreeEvaluationState`.
- SVG and GDS consume the normalized `ClockTreeVisualizationModel`; GDS layer assignment is split into `ClockTreeVisualizationLayerPolicy`.
- Focused tests cover root-buffer insertion failure, downstream net creation failure, inserted-object commit failure, and source-to-root failure rollback/status behavior.

Partial or intentionally deferred:

- `max_length` remains deferred. It is still part of legacy config compatibility and may be tied to characterization/runtime config expectations; removing it during this stabilization pass would risk broad config churn unrelated to restoring compile/test health.
- `FlowManager` remains deferred. It is the established internal singleton facade used by source-layer callers and tests; renaming it requires a dedicated facade-migration pass to avoid unnecessary churn while preserving public `CTSAPI` behavior.
- Static step wrappers remain deferred. The evaluation/report steps now pass explicit session state, but converting all static step classes to namespaces or owned stage objects is lifecycle cleanup beyond this scoped stabilization.
- Broad legacy config/default cleanup remains deferred unless it blocks compile or the focused transaction/report-model behavior.
- The requested current handoff stops after focused compile/tests. Tcl binary validation and final full `ecc_dev_tools` validation are intentionally not run in this pass per the current user instruction.

## Narrow Follow-up - 2026-04-29

User feedback added two constraints for this follow-up:

- GDS visualization must emit separate LYP files for the design and flyline GDS views. The active output contract is now `cts_design.gds` + `cts_design.lyp` and `cts_flyline.gds` + `cts_flyline.lyp` under `visualization/gds/`. The GDS filenames remain unchanged.
- The `source/flow` subdirectory names `session`, `report_data`, and `report` need CTS/EDA semantic review, but broad directory migration is deferred from this pass.

Directory naming recommendation:

- Rename `source/flow/session/` to `source/flow/run_setup/`. The directory owns config, work directory, logging/schema setup, and adapter initialization for a CTS run. `run_setup` is more precise than `session`; `environment` is acceptable for the class name `CTSRunEnvironment` but is too generic as the directory boundary.
- Rename `source/flow/report_data/` to `source/flow/clock_tree_view/`. The code is a readonly clock-tree result/view model consumed by report, visualization, synthesis adapters, netlist helpers, and flow state. `topology_view` is too narrow because the model also carries sink domain, synthesis phase, net/inst role, route/flyline role, and clock labels. `clock_tree_result_view` is accurate but verbose for a directory name.
- Rename `source/flow/report/` to `source/flow/visualization/` if textual statistics remain in evaluation/report-step code. The current files in this directory are SVG/GDS visualization writers and layer policy. The name `report` is too broad once textual statistics and evaluation summaries are owned elsewhere.

Migration plan and deferral reason:

- Current blast radius is not limited to local files: `session` is referenced by flow/API includes and CMake target names; `report_data` appears in flow public headers, synthesis/stage/netlist includes, tests, and several target links; `report` is linked by stage/flow and currently contains the GDS/SVG writer APIs. The grep audit found references across source, API, tests, CMake, specs, and task notes.
- Defer the actual directory rename to a dedicated migration pass so it can update directories, include paths, CMake variables/targets, optional compatibility aliases, tests, and backend spec text together. This avoids mixing a mechanical tree move with the narrow LYP output fix and reduces the risk of losing compile signal in the existing uncommitted refactor diff.
- Suggested migration order: first move `session` to `run_setup` and compile; then move `report_data` to `clock_tree_view` with temporary CMake aliases if downstream targets need a staged transition; finally move `report` to `visualization`, checking the existing `source/utils/visualization` include paths to avoid target/include ambiguity.

## Acceptance Criteria

- [ ] Public `CTSAPI` interface and external call sites remain source-compatible.
- [ ] `ClockTreeSynthesisDriver` no longer owns sink-domain building, commit/rollback transaction details, and flow-row assembly in one unit.
- [ ] Root-buffer insertion failure, downstream net creation failure, commit failure, and source-to-root failure have narrow tests.
- [ ] Failed per-clock synthesis restores source-net/load topology and clears CTS membership.
- [ ] Pending report data is merged only after inserted CTS objects commit successfully.
- [ ] `ClockTreeReportDataBuilder` no longer publicly depends on `ClockSynthesis::BuildResult`.
- [ ] Synthesis result data is layered by algorithm result, materialized objects, diagnostics, and report metadata.
- [ ] SVG and GDS consume a shared normalized clock-tree visualization model.
- [ ] Visualization fallback logic is centralized.
- [ ] Palette/layer policy is independent from visualization geometry/model construction.
- [ ] GDS visualization emits view-specific LYP files: `cts_design.lyp` for `cts_design.gds` and `cts_flyline.lyp` for `cts_flyline.gds`.
- [ ] Evaluation state is explicitly owned or passed by the flow/report path; `ClockTreeEvaluator` no longer relies on hidden static latest-state storage.
- [ ] `max_length` and remaining old config defaults are isolated, deleted, or explicitly deferred with reasons.
- [ ] `FlowManager` and static step wrapper cleanup is completed or explicitly deferred with concrete migration reasons.
- [ ] No new CTS behavioral decision depends on parsing object-name strings.
- [ ] Synthesis/writeback remains the only iDB/design writeback owner; evaluation/report/visualization stay readonly.
- [ ] Focused CTS build/test targets pass.
- [ ] Requested `iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` binary validation passes.
- [ ] Final full `src/operation/iCTS` `ecc_dev_tools` validation passes in finish-work.

## Out of Scope

- Changing public CTS API names, signatures, singleton macro, or external caller contracts.
- Rewriting CTS algorithms for different optimization results.
- Changing emitted clock-tree object names unless required by a structural boundary and explicitly reviewed.
- Moving raw iDB access out of `Wrapper`.
- Touching unrelated iEDA modules except when required to keep existing CTS callers compiling.
- Adding new global singleton state.

## Reference Material

- `.trellis/spec/backend/index.md`
- `.trellis/spec/backend/directory-structure.md`
- `.trellis/spec/backend/database-guidelines.md`
- `.trellis/spec/backend/quality-guidelines.md`
- `.trellis/spec/backend/error-handling.md`
- `.trellis/spec/backend/logging-guidelines.md`
- `.trellis/spec/guides/cross-layer-thinking-guide.md`
- `.trellis/spec/guides/code-reuse-thinking-guide.md`
- `.trellis/tasks/archive/2026-04/04-28-cts-engineering-refactor/prd.md`
- `.trellis/tasks/archive/2026-04/04-28-cts-engineering-refactor/research/cts-naming-audit.md`
