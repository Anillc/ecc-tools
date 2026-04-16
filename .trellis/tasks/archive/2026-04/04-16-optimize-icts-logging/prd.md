# Optimize iCTS Logging

## Goal

Refine the current iCTS logging so runtime output is easier to scan and reason about without changing functionality. The new style should move away from流水账 single-line spam toward titled sections and compact tables where appropriate, with clearer module/operator lifecycle markers and more accurate fallback diagnostics. The target style should borrow from `~/download/clock_trees.rpt` while staying idiomatic to existing `CTS_LOG_*` usage.

## What I already know

* The user wants a non-functional logging optimization across current iCTS flows, with special attention to H-tree flow output.
* The user explicitly wants subagents used for broad review and implementation.
* `src/operation/iCTS/source/utils/logger/Logger.cc` currently forwards logs to glog with `[<full path>:<line>]` while glog already prints file/line in its own prefix.
* `src/operation/iCTS/source/module/characterization/CharBuilder.cc` logs parameter resolution as if some values come from `Config` even when they are resolved by fallback heuristics.
* `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` and `src/operation/iCTS/api/CTSAPI.cc` currently emit many flat `CTS_LOG_INFO` lines instead of structured titled summaries.
* A reusable table helper already exists at `src/utility/report/ReportTable.hh`, but iCTS does not currently use it.
* `HTreeBuilder` already computes compact characterization-grid metadata such as `wire_length_unit_um`, `wire_length_iterations`, `unique_level_bins`, and `adapted`.

## Assumptions (temporary)

* No algorithmic behavior or database contents should change; only logging structure, wording, and supporting helpers may change.
* Existing warning/error/fatal behavior should remain semantically equivalent unless a message currently misrepresents fallback provenance.
* A local iCTS-scoped helper is acceptable if reusing `report_table` directly would introduce awkward dependency coupling.

## Open Questions

* None blocking at the moment. Proceed with repo-derived assumptions unless a design conflict appears during implementation.

## Requirements (evolving)

* Remove redundant file-path echoing from iCTS console logging while preserving useful call-site context.
* Introduce a reusable way to emit titled sections and ASCII tables for iCTS logging, either by reusing `src/utility/report` or adding a focused helper under `src/operation/iCTS/source/utils/`.
* Rework key iCTS logging in H-tree / characterization / API summary paths so that:
* major phases have clear `start`, `running`, `finished` style markers
* dense scalar dumps are collapsed into titled tables
* related sections are visually separated with blank lines where useful
* Fix misleading fallback wording in `CharBuilder`, especially around `wire_length_unit_um` and similar resolved parameters.
* When H-tree characterization bins collapse or config is absent, emit a warning that explains the fallback / auto-derivation path and reflect the resolved source in the parameter summary.
* Check similar parameter-resolution logs for consistent provenance wording.
* Run `ecc_dev_tools` path checks for touched iCTS paths and a final full `src/operation/iCTS` check. Handoff only when in-scope findings are clean.

## Acceptance Criteria (evolving)

* [ ] iCTS console logs no longer duplicate the full source path after glog’s own file/line prefix.
* [ ] H-tree flow logs show titled, table-oriented summaries instead of flat repetitive scalar lines where tabular presentation fits.
* [ ] `CharBuilder` clearly distinguishes values from `Config`, liberty-derived values, and auto-derived fallback values.
* [ ] Fallback to auto-derived characterization grid is warned at the decision point when unique bins collapse or config is absent.
* [ ] Module/operator lifecycle transitions are visibly clearer in the optimized logs.
* [ ] `ecc_dev_tools` is clean for touched paths and final `src/operation/iCTS` validation.

## Definition of Done (team quality bar)

* Tests added/updated where logging behavior is directly covered by existing test infrastructure
* `ecc_dev_tools` checks are green for touched paths
* Final `ecc_dev_tools` full iCTS pass is clean for in-scope findings
* No functional behavior regression introduced

## Out of Scope (explicit)

* Rewriting all legacy fatal/error messages across every iCTS module
* Algorithmic refactors unrelated to logging clarity
* Non-iCTS module cleanup unless minimally required by linkage or headers

## Technical Notes

* Relevant guidelines:
* `.trellis/spec/project-constraints.md`
* `.trellis/spec/backend/directory-structure.md`
* `.trellis/spec/backend/logging-guidelines.md`
* `.trellis/spec/backend/error-handling.md`
* `.trellis/spec/backend/quality-guidelines.md`
* `.trellis/spec/guides/code-reuse-thinking-guide.md`
* `.trellis/spec/guides/cross-layer-thinking-guide.md`
* Candidate code areas:
* `src/operation/iCTS/source/utils/logger/Logger.hh`
* `src/operation/iCTS/source/utils/logger/Logger.cc`
* `src/operation/iCTS/source/module/characterization/CharBuilder.cc`
* `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`
* `src/operation/iCTS/api/CTSAPI.cc`
* `src/operation/iCTS/test/common/io/TestArtifactIO.cc`
* Reference style: `~/download/clock_trees.rpt`
