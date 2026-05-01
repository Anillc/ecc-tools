# brainstorm: CTS flow architecture and naming redesign

## Goal

Optimize the CTS flow module architecture so users and developers can understand the main interfaces, core functions, behavior patterns, and responsibility boundaries from the directory and naming structure. The outcome should be a mature architecture and naming proposal grounded in both EDA industry CTS conventions and the existing code's real business semantics.

## What I already know

* The focus is the CTS flow module.
* At task start, flow directories included examples such as `synthesis`, `stage`, and `htree`.
* Subflow internals contain many files and many structs.
* The current structure has poor readability: users and developers cannot easily infer primary interfaces, major functionality, and behavior patterns from folder/file organization.
* The desired work is research/design first, not immediate code refactoring.
* The proposal should reference mature physical-design CTS terminology and workflow conventions from industrial EDA tools such as Cadence Innovus.
* The main local target is `src/operation/iCTS/source/flow`.
* At task start, top-level flow ownership was split below `CTSAPI`; the final implementation converges that ownership into `Flow`.
* At task start, flow subdirectories included setup, generic stage, synthesis, topology, diagnostic view, visualization, evaluation, and net-editing peer folders.
* Initial file count concentration confirmed the readability concern: topology and generic stage folders were dense, and the synthesis root also had many peer files.
* Initial build targets exposed too many conceptual peer boundaries; the final implementation keeps the root flow target plus architecture-aligned subtargets under setup, synthesis, instantiation, evaluation, and report.
* User design principles:
  * Cross-module database should live under `src/operation/iCTS/source/database`.
  * Internal module data should not be over-atomized into many tiny structs; prefer module-level data managers/contexts.
  * The target architecture should enforce high cohesion and low coupling.
  * The desired high-level CTS flow layers are `setup`, `synthesis` (CTS algorithm body), `instantiation` (materialize CTS algorithm results into design/iDB objects), `evaluation`, and `report`.
  * Avoid nested `flow/flow` or awkward substitutes such as `clock_tree_flow`; use `synthesis` for the algorithm body.
  * Each folder should expose one readable primary entry file aligned with the folder name, such as `synthesis/Synthesis.hh` and `instantiation/Instantiation.hh`.
  * In each flow folder, the folder root should either contain only the matching primary entry pair (`*.hh`/`*.cc`) or move all non-entry implementation files into responsibility-based subfolders; avoid mixing a main entry with many peer helper files.
  * Subfolder splitting should be proportional to code size and responsibility boundaries: keep tiny private helpers inside the main `.cc`; create subfolders when code volume, ownership, or reuse makes the boundary visible and useful.
  * Secondary folder names should be readable architectural concepts, not overly direct implementation labels such as `sink_domain` or `per_clock` unless the term is truly the business concept exposed to readers.
  * Secondary folder names should also avoid overly abstract engineering labels such as `planning`, `preparation`, `exploration`, `assembly`, and `diagnostics` when a clearer CTS/EDA business term exists.
  * In `instantiation`, use concise `design/DesignConversion` and `idb/IdbConversion`; the parent folder already provides the materialization context, while the file names keep the conversion action explicit.
  * `report` should not appear to contain only visualization; reports include summary, statistics, result export, schema/log output, and clock-tree visualization.
  * In `report`, avoid computer-generic names such as `artifacts` and `diagnostics`; use `export/ResultExport` for report result export policy/status and `visualization/ClockTreeVisualization` for SVG/GDS clock-tree visual output.
  * Do not add a redundant `database/cts` folder under iCTS; shared CTS design data should live under existing database concepts such as `database/design`.
  * Final synthesis second-level names are `distribution`, `topology`, and `trace`; H-tree internals use `characterization`, `pattern_search`, and `construction`.
  * Use `trace/SynthesisTrace` for synthesis algorithm execution records; avoid final-QoR, selection-only, or generic execution-log labels.

## Assumptions (temporary)

* "CTS flow module" refers to local source directories in this repository, likely under a `flow` or CTS-related backend module.
* The main deliverable for this task is an architecture and naming design document. Code changes may be a later task unless the design is explicitly approved for implementation.
* The redesign should preserve existing business behavior unless the research discovers clear responsibility conflicts that should be separated.
* The naming scheme should be readable to CTS users, flow developers, and maintainers who understand EDA physical design concepts.

## Open Questions

* None for the architecture direction. The user confirmed the naming and requested a concrete refactor plan before implementation.

## Finalized Architecture

```text
src/operation/iCTS/source/
  database/
    design/
      ClockTree.hh
      ClockTree.cc

  flow/
    Flow.hh
    Flow.cc

    setup/
      Setup.hh
      Setup.cc

    synthesis/
      Synthesis.hh
      Synthesis.cc
      distribution/
        ClockDistribution.hh
        ClockDistribution.cc
      topology/
        Topology.hh
        Topology.cc
        htree/
          HTree.hh
          HTree.cc
          characterization/
          pattern_search/
          construction/
      trace/
        SynthesisTrace.hh
        SynthesisTrace.cc

    instantiation/
      Instantiation.hh
      Instantiation.cc
      design/
        DesignConversion.hh
        DesignConversion.cc
      idb/
        IdbConversion.hh
        IdbConversion.cc

    evaluation/
      Evaluation.hh
      Evaluation.cc

    report/
      Report.hh
      Report.cc
      summary/
        Summary.hh
        Summary.cc
      statistics/
        StatisticsReport.hh
        StatisticsReport.cc
      export/
        ResultExport.hh
        ResultExport.cc
      visualization/
        ClockTreeVisualization.hh
        ClockTreeVisualization.cc
        svg/
        gds/
```

Root flow folders should expose only the matching primary entry pair plus build metadata. Any additional implementation files must live in responsibility-based subfolders when the code volume justifies the split.

## Final Convergence Phases

The implementation plan was extended beyond the original transition cleanup so the code converges on the recommended architecture instead of stopping at temporary wrappers:

* Phase 9: converge `flow/synthesis/` so its root contains only `Synthesis.hh`, `Synthesis.cc`, and `CMakeLists.txt`; move helpers under `distribution`, `topology`, and `trace`.
* Phase 10: converge `flow/evaluation/` so its root contains only `Evaluation.hh`, `Evaluation.cc`, and `CMakeLists.txt`; keep metric computation under `evaluation/metrics` and report file writing under `report/statistics`.
* Phase 11: remove the old top-level `stage` and `clock_tree_view` peer architecture folders; retain diagnostic visualization model code only under `report/visualization/model`.
* Phase 12: classify remaining public structs as stable boundary records or submodule-local contracts, and verify high-level flow/report/evaluation headers do not expose obvious H-tree search details.
* Phase 13: perform non-ECC, non-CTS-binary final verification only; record that ECC is deferred by the instruction active at that time rather than claiming ECC coverage.
* Phase 14: converge the flow entry itself so `Flow` owns the CTS lifecycle state and orchestration directly; delete the old manager files, singleton macro, build source, and test target names with no transitional facade or alternate entry.
* Phase 15: after the later explicit user override, perform final unified verification with `iEDA` build, full iCTS dev script execution, and ECC dev check for `src/operation/iCTS`.

## Requirements (evolving)

* Research physical-design CTS flow conventions and terminology from mature EDA industry sources, including Innovus-related material where available.
* Deeply inspect the existing CTS flow code to identify each flow/subflow's core function, business semantics, data structures, and responsibility boundary.
* Produce a redesigned architecture for the CTS flow module that makes primary interfaces and behavior patterns visible from the folder structure.
* Produce a naming proposal for directories, files, modules, structs, and key concepts where the current terms are ambiguous or too implementation-driven.
* Distinguish user-facing flow concepts from developer/internal implementation concepts.
* Identify expected migration risk and suggested staged rollout.
* Evaluate current CTS flow design against the user's database ownership, internal data encapsulation, cohesion/coupling, and layer-boundary principles.
* Revise the architecture proposal around `setup`, `synthesis`, `instantiation`, `evaluation`, and `report`, while documenting database and naming constraints.
* Define the responsibility boundary for `database/design/ClockTree` so it does not become an algorithm context, instantiation service, or report-only view.
* Re-analyze each proposed subflow independently before finalizing the second-level architecture and names.
* Produce a concrete refactor plan with module responsibilities, semantic boundaries, red lines, migration phases, and verification gates.
* Maintain a detailed phased implementation checklist with each phase's development scope, goal, acceptance criteria, status, and implementation notes.
* After each phased implementation completes, update `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/implementation-checklist.md` before starting the next phase.
* During development phases, do not run CTS binaries and do not perform ECC checks.
* After all architecture/code convergence phases finish, the latest user instruction requires final unified verification: build `iEDA`, run `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl`, then run ECC dev check on `src/operation/iCTS` until it passes or a concrete blocker is identified.
* For the flow-entry convergence pass, do not keep transitional entry points for the old lifecycle owner; `FLOW_INST` is the only active CTS flow singleton entry.

## Acceptance Criteria (evolving)

* [x] Research findings are persisted under this task's `research/` directory.
* [x] The codebase analysis identifies the current CTS flow entry points, subflows, major structs, and responsibility boundaries.
* [x] The design proposal maps current concepts to proposed architecture and names.
* [x] The proposal explains why each proposed name better matches CTS/physical-design semantics.
* [x] The proposal includes out-of-scope items and migration constraints.
* [x] The user confirms the proposed direction before implementation begins.
* [x] A concrete refactor plan records responsibilities, red lines, and staged migration guidance.
* [x] A phased implementation checklist exists with scope, goals, and acceptance criteria for each phase.
* [x] Each implementation phase updates the checklist immediately after completion.
* [x] `synthesis/` root contains only `Synthesis.hh`, `Synthesis.cc`, and `CMakeLists.txt`; synthesis helpers are placed under `distribution/`, `topology/`, or `trace/`.
* [x] `evaluation/` root contains only `Evaluation.hh`, `Evaluation.cc`, and `CMakeLists.txt`; statistics report output lives under `report/statistics`.
* [x] `stage/` and `clock_tree_view/` are no longer ambiguous top-level business architecture folders; retained adapter surfaces have final boundary names and checklist rationale.
* [x] Public structs that remain in headers are justified as stable boundary records or clear submodule-local contracts.
* [x] `Flow` is the only active CTS flow lifecycle owner; the old manager files, singleton macro, source target entry, and flow-root test naming are removed.
* [x] Final non-ECC, non-binary verification is performed after all phased implementation work converges under the earlier instruction.
* [x] Final unified verification per latest user instruction passes: `iEDA` build, full iCTS dev script, and ECC dev check for `src/operation/iCTS`.

## Definition of Done (team quality bar)

* Requirements and design are recorded in task files, not only in chat.
* Industry research and local code research artifacts exist under `research/`.
* Implementation/check context JSONL files are curated if this task moves to code changes.
* If code changes are performed during development phases: run static/CMake/build-level checks that do not execute CTS binaries and do not invoke ECC.
* During phased implementation: no CTS binary execution and no ECC checks.
* After all phases converge and the latest user override is active: run and record `iEDA` build, full iCTS dev script execution, and ECC dev check.
* Specs/notes are updated if the work establishes new project conventions.

## Out of Scope (explicit)

* Immediate broad code movement before the architecture is approved.
* Changing CTS algorithm behavior unless the design phase identifies a necessary semantic correction.
* Replacing the whole CTS implementation with a third-party flow.

## Technical Notes

* Task directory: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign`
* Current workflow phase: final unified verification for the implemented architecture convergence pass.
* Research should include both external CTS terminology and internal code structure.
* Initial top-level files inspected: `src/operation/iCTS/api/CTSAPI.hh`, `src/operation/iCTS/api/CTSAPI.cc`, the former flow-owner implementation files, and flow `CMakeLists.txt` files.
* Research agents launched:
  * `industry-cts-flow-terminology.md`: Innovus/CCOpt and industrial CTS terminology.
  * `current-cts-flow-code-map.md`: local iCTS flow code map and responsibility boundaries.
  * `open-source-cts-comparison.md`: OpenROAD/TritonCTS and other public CTS naming comparison.
  * `subflow-setup-architecture.md`: setup subflow responsibility and folder-depth analysis.
  * `subflow-synthesis-architecture.md`: synthesis algorithm-body responsibility and second-level architecture.
  * `subflow-instantiation-architecture.md`: design/iDB materialization boundary and naming.
  * `subflow-evaluation-architecture.md`: evaluation result/statistics boundary and folder-depth analysis.
  * `subflow-report-architecture.md`: report summary/statistics/export/visualization architecture.
  * `database-clocktree-boundary.md`: `database/design/ClockTree` semantic model boundary.
* Design proposal: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md`.
* Refactor plan: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/refactor-plan.md`.
* Phased implementation checklist: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/implementation-checklist.md`.
