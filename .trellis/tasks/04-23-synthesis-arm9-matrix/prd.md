# analysis: synthesis arm9 case matrix

## Goal

Correct the remaining H-tree leaf-boundary capacity semantics from the misleading `leaf_driven_cap` notion to the actual `leaf_load_cap` notion, validate the behavior on targeted CTS/HTree tests, and trace where the candidate leaf-side load-cap range is narrowed in the current algorithm flow.

## What I already know

* The relevant real-tech test target is `icts_test_flow_synthesis_realtech`.
* The target test case is `ClockSynthesisRealTechSmokeTest.Arm9FullSinkNonClusteredExperimentMatrix`.
* The matrix is hard-coded in [ClockSynthesisRealTechSmokeTest.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc): `wire_length_iterations={2,3,4,5}` and `slew_cap_steps={10,15}` for 8 combinations.
* The test writes a summary report named `matrix_report.txt` under the synthesis flow artifact output root.
* Output location is controlled by `ICTS_TEST_OUTPUT_DIR`; otherwise artifacts default to `icts_test_output` near the test executable.
* The real-tech helpers probe repo-local design assets under `scripts/design/` and try to run on real ARM9 data when LEF/DEF/LIB/SDC assets are available.
* The checked-in source defaults currently come from [Config.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/database/config/Config.hh): `wire_length_iterations=5`, `slew_steps=10`, `cap_steps=10`.
* The real-tech synthesis smoke tests do not rely only on `Config::reset()`: `RealTechCharSession` currently re-applies test-side defaults from [CharacterizationRealTechTestSupport.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.hh).
* The relevant non-matrix `flow_synthesis_realtech` cases are:
  * `ClockSynthesisRealTechSmokeTest.ClusteredModeBuildsCentroidBuffersAndUsesUnrestrictedHtreeFrontier`
  * `ClockSynthesisRealTechSmokeTest.ClusteredModeForceBranchBufferedRealtechSmoke`
  * `ClockSynthesisRealTechSmokeTest.NonClusteredModeSkipsClusterBuffersAndUsesLeafUnbufferedHTree`
* The previous force-branch failure under `(3,15)` was caused by comparing real leaf boundary load coverage against `leaf_driven_cap_idx`, which was being propagated from downstream `driven_cap_idx` rather than the leaf-side `load_cap_idx`.
* `HTreeTopologyChar` already stores the downstream-most `load_cap_idx` in `CharCore`, so a separate leaf-side capacity field is not required to model leaf load-cap semantics.

## Assumptions (temporary)

* The local workspace has a usable build of `icts_test_flow_synthesis_realtech`, or it can be built without code changes.
* The local environment has access to the real-tech ARM9 assets so the test will run instead of skipping.
* Reinterpreting leaf boundary capability as `load_cap` is consistent with actual-load legality, global coverage filtering, and the user-facing diagnosis/log messages.
* Targeted unit + smoke coverage is sufficient for this semantic correction pass before rerunning any broader matrix.

## Open Questions

* Which code paths still encode the misleading `leaf_driven_cap` name or behavior?
* At what algorithm stage is the available candidate `entry_leaf_load_cap` range materially narrowed?

## Requirements (evolving)

* Replace remaining `leaf_driven_cap` leaf-boundary semantics with `leaf_load_cap` semantics in H-tree characterization/build code and relevant tests/logs.
* Ensure the final actual-load coverage check compares required real leaf load-cap against candidate `leaf_load_cap_idx`.
* Rebuild the affected characterization/htree/synthesis test targets.
* Run targeted validation for characterization join semantics, HTreeBuilder real-tech smoke, and force-branch synthesis smoke.
* Record the exact commands, runtimes, and artifact paths.
* Explain, with code references, where candidate `entry_leaf_load_cap` is narrowed in the build flow.

## Acceptance Criteria (evolving)

* [ ] `leaf_driven_cap` leaf-boundary semantics are corrected to `leaf_load_cap`.
* [ ] Affected test targets rebuild successfully.
* [ ] Targeted unit and smoke validation completes or a concrete blocker is identified.
* [ ] The leaf-load-cap tightening path is explained with concrete code references.

## Definition of Done (team quality bar)

* Semantic rename/behavior correction is internally consistent
* Commands and artifact paths are documented
* Conclusions are grounded in generated test data and source references
* Any blocker or environment caveat is explicit

## Out of Scope (explicit)

* Broad non-ARM9 synthesis benchmarking
* Re-tuning the arm9 matrix beyond targeted regression checks for this semantic fix

## Technical Notes

* Relevant target declaration: [src/operation/iCTS/test/flow/synthesis/CMakeLists.txt](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/test/flow/synthesis/CMakeLists.txt)
* Test layout and artifact behavior: [src/operation/iCTS/test/README.md](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/test/README.md)
* Shared real-tech setup facade: [src/operation/iCTS/test/common/realtech/support/RealTechSetupSupport.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/test/common/realtech/support/RealTechSetupSupport.cc)
* Real-tech characterization test defaults: [src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.hh)
* Leaf-boundary topology entry model: [src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh)
* H-tree join rule: [src/operation/iCTS/source/module/characterization/HTreeTraits.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/module/characterization/HTreeTraits.hh)
* Actual-load legality + global coverage filter: [src/operation/iCTS/source/flow/htree/HTreeBuilder.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/flow/htree/HTreeBuilder.cc)
