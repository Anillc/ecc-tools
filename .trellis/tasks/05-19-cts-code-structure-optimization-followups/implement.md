# Implementation Plan: CTS Code Structure Optimization Leftovers

## Phase 1: Evidence and Scope

- [x] Review archived task PRD, design, implementation plan, and research notes.
- [x] Re-run current checks for file size, fallback wording, license/author metadata, and CTest visibility.
- [x] Identify confirmed leftover: current iCTS `.cc/.hh` files over 600 lines.

## Phase 2: Structure Cleanup

- [x] Split oversized files by nearest semantic responsibility until every iCTS `.cc/.hh` file is 600 lines or fewer.
- [x] Update `CMakeLists.txt` files for newly split `.cc` files.
- [x] Keep existing pre-task edits intact.

## Phase 3: Targeted Validation

- [x] Re-run the file-size scan and confirm no iCTS `.cc/.hh` file exceeds 600 lines.
- [x] Build affected iCTS targets.
- [x] Run representative affected tests where targets are available.
- [x] Confirm default iCTS CTest discovery still lists tests.

## Phase 4: Final Check

- [x] Run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

- [x] Fix in-scope findings and rerun the same command until it passes.
- [x] Summarize remaining risk, especially any files intentionally deferred.

## Validation Notes

- File-size scan: no output from `find src/operation/iCTS ... | awk '$1 > 600 ...'`.
- Representative build: `ninja -C build icts_source_database_io icts_source_database_adapter_fast_sta icts_source_database_adapter_sdc icts_source_flow_evaluation_qor icts_source_flow_synthesis_htree icts_source_flow_synthesis_htree_analytical_solver icts_source_module_routing_bst icts_source_utils_logger` passed.
- CTest discovery: `ctest --test-dir build -N -R icts` listed 15 tests.
- Final full check: `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with 0 in-scope findings.
- Residual risk: no oversized iCTS `.cc/.hh` files remain; no intentional iCTS file-size deferrals remain.

## Phase 5: Architecture Closure

- [x] Move root-level H-tree helper files into semantic subdirectories and keep only `HTree.hh/.cc` in the htree root.
- [x] Replace root-level H-tree internal helper contracts with narrow subdirectory headers.
- [x] Move optimization internals into semantic subdirectories and keep only `Optimization.hh/.cc` in the optimization root.
- [x] Replace flat optimization internal helper contracts with narrow subdirectory headers.
- [x] Update all rooted includes and CMake target wiring after moves.
- [x] Re-run file-size scan.
- [x] Build affected HTree and optimization targets.
- [x] Run:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- [x] Run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Phase 5 Validation Notes

- H-tree root scan: `CMakeLists.txt`, `HTree.cc`, `HTree.hh`.
- Optimization root scan: `CMakeLists.txt`, `Optimization.cc`, `Optimization.hh`.
- Removed flat internal headers:
  - `src/operation/iCTS/source/flow/synthesis/htree/HTreeInternal.hh`
  - `src/operation/iCTS/source/flow/optimization/OptimizationInternal.hh`
- No remaining references to `HTreeInternal` or `OptimizationInternal`.
- File-size scan: no `src/operation/iCTS` `.cc/.hh` files over 600 lines.
- Affected build passed:

```bash
ninja -C build icts_source_flow_synthesis_htree icts_source_flow_optimization icts_test_database_adapter_fast_sta iEDA
```

- `ctest --test-dir build -N -R icts` listed 15 tests.
- Final `ics55_dev` script passed with `iCTS run successfully` and report status `finished`.
- Final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with 0 in-scope findings.
