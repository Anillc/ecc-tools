# Remove numerical H-tree

## Goal

Remove the numerical H-tree implementation and its support surface from iCTS so the codebase only keeps the traditional H-tree synthesis path.

## What I already know

* The user asked to delete numerical H-tree related code, configuration, and tests.
* The traditional CTS flow currently calls `HTreeBuilder::build`; numerical H-tree is not on the production `ClockSynthesis` path.
* Numerical H-tree files live under `src/operation/iCTS/source/flow/numerical_htree/`.
* Numerical characterization support files live under `src/operation/iCTS/source/module/numerical_characterization/`.
* Numerical H-tree tests live under `src/operation/iCTS/test/flow/numerical_htree/`.

## Assumptions

* Remove the whole numerical H-tree feature surface, including CMake targets and test targets.
* Keep traditional `flow/htree`, `module/characterization`, CTS synthesis, and other non-numerical modules intact.
* Do not add replacement functionality.

## Requirements

* Delete numerical H-tree source files.
* Delete numerical characterization support files if they are only used by numerical H-tree.
* Remove CMake references to deleted source and test directories.
* Remove stale references to `NumericalHTree*`, `numerical_htree`, and `numerical_characterization`.
* Verify the remaining iCTS code still configures/builds or at least passes targeted static checks available in the workspace.

## Acceptance Criteria

* [ ] No numerical H-tree source or test target remains in iCTS CMake wiring.
* [ ] Repository search finds no active references to deleted numerical H-tree symbols or directories.
* [ ] A relevant build/configure/test check has been run, or any blocker is documented.

## Definition of Done

* Backend specs reviewed before editing.
* Code and CMake deletions are scoped to numerical H-tree.
* Quality checks are run after edits.

## Out of Scope

* Traditional H-tree algorithm changes.
* New H-tree performance work.
* New numerical/model-based replacement.

## Technical Notes

* Current task directory: `.trellis/tasks/archive/2026-04/04-25-remove-numerical-htree/`.
