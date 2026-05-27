# Clean CTS Static Archive CMake Links Implementation Plan

## Steps

1. Confirm current CTS archive references with `rg`.
2. Read local CMake targets around H-tree, topology pruning, plan, characterization, and STA adapter.
3. Replace explicit archive file paths with target links.
4. Reconfigure/build focused targets and resolve any direct dependency gaps.
5. Build `iEDA`.
6. Run focused H-tree tests.
7. Run final `src/operation/iCTS` checker.
8. Report changes and leave the task uncommitted.

## Initial Findings

The current scan identified six archive path references:

- `source/flow/synthesis/htree/synthesis_state/CMakeLists.txt`
- `source/flow/synthesis/htree/CMakeLists.txt`
- `source/flow/synthesis/htree/characterization/library/CMakeLists.txt`

No broad CTS-wide archive cleanup is expected beyond these files unless a later scan reveals more.

## Validation Commands

```bash
rg -n "CMAKE_ARCHIVE_OUTPUT_DIRECTORY|\\.a\\b|libicts_.*\\.a" src/operation/iCTS -g 'CMakeLists.txt'
cmake --build build --target icts_source_flow_synthesis_htree icts_source_flow_synthesis_htree_solution icts_source_flow_synthesis_htree_analytical_solver icts_test_flow_synthesis_htree icts_test_flow_synthesis_htree_analytical_solver -- -j 16
ctest --test-dir build -R '^icts_test_flow_synthesis_htree$|^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
cmake --build build --target iEDA -- -j 16
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Implementation Notes

- Removed all explicit `${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/libicts_*.a` links from CTS CMake.
- Kept H-tree root target target-based by linking the synthesis-state target instead of compiling `SynthesisState.cc` again.
- Split `icts_source_flow_synthesis_htree_depth_plan` from the base `plan` target so `DepthPlan.cc` owns its topology-pruning dependencies without forcing the base plan target into a circular static-link workaround.
- Replaced broad `icts_source_module` dependencies in touched flow targets with the narrower module targets actually required by those translation units.
- Updated Router/Timing/Clustering/FastClustering include sites touched by the dependency cleanup to match the include roots exposed by their concrete CMake targets.

## Validation Results

- `rg -n "CMAKE_ARCHIVE_OUTPUT_DIRECTORY|\\.a\\b|libicts_.*\\.a" src/operation/iCTS -g 'CMakeLists.txt' || true`: passed, no output.
- `cmake --build build --target iEDA -- -j 16`: passed; final executable linked at `scripts/design/ics55_dev/iEDA`.
- `cmake --build build --target icts_source_flow_synthesis_htree icts_source_flow_synthesis_htree_solution icts_source_flow_synthesis_htree_analytical_solver icts_test_flow_synthesis_htree icts_test_flow_synthesis_htree_analytical_solver -- -j 16`: passed.
- `ctest --test-dir build -R '^icts_test_flow_synthesis_htree$|^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure`: passed, 2/2 tests.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`: passed with 0 in-scope findings. The checker still reported 6027 out-of-scope diagnostics in external/non-iCTS-owned paths, primarily under `src/database/...`, and those were left untouched.

## Handoff State

Implementation is intentionally left uncommitted for user review.
