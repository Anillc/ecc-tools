# Fix CTS minimal compile deps

## Goal

Fix iCTS production CMake dependencies so `ecc_bin` does not rely on incidental static-library re-scans from unrelated tools. The fixed dependency graph must build on the current `cts_refactor` branch and remain valid after the same iCTS changes are applied to the remote `reduce_tools` branch.

## Background

- Current branch: `cts_refactor` at `849bc6ec03f25d37d716bf5d85f913c7629f0ae4`.
- Remote `reduce_tools` worktree inspected at `/tmp/ecc-tools-reduce-tools`, commit `1ed62a664302f4365ca707a8525871d9a5e806de`.
- `src/operation/iCTS` is identical between the current branch and `reduce_tools`.
- `reduce_tools` `bash build.sh -y` builds C++ sources but fails at final `ecc_bin` link.
- The failed symbols are implemented in iCTS archives, so the failure is not missing source code. It is static archive order sensitivity:
  - `icts::Wrapper::writeClocksDetailed(...)`
  - `icts::CharacterizationLibrary::buildRuntimeInput(...)`
  - `icts::CharacterizationLibrary::buildRuntimeConfig(...)`
  - `icts::CharacterizationLibrary::ensure(...)`
  - `icts::HTree::build(...)`
  - `icts::Flow::*` methods referenced by `CTSAPI.cc`
  - `icts::FastStaLibertyTable::lookup(...)`
- iCTS CMake does not hard-code `*.a` production links. The archives enter `ecc_bin` through `PUBLIC` / `INTERFACE` target propagation.
- `tool_manager` must not broadly link `icts_source`; the direct iCTS API dependency belongs to `tool_api_icts -> icts_api`.
- The full current branch currently hides this issue because unrelated tools expand the final link line and cause extra static archive repetitions. `reduce_tools` removes those tools, so the implicit extra re-scan disappears.

## Requirements

- Fix production iCTS CMake target dependencies for `ecc_bin` so iCTS no longer depends on incidental archive ordering or repeated archive scans.
- Keep the change focused on iCTS CMake unless validation proves a non-iCTS build target must be adjusted.
- Do not add deleted tools back to `reduce_tools` to mask the issue.
- Do not change iCTS runtime behavior or algorithm source unless CMake-only repair is proven insufficient.
- Prefer minimal target dependencies:
  - Use `PUBLIC` only when a target's public headers expose types or include headers from the dependency.
  - Use `PRIVATE` for implementation-only dependencies.
  - Avoid broad aggregate dependencies that only exist to influence link order.
- Strictly eliminate order sensitivity for the known failed iCTS symbols under the reduced `ecc_bin` link graph by breaking/clarifying iCTS internal dependency cycles.
- Do not use fallback link behavior such as CMake `LINK_GROUP:RESCAN`, linker start/end groups, or artificial duplicate archive scans.
- Keep Python / wheel binding failures out of scope for this task; this task only covers `ecc_bin`.

## Acceptance Criteria

- [x] Current branch builds `ecc_bin` successfully after the iCTS CMake changes.
- [x] Current branch `ics55_dev` binary flow runs successfully with the newly built binary using `cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`.
- [x] The same CMake changes can be applied to `/tmp/ecc-tools-reduce-tools`.
- [x] `reduce_tools` builds `ecc_bin` successfully.
- [x] The reduced `ecc_bin` link no longer reports unresolved iCTS symbols for `Wrapper::writeClocksDetailed`, `CharacterizationLibrary::*`, `HTree::build`, `Flow::*`, or `FastStaLibertyTable::lookup`.
- [x] No Python binding or `ecc_py` changes are required for this task.

## Out Of Scope

- Fixing `ecc_py` / wheel builds on `reduce_tools`.
- Reintroducing removed tools such as iPA, iPL, iTO, iNO, iPNP, iMP, iECO, or iIR.
- Refactoring iCTS C++ algorithms or changing CTS outputs intentionally.
- Changing top-level product packaging policy for `BUILD_PYTHON`, `BUILD_STATIC_LIB`, or wheel generation.
- Adding link-group or repeated-archive fallback behavior to mask static-library cycles.

## Open Questions

- None blocking planning. User explicitly requested implementation after clarifying that fallback link behavior is disallowed.
