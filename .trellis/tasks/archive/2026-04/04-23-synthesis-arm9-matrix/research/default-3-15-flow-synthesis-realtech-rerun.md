# Default `(3,15)` Flow Synthesis Real-Tech Rerun

## Change Applied

Updated the effective default characterization configuration to:

- `wire_length_iterations = 3`
- `slew_steps = 15`
- `cap_steps = 15`

Files changed:

- [Config.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/database/config/Config.hh)
- [CharacterizationRealTechTestSupport.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.hh)

Why both files changed:

- `Config.hh` controls source/runtime defaults.
- `RealTechCharSession::prepare()` rewrites the test config from `CharacterizationRealTechTestSupport.hh`, so updating source defaults alone would not change the real-tech synthesis smoke behavior.

## Rebuild

Rebuilt:

- `cmake --build build -j 12 --target icts_test_flow_synthesis_realtech`

Binary verified:

- [bin/icts_test_flow_synthesis_realtech](/home/liweiguo/project/ecc-tools-dev/bin/icts_test_flow_synthesis_realtech)

## Tests Run

Output root:

- `.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output/`

Commands:

```bash
export ICTS_TEST_OUTPUT_DIR=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output
/usr/bin/time -p bin/icts_test_flow_synthesis_realtech --gtest_filter=ClockSynthesisRealTechSmokeTest.ClusteredModeBuildsCentroidBuffersAndUsesUnrestrictedHtreeFrontier
/usr/bin/time -p bin/icts_test_flow_synthesis_realtech --gtest_filter=ClockSynthesisRealTechSmokeTest.ClusteredModeForceBranchBufferedRealtechSmoke
/usr/bin/time -p bin/icts_test_flow_synthesis_realtech --gtest_filter=ClockSynthesisRealTechSmokeTest.NonClusteredModeSkipsClusterBuffersAndUsesLeafUnbufferedHTree
```

## Summary Table

| Case | Status | gtest elapsed | wall real | iter/step | char elapsed | HTree elapsed | selected depth | final frontier | delay | power | Note |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| clustered unrestricted | pass | 34.785 s | 35.17 s | `3 / 15` | 9.094 s | 9.570 s | 5 | 28350 | 0.1485 ns | 78.244 uW | clustering enabled, 39 cluster buffers |
| clustered force-branch-buffer | fail | 34.379 s | 34.74 s | `3 / 15` | 9.092 s | 9.209 s | - | - | - | - | skipped in HTree build |
| non-clustered leaf-unbuffered | pass | 35.093 s | 35.53 s | `3 / 15` | 8.938 s | 14.582 s | 9 | 4320 | 0.3582 ns | 888.426 uW | non-clustered, 348 inserted insts |

## Per-Case Notes

### 1. Clustered unrestricted

Artifacts:

- GTest log: [clustered gtest cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output/gtest/icts_test_flow_synthesis_realtech/clocksynthesisrealtechsmoketest/clusteredmodebuildscentroidbuffersandusesunrestrictedhtreefrontier/cts.log)
- Flow log: [clustered flow cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output/flow/synthesis/clustered_mode_realtech_smoke/cts.log)
- Stdout capture: [clustered_mode.stdout.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/clustered_mode.stdout.log)

Observed:

- Used `wire_length_iterations=3`, `slew_steps=15`, `cap_steps=15`
- Characterization grid:
  - `required_covering_iterations = 5`
  - `distinct_level_bins = 3`
  - `segment_chars = 4080`
  - `buffer_patterns = 87`
- HTree build succeeded without boundary fallback
- Result summary:
  - `selected_depth = 5`
  - `final_frontier_count = 28350`
  - `feasible_solutions = 880`
  - `inserted_insts = 20`
  - `inserted_nets = 21`
  - `cluster_buffer_count = 39`

### 2. Clustered force-branch-buffer

Artifacts:

- GTest log: [force-branch gtest cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output/gtest/icts_test_flow_synthesis_realtech/clocksynthesisrealtechsmoketest/clusteredmodeforcebranchbufferedrealtechsmoke/cts.log)
- Flow log: [force-branch flow cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output/flow/synthesis/clustered_mode_force_branch_buffered_realtech_smoke/cts.log)
- Stdout capture: [force_branch_buffer.stdout.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/force_branch_buffer.stdout.log)

Observed:

- Used `wire_length_iterations=3`, `slew_steps=15`, `cap_steps=15`
- Characterization part still completed successfully:
  - `required_covering_iterations = 5`
  - `distinct_level_bins = 3`
  - `segment_chars = 4080`
  - `buffer_patterns = 87`
  - characterization elapsed `9.092 s`
- Failure occurs in HTree build stage, not in setup or characterization

Skip/failure reason from flow log:

- `actual_boundary_load_coverage_violation required_leaf_driven_cap_idx=2, entry_leaf_driven_cap_idx=1, max_real_load_pf=0.0107723`

Interpretation:

- Under the new default, the force-branch-buffered scenario no longer finds a legal entry that covers the real boundary load requirement.
- This is a real regression signal for adopting `(3,15)` as a universal smoke/default setting.

### 3. Non-clustered leaf-unbuffered

Artifacts:

- GTest log: [non-clustered gtest cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output/gtest/icts_test_flow_synthesis_realtech/clocksynthesisrealtechsmoketest/nonclusteredmodeskipsclusterbuffersandusesleafunbufferedhtree/cts.log)
- Flow log: [non-clustered flow cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/icts_test_output/flow/synthesis/non_clustered_mode_realtech_smoke/cts.log)
- Stdout capture: [non_clustered_mode.stdout.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke/non_clustered_mode.stdout.log)

Observed:

- Used `wire_length_iterations=3`, `slew_steps=15`, `cap_steps=15`
- Characterization grid:
  - `required_covering_iterations = 10`
  - `distinct_level_bins = 7`
  - `segment_chars = 4110`
  - `buffer_patterns = 87`
- HTree build succeeded without boundary fallback
- Result summary:
  - `selected_depth = 9`
  - `final_frontier_count = 4320`
  - `feasible_solutions = 2304`
  - `inserted_insts = 348`
  - `inserted_nets = 349`
  - `cluster_buffer_count = 0`

## Result Interpretation

### Runtime

- All three tests stayed around `35s` wall-clock.
- Characterization cost is consistently about `9s`.
- The remaining time is dominated by setup plus per-scenario HTree/materialization work.
- Even though non-clustered HTree elapsed (`14.582s`) is noticeably longer than clustered unrestricted (`9.570s`), the total end-to-end runtime remains in the same rough band because setup overhead is large.

### Quality / Stability

- `clustered unrestricted`: stable under `(3,15)`
- `non-clustered leaf-unbuffered`: stable under `(3,15)`
- `clustered force-branch-buffer`: regressed under `(3,15)`

### Main Takeaway

Switching the effective default to `(3,15)` does not uniformly preserve the existing `flow_synthesis_realtech` smoke suite:

- 2 / 3 requested tests pass
- 1 / 3 requested tests fails with a concrete boundary-load-coverage violation in the force-branch-buffered path

So `(3,15)` looks viable for unrestricted clustered and non-clustered synthesis, but it is not yet safe as a blanket default for all current synthesis real-tech smoke scenarios.

## Recommendation

Do not treat `(3,15)` as universally validated yet.

The next practical options are:

1. Keep `(3,15)` as the general default and add a scenario-specific override for force-branch-buffered synthesis.
2. Revisit the force-branch-buffered path to understand whether it needs a slightly denser length/cap coverage setting or a fallback policy adjustment.
3. If strict smoke-suite stability is required immediately, do not finalize the default switch until the force-branch-buffered regression is resolved.

## Spec Update Judgment

No `.trellis/spec/` update for this task.

Reason:

- The result is actionable, but it is still benchmark/test evidence rather than a stable shared coding rule.
- The failure pattern should first be understood and either fixed or intentionally encoded as a per-scenario override before promoting it into project-wide specification text.
