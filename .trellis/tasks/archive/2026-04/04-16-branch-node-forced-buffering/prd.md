# framework-level leaf-branch buffering and H-tree boundary controls for iCTS

## Goal

Unify iCTS characterization coverage around `wire_length_iterations`, remove the separate pattern-node cap, and integrate leaf-branch buffering plus H-tree boundary controls as first-class framework concepts. The selected timing/power solution must remain consistent with the final realized tree, while H-tree callers can explicitly choose whether leaf branches require terminal buffers and whether top/leaf electrical defaults constrain the composed table.

## What I already know

* iCTS currently represents segment buffering patterns as normalized buffer positions plus cell-master names.
* H-tree currently exposes a single static build entry point and reads selection policy only from shared runtime config.
* Characterization coverage is already expressed through `wire_length_unit_um` and `wire_length_iterations`, but an extra node-cap path still truncates slot enumeration.
* H-tree table construction currently composes unrestricted per-level frontiers and selects from a global delay/power Pareto frontier.

## Resolved Defaults

* Leaf-branch buffering remains disabled by default.
* Characterization defaults use `wire_length_iterations = 5`, `slew_steps = 5`, and `cap_steps = 5`.
* Removing the node-cap path is acceptable as long as existing efficiency characteristics are preserved for the default iteration range.
* H-tree boundary defaults are optional. When absent, the existing unrestricted frontier-composition and global Pareto-selection behavior must remain unchanged.
* When strict boundary feasibility is impossible, the selected fallback uses the normalized active-boundary capability score, with constrained-dimension-first tie-break before delay, power, and pattern id.

## Open Questions

* No blocking user question remains.

## Requirements (evolving)

* Remove `max_pattern_nodes` from runtime config, reports, characterization, and related tests/helpers.
* Make `wire_length_iterations` the single characterization coverage control and set its default to `5`.
* Set `slew_steps` and `cap_steps` defaults to `5`.
* Keep segment-pattern representation able to express whether the segment ends with a terminal buffer that can drive a branch point.
* Ensure composed segment patterns preserve that terminal-buffer semantic on the downstream end.
* Separate pattern representation from H-tree policy:
  terminal-buffer-capable patterns should be characterized and represented independently of whether H-tree later requires them.
* Add H-tree caller-facing build options for:
  leaf-branch buffering requirement,
  top-level minimum input slew,
  leaf-level minimum driven cap.
* Make H-tree use those caller-facing options without mutating shared config state for the call.
* When a leaf-branch buffering requirement is active, H-tree must select only terminal-buffer-capable segment frontiers for fanout-bearing levels.
* When a top-level minimum input slew is provided, H-tree composition must preserve top-segment input-slew capability so final selection can keep only entries whose top boundary is at least that threshold.
* When a leaf-level minimum driven cap is provided, H-tree composition must preserve leaf-segment driven-cap capability so final selection can keep only entries whose leaf boundary is at least that threshold.
* If no boundary defaults are provided, keep the existing unrestricted frontier composition and Pareto selection.
* If no H-tree solution satisfies the requested top/leaf boundary thresholds, emit a warning and return the closest available solution instead of failing the build outright.
* The fallback path must preserve the leaf-level driven-cap boundary as first-class topology metadata so closeness scoring cannot be pruned away during composition.
* Boundary-constrained H-tree builds should use a single composed frontier and derive the strict-feasible subset only at final selection time, rather than rerunning composition under a fallback mode.
* Topology materialization must faithfully instantiate the selected terminal buffers at branch-entry points.

## Acceptance Criteria

* [x] Default-off runs keep existing behavior and pass the existing iCTS test suite.
* [x] `wire_length_iterations` fully replaces the node-cap path as the characterization coverage control.
* [x] Segment-pattern composition preserves terminal-buffer semantics on the downstream end.
* [x] H-tree callers can explicitly request leaf-branch buffering through a build option.
* [x] H-tree callers can explicitly provide top-level minimum input slew and leaf-level minimum driven cap thresholds through build options.
* [x] When enabled, leaf-branch buffering rejects non-terminal-buffer patterns on required H-tree levels.
* [x] When provided, top/leaf boundary thresholds filter final H-tree candidates without changing unrestricted behavior elsewhere.
* [x] When top/leaf boundary thresholds are infeasible, H-tree build warns and returns the closest available solution with explicit fallback metadata.
* [x] Materialized CTS objects place the terminal buffer at the branch entry point selected by the pattern.
* [x] New and updated tests cover representation, composition, H-tree option handling, and end-to-end H-tree behavior.
* [x] `ecc_dev_tools` path checks pass on touched paths and a full iCTS inspection passes as final acceptance.

## Definition of Done (team quality bar)

* Tests added/updated for leaf-branch buffering and H-tree boundary defaults
* Quality checks pass for touched iCTS paths
* Full iCTS inspection completes without blocking findings
* The final handoff clearly states behavior, risks, and validation scope

## Validation Summary

* Built `icts_test_module_characterization`, `icts_test_flow_htree`, and `icts_test_flow_htree_realtech`.
* Passed `./bin/icts_test_module_characterization`.
* Passed `./bin/icts_test_flow_htree`.
* Passed the targeted real-tech HTree boundary/leaf-buffer smoke path.
* Passed the full `./bin/icts_test_flow_htree_realtech` suite.
* Passed `ecc_dev_tools` path checks on touched HTree source/test paths with zero in-scope findings.
* Passed full `src/operation/iCTS` inspection with zero in-scope findings; existing out-of-scope findings remain as repository baseline.

## Out of Scope (explicit)

* Renaming existing public CTS APIs without necessity
* Large unrelated refactors in topology generation or routing
* External-module cleanup unrelated to this feature

## Technical Notes

* Current config entry point: `source/database/config/Config.hh` and `Config.cc`
* Current segment-pattern model: `source/database/characterization/BufferingPattern.hh`
* Current characterization builder: `source/module/characterization/CharBuilder.hh` and `CharBuilder.cc`
* Current H-tree composition and materialization: `source/flow/htree/HTreeBuilder.cc`
* Relevant existing tests live under `test/module/characterization/` and `test/flow/htree/`
* Prefer an explicit caller-facing H-tree options structure over ad-hoc temporary mutation of shared config state.
* The design must keep pattern representation, H-tree selection, and final materialization consistent so timing/power metrics describe the realized structure rather than an unbuffered proxy.
