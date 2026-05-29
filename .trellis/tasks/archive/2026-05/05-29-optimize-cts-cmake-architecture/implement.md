# Implementation Plan

## Checklist

1. [x] Load iCTS backend specs before editing.
2. [x] Remove stale instantiation idb-conversion CMake:
   - delete `source/flow/instantiation/idb_conversion/CMakeLists.txt`;
   - verify no parent includes it;
   - verify `IdbConversion.cc` remains owned by `icts_source_flow_instantiation`.
3. [x] Add missing direct dependencies:
   - `realization` consumers -> `icts_source_database_io`;
   - `topology_trunk -> icts_source_flow_synthesis_topology_buffer`;
   - `trace_distance -> icts_source_flow_synthesis_topology_buffer`.
4. [x] Break `topology_trunk -> topology` reverse dependency:
   - add `SourceTrunkStage.hh/.cc`;
   - update `Topology.hh/.cc`, `SourceTrunk.hh/.cc`, and all users;
   - add/link `icts_source_flow_synthesis_topology_source_trunk_stage`.
5. [x] Restore `source/flow/CMakeLists.txt` subdirectory order to the CTS lifecycle.
6. [x] Remove test `LINK_GROUP:RESCAN`:
   - update the synthetic fast-clustering test CMake;
   - add direct test dependencies as needed.
7. [x] Run local structural audits:
   - orphan CMake scan;
   - duplicate `.cc` source ownership scan;
   - `LINK_GROUP` / archive fallback scan;
   - iCTS symbol provider closure scan.
8. [x] Build current `ecc_bin`:
   - `cmake --build build --target ecc_bin -j 32`
9. [x] Run duplicate iCTS archive removal relink check.
10. [x] Copy `bin/ecc_bin` to `scripts/design/ics55_dev/iEDA` and run:
    - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
11. [x] Run full iCTS ecc dev check:
    - `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
12. [x] Fix any in-scope findings and rerun the same failed check.
13. [x] Leave changes uncommitted.

## Rollback Points

- If `SourceTrunkStage` move becomes larger than expected, revert that move and instead introduce a narrow helper target only for `ToString`, but do not leave a child-to-parent link.
- If test link-group removal exposes unrelated non-CTS failures, document and isolate, but do not restore `LINK_GROUP` for iCTS tests.

## Validation Results

- `cmake --build build --target ecc_bin -j 32`: passed.
- `cmake --build build --target icts_test_module_topology_fast_clustering -j 32`: passed.
- `cmake --build build --target icts_test_flow -j 32`: passed.
- `cmake --build build --target icts_test_flow_synthesis -j 32`: passed.
- Local structural audit:
  - orphan `CMakeLists.txt`: 0;
  - duplicate explicit `.cc` source tokens: 0;
  - `LINK_GROUP` / linker-group fallback patterns under `src/operation/iCTS`: 0;
  - explicit `.a` references under `src/operation/iCTS`: 0.
- Duplicate iCTS archive removal relink:
  - removed duplicate iCTS archive entries: 104;
  - unique iCTS archive entries kept: 81;
  - relink succeeded.
- `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`: passed with `iCTS run successfully`.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`: passed on the final run with 0 in-scope findings.
