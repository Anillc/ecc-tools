# brainstorm: CTS topology and H-tree architecture readability analysis

## Goal

Analyze and implement the readability cleanup under `src/operation/iCTS/source/flow/synthesis/topology` and its former nested `htree` implementation, reducing directory depth, clarifying business semantics, and keeping the CTS synthesis boundary coherent.

## What I already know

* The previous CTS flow architecture convergence is broadly acceptable.
* The user explicitly called out `source/flow/synthesis/topology` and nested `topology/htree` as too deep and less readable.
* The current path reaches five business/code levels for H-tree internals, for example `synthesis/topology/htree/pattern_search`.
* `topology/Topology.hh` exists as a nominal primary entry but currently has no operational interface.
* The operational public entries in this area are mostly `ClockSynthesis.hh`, `ClockTreeSynthesisTransaction.hh`, and `htree/HTree.hh`.
* `topology` currently mixes at least four concerns:
  * one-clock tree synthesis orchestration
  * downstream sink-tree synthesis
  * source-to-root/top-segment synthesis
  * per-clock transaction and commit/rollback coordination
* `htree` currently mixes H-tree family entry, characterization, pattern search, and temporary object construction.
* Some implementation files now live under H-tree even though their semantics are broader than H-tree, especially temporary net/object editing and source-to-root segment building.
* Latest user feedback:
  * Splitting the current nested area into two folders is accepted.
  * Naming should not add arbitrary `Clock`, `Tree`, or `Synthesis` prefixes/suffixes when parent folders already provide that meaning.
  * Avoid `transaction` and `editor`; those are implementation-mechanical words, not CTS business concepts.
  * Avoid visible architecture names using `registry`, `profile`, or `context`; prefer CTS/EDA terms that describe the data's role.
  * Use `SinkLoadRegion` instead of `SinkLoadEnvelope`; `region` is more familiar and should mean sink-load legality region, not a physical placement region.
  * The target names should be CTS-friendly and semantically concrete.
  * The user later authorized implementation, independent check, full `iEDA` build, the `ics55_dev` iCTS Tcl run, and full `src/operation/iCTS` ECC dev validation.
  * Follow-up feedback: `htree/characterization` still contains unclear names (`Options`, `BuildOptions`, `Cache`) whose semantics do not match the folder. `characterization/` should contain only characterization grid/library/flow concepts.

## Assumptions

* The analysis-first phase is complete; the user confirmed implementation should proceed.
* The previous top-level flow structure remains valid: `setup`, `synthesis`, `instantiation`, `evaluation`, and `report`.
* The issue is primarily inside the second and third levels of `synthesis`, not the top-level flow architecture.
* The implementation should preserve CTS behavior and avoid compatibility aliases.

## Research References

* [`research/current-topology-htree-code-map.md`](research/current-topology-htree-code-map.md) - local code map, readability diagnosis, and recommended architecture options.

## Requirements

* Quantify current directory depth, file distribution, and dense files under `synthesis/topology` and nested `htree`.
* Identify which files are true topology/H-tree code and which files are topology formation, sink-branch construction, source-trunk construction, trace, or temporary object management.
* Evaluate whether the `topology` layer is a useful abstraction or an empty taxonomy layer in the current codebase.
* Evaluate whether the nested `topology/htree` placement is justified by likely future topology families.
* Propose a clearer directory structure and naming plan with explicit responsibility boundaries.
* Implement the accepted `synthesis/topology` and sibling `synthesis/htree` architecture without compatibility aliases.
* Refine the `htree/characterization` subfolder so generic option/cache names are removed or relocated by responsibility:
  * topology-to-H-tree input assembly should live with topology callers or be folded into their `.cc` files.
  * H-tree build-option boundary resolution should live with pattern/depth search constraints, not characterization.
  * buffer strength/port lookup tables should use semantic names and live with pattern or embedding usage, not generic `Cache`.
* Keep the proposal aligned with CTS business terminology and the previous user constraints: high cohesion, low coupling, no unnecessary compatibility layer, and readable primary entry files.
* Mark red lines for future implementation so this area does not regress into generic `stage`, empty facade, or over-nested taxonomy folders.

## Acceptance Criteria

* [x] A dedicated child task exists under the CTS flow architecture task.
* [x] Current `synthesis/topology` and `synthesis/topology/htree` files are mapped by responsibility.
* [x] The analysis identifies concrete readability failures, not only subjective naming concerns.
* [x] The analysis proposes at least two viable architecture options and recommends one.
* [x] The recommendation includes future implementation phases and acceptance checks.
* [x] The recommendation reflects the latest naming feedback: no arbitrary `Clock`/`Tree`/`Synthesis` prefixes, no `transaction`, no `editor`, no visible `registry`/`profile`/`context` target names, and `SinkLoadRegion` instead of uncommon `SinkLoadEnvelope`.
* [x] User confirmed implementation may proceed.
* [x] Source/test/CMake implementation converges to sibling `synthesis/topology` and `synthesis/htree` architecture.
* [x] Independent Trellis check completed and fixed CMake visibility/test-target findings.
* [x] Full `iEDA` build passes after final fixes.
* [x] Before the later characterization naming cleanup, `cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` passed and reported `iCTS run successfully.`
* [x] Before the later characterization naming cleanup, full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with 0 in-scope findings.
* [x] `htree/characterization` no longer exposes generic `Options`, `BuildOptions`, or `Cache` files; remaining files are characterization-specific.
* [x] After the characterization naming cleanup, focused build/tests/checks passed; latest user instruction deferred further iCTS dev-script and full ECC dev validation.

## Definition of Done

* Analysis and final implementation status are persisted under this task, not only in chat.
* The recommended target structure is concrete enough to maintain.
* The plan clearly states what must not be reintroduced.
* Source, test, and CMake changes are verified by the latest user-approved scope. Full iCTS dev-script and ECC dev validation were run before the characterization naming follow-up; after the follow-up, the user explicitly deferred further dev checks.

## Out of Scope

* Rewriting unrelated CTS flow/database/evaluation/report architecture outside the accepted topology/H-tree cleanup.
* Rewriting H-tree search, characterization, or construction behavior.
* Repairing out-of-scope ECC findings from external modules.

## Implementation Status

Completed on 2026-05-01.

Implemented architecture:

```text
src/operation/iCTS/source/flow/synthesis/
  topology/
    Topology.hh
    Topology.cc
    sink/
      SinkBranch.hh
      SinkBranch.cc
    trunk/
      SourceTrunk.hh
      SourceTrunk.cc
      SourceTrunkSegment.hh
      SourceTrunkSegment.cc
    buffer/
      BufferInsertion.hh
      BufferInsertion.cc
  htree/
    HTree.hh
    HTree.cc
    characterization/
      Characterization.hh
      Characterization.cc
      CharacterizationLibrary.hh
      CharacterizationLibrary.cc
    pattern/
      PatternSearch.hh
      PatternSearch.cc
      PatternLibrary.hh
      BufferStrengthTable.hh
      BoundaryConstraints.hh
      BoundaryConstraints.cc
      SegmentFrontier.cc
      DepthSearch.cc
      DepthEvaluation.hh
      DepthEvaluation.cc
      SinkLoadRegion.hh
      SinkLoadRegion.cc
    embedding/
      Embedding.cc
      EmbeddingState.hh
      BufferPortTable.hh
```

Verification record:

* `cmake --build build --target iEDA -j2` passed after final CMake visibility fix.
* `cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` passed after final CMake visibility fix; the run reported `iCTS run successfully.` and completed report generation.
* `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with 0 in-scope findings. The checker still reported existing out-of-scope diagnostics in external `src/database/**` headers, but they are not in-scope for this iCTS task.
* After the later `htree/characterization` naming cleanup, `cmake --build build --target iEDA -j2` passed again.
* After the later `htree/characterization` naming cleanup, focused checks passed:
  * `cmake --build build --target icts_source_flow_synthesis icts_source_flow_synthesis_htree icts_test_flow_synthesis icts_test_flow_synthesis_htree icts_test_module_characterization_support -j2`
  * `./bin/icts_test_flow_synthesis`
  * `./bin/icts_test_flow_synthesis_htree`
  * focused `.trellis/ecc_dev_tools/check.py` over the touched H-tree/topology/test support paths with 0 in-scope findings.
* A post-follow-up iCTS dev-script run was started but intentionally interrupted after the user said not to run dev checks; no post-follow-up full ECC dev check was run.

## Technical Notes

* Task directory: `.trellis/tasks/05-01-cts-topology-htree-architecture-readability-analysis`
* Parent task: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign`
* Main source scope:
  * `src/operation/iCTS/source/flow/synthesis/topology`
  * `src/operation/iCTS/source/flow/synthesis/htree`
  * `src/operation/iCTS/test/flow/synthesis/htree`
* Current source tree inspection found:
  * `topology` root: 10 files
  * `htree` root: 3 files including CMake
  * `htree/characterization`: 8 files
  * `htree/construction`: 6 files
  * `htree/pattern_search`: 12 files
