# Fix CTS code structure optimization leftovers

## Goal

Review the archived CTS code-structure optimization task, identify requirements that were recorded as follow-up or completed but are still not satisfied in the current tree, and remediate the confirmed leftovers without changing CTS behavior.

## Confirmed Facts

- Source task reviewed: `.trellis/tasks/archive/2026-05/05-18-cts-code-structure-optimization`.
- The archived task accepted the 600-line file-size policy for iCTS `.cc` and `.hh` files.
- The archived task explicitly left historical oversized files as a semantic-refactor follow-up instead of mechanically splitting them.
- Current verification shows license wording, required author metadata, and default iCTS CTest registration are no longer the primary leftovers:
  - old license wording count is zero in the current tree;
  - required Dawn Li author metadata is present in current iCTS `.cc/.hh` files;
  - `ctest --test-dir build -N -R icts` discovers 15 tests.
- Current verification still shows 17 iCTS `.cc/.hh` files over 600 lines, including `source/flow/optimization/OptimizationSolver.cc` at 623 lines.
- Existing uncommitted edits predate this task in:
  - `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
  - `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`
  - `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc`
  - `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc`

## Requirements

- Preserve user/worktree changes already present before this task.
- Treat oversized iCTS `.cc/.hh` files as the confirmed leftover from the archived task.
- Reduce every current oversized iCTS `.cc/.hh` file to 600 lines or fewer through behavior-preserving semantic splits.
- Keep split files in semantically correct flow subdirectories instead of using stage-root helper dumps.
- Keep `source/flow/synthesis/htree/` root focused on the `HTree.hh/.cc` core facade; place analytical, solution, compensation, and planning helpers under their existing subdirectories.
- Keep `source/flow/optimization/` root focused on the `Optimization.hh/.cc` flow facade; place optimizer options, shared model, preparation, candidate generation, state, solver, mutation, and reporting helpers under subdirectories.
- Keep new files in the same source/test layer and nearest responsibility directory.
- Update CMake target wiring before relying on newly split `.cc` files.
- Do not introduce broad service abstractions, public config changes, external-module changes, exceptions, or new singleton boundaries.
- Keep CTS flow semantics aligned with `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- Run the `ics55_dev` iCTS script after architecture closure and before the final `ecc_dev_tools` check.
- Run the requested final iCTS `ecc_dev_tools` check after implementation and fix in-scope findings.

## Acceptance Criteria

- [x] The archived task leftovers are documented with current evidence.
- [x] All current `src/operation/iCTS` `.cc/.hh` files are 600 lines or fewer.
- [x] CMake builds include all newly split implementation files.
- [x] Existing pre-task edits are preserved and not reverted.
- [x] Representative affected iCTS build/test targets pass.
- [x] `ctest --test-dir build -N -R icts` still discovers default iCTS tests.
- [x] Final command passes:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```
- [x] `source/flow/synthesis/htree/` root contains only the core HTree facade source/header plus CMake metadata.
- [x] `source/flow/optimization/` root contains only the Optimization facade source/header plus CMake metadata.
- [x] The `ics55_dev` iCTS script passes:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Completion Evidence

- File-size scan returns no `src/operation/iCTS` `.cc/.hh` files over 600 lines.
- Representative build targets passed:

```bash
ninja -C build \
  icts_source_database_io \
  icts_source_database_adapter_fast_sta \
  icts_source_database_adapter_sdc \
  icts_source_flow_evaluation_qor \
  icts_source_flow_synthesis_htree \
  icts_source_flow_synthesis_htree_analytical_solver \
  icts_source_module_routing_bst \
  icts_source_utils_logger
```

- `ctest --test-dir build -N -R icts` discovers 15 iCTS tests.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passes with 0 in-scope findings.
- Architecture closure root scan:
  - `source/flow/synthesis/htree/` contains `CMakeLists.txt`, `HTree.cc`, and `HTree.hh`.
  - `source/flow/optimization/` contains `CMakeLists.txt`, `Optimization.cc`, and `Optimization.hh`.
- Final architecture-closure build passed:

```bash
ninja -C build icts_source_flow_synthesis_htree icts_source_flow_optimization icts_test_database_adapter_fast_sta iEDA
```

- Final `ics55_dev` script passed:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- Final full check passed with 0 in-scope findings:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Architecture Closure Follow-up

Review after the initial split found that some helper files were mechanically placed in stage root directories. The remaining closure work is to move those helpers into responsibility subdirectories while preserving the 600-line policy and behavior.

## Out of Scope

- Algorithm redesign, QoR tuning, or behavior changes.
- Broad refactors outside `src/operation/iCTS`.
- Manual real-tech asset execution unless required to validate a touched file and assets are available.
- Committing or pushing changes.
