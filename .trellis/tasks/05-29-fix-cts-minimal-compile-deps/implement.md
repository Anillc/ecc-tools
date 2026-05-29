# Implementation Plan

## Checklist

1. [x] Load backend development guidelines before editing.
2. [x] Update iCTS CMake target dependencies:
   - Add missing direct dependency from `topology_sink` to `icts_source_flow_synthesis_htree_characterization_library`.
   - Verify `topology_trunk` keeps direct dependencies for `HTree` and `CharacterizationLibrary`.
   - Preserve `PUBLIC` only where public headers expose dependency types.
3. [x] Reorder aggregate iCTS target links so consumer archives appear before provider archives:
   - `flow/instantiation`
   - `flow/synthesis/topology`
   - `flow/synthesis`
   - `flow`
4. [x] Break remaining static archive sensitivity:
   - Fold `IdbConversion.cc` into `icts_source_flow_instantiation` so `Wrapper::writeClocksDetailed` is resolved by the owning instantiation archive path.
   - Add `icts_source_module_characterization_headers` for header-only characterization exposure without pulling implementation archives into early consumers.
   - Split `FastStaLibertyTable::lookup` into `icts_source_database_adapter_fast_sta_liberty_model`, then link `timing` and `power` to that narrow provider.
   - Move iCTS API ownership from broad `tool_manager -> icts_source` to direct `tool_api_icts -> icts_api`.
5. [x] Reconfigure or rebuild current branch as needed.
6. [x] Build current branch `ecc_bin`:
   - `cmake --build build --target ecc_bin -j 32`
7. [x] Confirm current branch does not rely on repeated iCTS archive scans:
   - remove duplicate `libicts_source*.a` entries from the generated `ecc_bin` link command and relink successfully.
8. [x] Prepare `scripts/design/ics55_dev/iEDA` from the newly built current branch binary if needed.
9. [x] Run current branch `ics55_dev` binary validation:
   - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
10. [x] Apply the same CMake patch to `/tmp/ecc-tools-reduce-tools` without switching the current repo branch.
11. [x] Build reduced branch `ecc_bin`:
   - `cmake --build build --target ecc_bin -j 32`
12. [x] Confirm reduced branch does not rely on repeated iCTS archive scans:
   - remove duplicate `libicts_source*.a` entries from the generated `ecc_bin` link command and relink successfully.
13. [x] If reduced build still fails with static iCTS unresolved symbols:
    - Continue iCTS dependency-cycle analysis.
    - Repair direct target dependencies or split dependency direction.
    - Do not add link groups or artificial duplicate archive scans.
14. [x] Review final diff to confirm:
   - CMake-only change except a corrected iCTS include path in `CharacterizationLibrary.hh`.
   - No Python binding changes.
   - No removed tools reintroduced.

## Verification Commands

Current branch:

```bash
cmake --build build --target ecc_bin -j 32
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Reduced branch worktree:

```bash
cd /tmp/ecc-tools-reduce-tools
cmake --build build --target ecc_bin -j 32
```

Optional link-line inspection:

```bash
ninja -C build -t commands ecc_bin | tail -n 1
```

## Verification Results

- Current branch `cmake --build build --target ecc_bin -j 32`: passed.
- Current branch duplicate-iCTS-archive removal relink: passed; removed 102 duplicate `libicts_source*.a` entries and kept 81 unique entries.
- Current branch required runtime command: passed; `iCTS run successfully.`, CTS/report status finished.
- Reduced branch `/tmp/ecc-tools-reduce-tools` `cmake --build build --target ecc_bin -j 32`: passed.
- Reduced branch duplicate-iCTS-archive removal relink: passed; removed 51 duplicate `libicts_source*.a` entries and kept 81 unique entries.
- `git diff --check`: passed in both worktrees.
- `ecc dev`: intentionally not run per user request.

## Rollback Points

- Revert only the edited iCTS CMake files if current branch build regresses.
- If a proposed change introduces link groups or artificial repeated archive scans, reject it and continue direct dependency-cycle repair.

## Notes

- Do not use the `ecc_py` target as a gate for this task. `reduce_tools` has separate Python binding failures caused by removed tools and stale Python bindings.
- Do not change branches in the current repository to validate `reduce_tools`; use the existing `/tmp/ecc-tools-reduce-tools` worktree.
- Do not run ecc dev checks for this task unless explicitly requested later.
