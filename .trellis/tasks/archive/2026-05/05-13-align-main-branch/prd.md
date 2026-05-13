# align main branch

## Goal

Prepare `cts_refactor` for a later selective sync into `main` by reverting non-essential non-CTS differences back to `main` while preserving CTS refactor behavior and CTS-facing interfaces.

## Background / Known Context

- Current branch is `cts_refactor`; `main` is the merge-base/ancestor of `cts_refactor`.
- A direct merge from `cts_refactor` into `main` would not have textual conflicts, but would carry unrelated branch-local changes.
- User-approved exclusions for prior sync analysis remain excluded from the review scope: `.trellis/`, `.claude/`, `.agent/`, `.agents/`, `.codex/`, `AGENTS.md`, `.gitignore`, and `src/apps/CMakeLists.txt`.
- `src/operation/iCTS/**` is the CTS refactor payload and should not be reverted to `main`.
- The target cleanup is to restore the previously identified category 3 and 4 differences to `main` where they do not affect CTS refactor behavior.

## Requirements

- Create and use this Trellis task for the cleanup.
- Before implementation, report when and why the category 3 and 4 differences were introduced.
- Restore category 3 differences to `main` where they are unrelated to CTS refactor:
  - iMP, py_imp, py_idb formatting, python CMake whitespace, iRT congestion Tcl, sky130 `rt.lyp`, congestion/evaluation formatting, iIR, iPL include path, operation-level CMake, global CMake/build/rust CMake, parser Rust CMake, and iSTA test/debug/report-only changes.
- Restore category 4 differences to `main`:
  - `src/database/data/design/db_design/IdbInstance.*`
  - the related non-CTS `TimingIDBAdapter::substituteCell` / DB sync changes, unless analysis shows a CTS runtime dependency.
- Preserve CTS refactor code and CTS-facing interfaces/configs.
- Preserve iCTS-required iSTA/liberty/iPA semantics such as char timing update, SDC clock-only reading, liberty unit handling, and power unit interpretation.
- User approved implementation start and direct commit after validation.
- This task does not modify CTS code; do not run `ecc_dev_tools` and do not update specs for this task.

## Acceptance Criteria

- [ ] PRD records the cleanup scope and constraints.
- [ ] Category 3 and 4 origin analysis is reported before implementation starts.
- [ ] Working tree changes restore the approved category 3 and 4 paths to `main` while preserving required CTS/iCTS behavior.
- [ ] Build is run after cleanup, or any build blocker is reported with concrete error context.
- [ ] The iCTS dev script output before and after cleanup is compared:
  `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- [ ] Final report summarizes remaining diff against `main`, confirms whether the iCTS run result is unchanged, and records the commit.

## Notes

- Out of scope: changing `src/apps/CMakeLists.txt`, Trellis/agent metadata, `.gitignore`, or iCTS refactor internals beyond what is strictly required by restore fallout.
- Out of scope: pushing.

## Research References

- [Category 3/4 origin analysis](./research/category-3-4-origin.md)
