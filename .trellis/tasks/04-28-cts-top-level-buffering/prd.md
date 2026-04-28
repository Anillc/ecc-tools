# CTS top-level source-to-root buffering

## Goal

Extend iCTS so the clock source to root buffer input path is synthesized instead of directly reconnecting the source net to root inputs. The new top-level stage must reuse existing characterization/frontier data where possible, support both single-root and multi-root cases, preserve immutable clock source cell masters, and keep downstream H-tree behavior unchanged for the current no-macro validation case.

## What I Already Know

* Current flow inserts one root buffer per non-empty sink group in `FlowManager::buildSinkGroup`.
* Downstream synthesis currently starts at each root buffer output via `ClockSynthesis::build` and `HTreeBuilder::build`.
* After all sink groups finish, `ClockNetManager::reuseClockSourceNetAsSourceToRootBuffers` directly reconnects the clock source net to all root buffer input pins.
* Therefore the `clock source -> root buffer input(s)` path is currently not buffered, characterized, or selected through delay/power frontier logic.
* The current validation command is:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

* The requested validation case has no macro sink group, but the implementation must keep regular/macro/multi-clock logging unambiguous.

## Requirements

* Add a top-level source-to-root synthesis stage after downstream sink-group H-tree synthesis and before final source-net reconnect.
* For exactly one root input:
  * Build a single `clock source -> root input` segment.
  * Reuse existing `SegmentChar` / `BufferingPattern` data.
  * If the exact length bin is not available, compose segment frontiers using existing segment concatenation logic.
  * Select from delay/power frontier candidates.
  * Apply boundary filtering with input slew target equal to `max_buf_tran * 0.5` when available.
  * Use actual root input capacitance as the required load cap.
  * Treat clock source drive capability as a hard boundary.
  * If no strict candidate exists, remove only soft boundary constraints such as the 50% input-slew target and retry.
  * Do not remove hard legality constraints such as actual load cap, source drive capability, port resolution, or materialization validity.
* For multiple root inputs:
  * Build the top-level distribution with H-tree logic.
  * The H-tree root location must be fixed to the clock source location, not the median of root inputs.
  * The top-level H-tree must not resize or modify the clock source instance/cell master.
  * Add an H-tree build option, or equivalent policy, to disable root driver sizing.
  * Keep downstream H-tree root sizing behavior available for CTS-inserted root buffers.
* Reuse characterization results through an explicit shared characterization library or equivalent reusable data structure rather than relying on one downstream builder's private local `CharBuilder`.
* Include top-level source-to-root lengths in characterization planning so the selected char grid can cover both downstream H-tree levels and source-to-root paths.
* Ensure object ownership and final commit semantics remain compatible with `ClockNetManager::commitInsertedObjects`.
* Preserve rollback behavior: if top-level synthesis fails, netlist side effects must not leave partial source-net or inserted-object state.
* Add log/report context so repeated H-tree and top-level sections can be attributed to:
  * `clock_name`
  * `clock_net_name`
  * sink domain such as `top`, `regular`, or `hard_macro`
  * stage such as `top_segment`, `top_htree`, or `downstream_htree`
  * object prefix or root/sink counts when useful.

## Acceptance Criteria

* [x] Single-root top-level source-to-root path is synthesized as a segment and can insert zero or more buffers.
* [x] Multi-root top-level source-to-root path is synthesized through H-tree logic with source location as fixed root.
* [x] Clock source instance cell master is never changed by top-level synthesis.
* [x] Downstream H-tree can still resize CTS-inserted root buffers unless explicitly disabled.
* [x] Top-level strict boundary selection falls back only by dropping soft input-slew boundary when necessary.
* [x] Actual root input load cap and clock-source drive capability remain hard selection constraints.
* [x] Reports/logs identify clock/domain/stage clearly enough to disambiguate repeated H-tree sections.
* [x] The no-macro validation case completes successfully:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

* [x] Validation logs show no unexpected functional differences for the no-macro case except expected additional top-level source-to-root reporting.
* [x] Project quality checks pass through the ecc dev workflow.

## Completion Notes

* Implemented `CharacterizationLibrary` as an explicit reusable characterization cache for downstream and top-level stages.
* Implemented `SegmentBuilder` for single-root source-to-root synthesis with strict hard-boundary filtering and soft input-slew fallback.
* Extended `HTreeBuilder` and `TopologyGen` with fixed-root topology, shared characterization, top-level extra char lengths, root-driver sizing control, and context-rich reports.
* Extended `STAAdapter` with clock-source drive-cap lookup; IO clock sources use configured runtime `max_cap` as the explicit hard fallback when source-specific STA/liberty cap is unavailable.
* Validation command completed successfully on 2026-04-28:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

* `result/cts/cts.log` showed `CTS Flow Summary` status `finished`, `failed_clocks = 0`, `SegmentBuilder Build Summary` with `clock_name = clk_i`, `sink_domain = top`, `stage = top_segment`, `inserted_insts = 1`, `inserted_nets = 1`, and `used_boundary_fallback = false`.
* `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with 0 in-scope findings.

## Additional Requirement: CTS Report Visualization Artifacts

After the source-to-root buffering work, extend the CTS report stage to emit visualization artifacts.

### New Requirements

* Add CTS report-stage SVG output only:
  * Reuse the existing SVG plotting approach from iCTS tests.
  * Move the reusable SVG plotter/helpers from `test/common/visualization` into production `source/utils` so report code can depend on it without depending on test targets.
  * Keep tests using the moved SVG utility rather than carrying a duplicate test-only implementation.
  * Emit `cts_design.svg` for the routed CTS design view and `cts_flyline.svg` for the driver-to-load flyline view.
  * Do not emit report-stage GDS/GDSII artifacts or KLayout `.lyp` sidecars. Do not generate or record `cts_design.gds`, `cts_flyline.gds`, or `cts_report.lyp`.
  * `cts_design.svg` must draw the routed CTS design. It may continue using `Router::buildClockNetTree`.
  * `cts_flyline.svg` must draw only flylines from each clock-net driver to its loads. Do not draw routed design wires in this artifact.
  * Use the same semantic palette and legend style as buffering/H-tree visualizations: sink load `#1f77b4`, driver/root `#d62728`, routed net `#2ca25f`, flyline/root net `#6a3d9a`, fallback/internal net `#ff8c42`, sink-level net `#0f766e`, monospace 12 px legend, white `fill-opacity=0.88` legend frame, `#d0d0d0` frame stroke, and 18 px row spacing.
  * Use report-scale marker sizes for full-design SVGs so loads do not dominate the view: sink load around 2 px, driver/root around 3 px, and CTS buffer marker around 4-5 px.
* Add the generated artifact paths to `cts.log` in the CTS report section.
* Keep `cts.log` focused on CTS flow/report state. Do not add SVG-internal details such as segment counts, marker counts, palette metadata, or other visualization-only diagnostics; basic SVG artifact file paths are sufficient.
* Preserve existing `cts_report` semantics:
  * If evaluator state is already available, report should reuse it.
  * If report is called standalone, it may rebuild evaluation state as before.
  * Visualization generation must not rerun CTS synthesis.
* Failure handling:
  * A visualization artifact failure should be reported in `cts.log` with a clear reason.
  * If a required database/design state is missing, report should fail the artifact generation cleanly rather than crash.
* Validation:
  * Run `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`.
  * Confirm `result/cts/output/cts_design.svg` and `result/cts/output/cts_flyline.svg` are produced.
  * Confirm `result/cts/cts.log` records the visualization artifact paths/status.
  * Do not run `ecc_dev_tools` for this cleanup unless the user explicitly changes the latest instruction.

### Completion Notes

* Added report-stage visualization artifact generation through `FlowManager::report`, keeping report evaluation reuse semantics intact.
* Added `cts_design.svg` and `cts_flyline.svg` using SVG helpers moved from test-only visualization code into `source/utils/visualization`; tests now depend on the production visualization utility.
* `cts_design.svg` uses `Router::buildClockNetTree` so it follows the routed CTS tree used by evaluation; `cts_flyline.svg` draws driver-to-load connections only.
* Unified report SVG style with buffering/H-tree visualizations through shared `SvgCommon.hh` constants; report SVG load markers are small full-design markers, and the report SVG legend uses the same monospace/white-frame style.
* Added `CTS Report Visualization Artifacts` to `cts.log`, limited to the two SVG artifact paths, type/view, status, and summary-level detail/reason.
* Revalidated after SVG-only cleanup on 2026-04-28:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

* Confirmed generated artifacts:
  * `result/cts/output/cts_design.svg` (`SVG Scalable Vector Graphics image`)
  * `result/cts/output/cts_flyline.svg` (`SVG Scalable Vector Graphics image`)
* `result/cts/cts.log` showed `CTS Flow Summary` with `failed_clocks = 0`, both SVG visualization artifacts as `generated`, and `CTSReport` with `statistics_status = finished`, `visualization_status = finished`.
* Removed stale ignored `cts_design.gds`, `cts_flyline.gds`, and `cts_report.lyp` files from the validation output directory after confirming the current report log no longer records them.
* `ecc_dev_tools` is skipped for the latest cleanup by explicit user request.

### Follow-up Completion Notes: Non-Invasive Visualization Semantics

* Removed the attempted visualization-only fields/API from core `Inst` and `Net`; no `Pin` visualization metadata was added.
* Removed the report-only `Wrapper` physical layout snapshot API because the latest scope is SVG-only and does not need iDB physical-context facts.
* Removed flow-local visualization metadata propagation through H-tree, segment, synthesis, and report code because it only served the withdrawn GDS semantic-layer work.
* `cts.log` now records only the two basic SVG report artifacts:
  * `result/cts/output/cts_design.svg`
  * `result/cts/output/cts_flyline.svg`
* No `source/database/dag` helper was added because the current SVG-only implementation does not need one.
* The requested build, topology/synthesis tests, and iEDA dev script were reported passing after this cleanup. The final ecc dev check was skipped by user request.

### Technical Notes

* Current refactor branch report entry point: `FlowManager::report(const std::string& save_dir)`.
* Report visualization artifacts are `output/cts_design.svg` and `output/cts_flyline.svg`.
* Existing SVG test utilities were moved from `src/operation/iCTS/test/common/visualization` into production `source/utils/visualization`.

## Definition of Done

* Implementation is scoped to iCTS backend flow modules and supporting characterization/topology helpers.
* Lint/type/quality checks required by project workflow pass or any blocker is documented.
* The requested iCTS dev validation command is run and its result is reported.
* New or updated tests are added where practical for single-root, multi-root, no-root/error, and source immutability behavior.
* Spec update is considered at finish; update spec docs only if new conventions or durable contracts are introduced.

## Out of Scope

* Precise clock-source-specific characterization of arbitrary source cell delay/power.
* Changing sink clustering behavior.
* Changing downstream H-tree selection policy except for adding reusable char/log/root-sizing options needed by top-level synthesis.
* Macro-specific validation in this task; current requested validation case has no macro.

## Technical Notes

* Current direct reconnect point: `FlowManager::runClock` calls `ClockNetManager::reuseClockSourceNetAsSourceToRootBuffers`.
* Current downstream H-tree root sizing happens in `HTreeBuilder::build` through `ValidateRootDriverSizing` and `ApplyRootDriverSizing`.
* `TopologyGen` currently places the root at the median of loads; top-level multi-root synthesis needs an override for fixed clock-source root position.
* `SynthesizeSegmentEntrySets` already supports composing missing length bins from shorter segment frontiers.
* `HTreeTraits` already models binary fanout by matching downstream driven cap against half upstream load cap.
* Existing log titles such as "HTreeBuilder Characterization Grid Plan" and "HTreeBuilder Build Summary" are ambiguous when emitted multiple times in one CTS run; context fields or context-aware titles are required.

## Additional Requirement: Non-Invasive SVG Visualization Semantics

After the report visualization work, keep SVG output semantics local to report code without polluting core CTS database objects.

### New Requirements

* Remove visualization-only state from core design objects:
  * Do not add H-tree depth, report color, marker, text, or visualization classification fields to `Inst`, `Net`, `Pin`, or other core semantic database classes.
  * Do not encode report visualization classification policy or physical-context collection in `Wrapper`.
* Do not add a `source/database/dag` helper for report visualization; the SVG-only implementation does not need one.
* Do not add algorithm-result visualization metadata solely for report SVG. Use existing net/inst/pin facts and local report heuristics.
* Do not generate GDS/GDSII semantic layers or KLayout `.lyp` sidecars in this task scope.
* SVG output should keep full-design markers small and styling aligned with existing H-tree/buffering visualization conventions.
* Validation remains:
  * Run `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`.
  * Confirm report SVG artifacts are generated and `cts.log` records their basic file paths.
  * Do not run `ecc_dev_tools` while the latest user instruction forbids it.
