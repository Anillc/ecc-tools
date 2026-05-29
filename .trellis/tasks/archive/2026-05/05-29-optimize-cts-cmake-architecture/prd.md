# 优化 CTS cmake 架构

## Goal

Make the iCTS CMake architecture internally consistent, maintainable, and free of build-order fallback behavior after the static archive ordering fix committed as `86a56d141`.

The work must fix the remaining CMake architecture issues found in the follow-up audit, not only the specific `ecc_bin` link failures that were already fixed.

## Background

- Baseline commit for this task: `86a56d141 fix(iCTS): eliminate static archive order sensitivity`.
- `ecc_bin` currently builds, and the reduced branch build was validated in the previous task.
- A broader audit of `src/operation/iCTS/**/CMakeLists.txt` found residual architecture issues:
  - orphan CMake target for `idb_conversion`;
  - missing direct dependencies hidden by transitive or upper-level targets;
  - a topology parent/child reverse dependency around `SourceTrunkStage`;
  - test-only `LINK_GROUP:RESCAN`;
  - flow subdirectory order no longer matching the CTS lifecycle;
  - inconsistent CMake formatting and broad include/link patterns.

## Requirements

- Remove or restructure orphan iCTS CMake files so every `CMakeLists.txt` under active source directories is reached intentionally from its parent.
- Ensure each iCTS target declares the provider targets for the symbols it directly uses.
- Break parent/child target cycles instead of relying on aggregate targets or archive repetition.
- Keep production CMake free of fallback link behavior:
  - no `LINK_GROUP:RESCAN`;
  - no linker start/end groups;
  - no intentional repeated archive workaround.
- Remove the test `LINK_GROUP:RESCAN` by fixing the real test target dependencies.
- Restore CTS flow directory wiring to the lifecycle order where possible:
  `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- Keep `PUBLIC` only for dependencies exposed through public headers; otherwise use `PRIVATE`.
- Prefer target links over duplicated include directories.
- Keep runtime C++ behavior unchanged unless a small source movement is necessary to break a CMake cycle.
- After changes, run the requested validation:
  - `ecc_bin` build;
  - duplicate-iCTS-archive removal relink check;
  - `ics55_dev` iCTS script;
  - full `src/operation/iCTS` `ecc_dev` check.
- Do not commit the changes from this new task until explicitly requested.

## Acceptance Criteria

- [x] No orphan `CMakeLists.txt` remains under active `src/operation/iCTS/source` directories.
- [x] The known missing direct dependencies are fixed:
  - `realization` consumers declare `database_io` instead of relying on an instantiation-stage static archive;
  - `topology_trunk -> topology_buffer`;
  - `trace_distance -> topology_buffer`.
- [x] `SourceTrunkStage` ownership no longer creates a child-to-parent target dependency.
- [x] `src/operation/iCTS/test` no longer contains `LINK_GROUP:RESCAN`.
- [x] CTS flow CMake subdirectory order matches the lifecycle order unless a documented CMake generation constraint prevents it.
- [x] Current branch builds `ecc_bin`.
- [x] Current branch duplicate-iCTS archive removal relink succeeds.
- [x] `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` succeeds.
- [x] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` succeeds or any remaining in-scope findings are fixed and rerun cleanly.
- [x] The working tree remains uncommitted after this task's implementation.

## Out Of Scope

- Reworking non-CTS tool CMake beyond what is required to keep iCTS API ownership correct.
- Large behavior refactors of CTS algorithms.
- Changing Python packaging, wheel, or `ecc_py` behavior.
- Pushing changes.

## Open Questions

- None blocking. The user requested direct implementation after the audit list.
