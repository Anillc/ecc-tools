# Default `(3,15)` Force-Branch Failure Diagnosis

## Scope

Diagnose why `ClockSynthesisRealTechSmokeTest.ClusteredModeForceBranchBufferedRealtechSmoke` fails after switching the effective default characterization config to:

- `wire_length_iterations = 3`
- `slew_steps = 15`
- `cap_steps = 15`

This note records the temporary instrumentation result after rerunning the failing case and then removing the instrumentation from source.

## Reproduction

Command:

```bash
env ICTS_TEST_OUTPUT_DIR=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke_debug/icts_test_output \
  /usr/bin/time -p bin/icts_test_flow_synthesis_realtech \
  --gtest_filter=ClockSynthesisRealTechSmokeTest.ClusteredModeForceBranchBufferedRealtechSmoke
```

Observed:

- gtest elapsed: `34.380 s`
- wall time: `34.77 s`
- status: `FAILED`

Artifacts:

- Flow log: [cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke_debug/icts_test_output/flow/synthesis/clustered_mode_force_branch_buffered_realtech_smoke/cts.log)
- GTest log: [cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke_debug/icts_test_output/gtest/icts_test_flow_synthesis_realtech/clocksynthesisrealtechsmoketest/clusteredmodeforcebranchbufferedrealtechsmoke/cts.log)

## Instrumentation Result

The added debug table grouped the global candidate pool by depth candidate and counted:

- total candidate refs
- actual-load-legal refs
- refs whose `entry_leaf_driven_cap_idx` covers the required real boundary load index
- entry-side `leaf_driven_cap_idx` range
- required covering-index range from exact real-load evaluation

Key table excerpt from the flow log:

| candidate | depth | total refs | actual legal | covered | entry idx range | required idx range | max required load |
|---|---:|---:|---:|---:|---|---|---|
| 0 | 5 | 7260 | 7260 | 0 | `1` | `2` | `0.0108 pF` |
| 1 | 4 | 5520 | 5520 | 0 | `1` | `3` | `0.0260 pF` |
| 2 | 3 | 9675 | 9675 | 0 | `1-2` | `5` | `0.0451 pF` |
| 3 | 2 | 7365 | 7365 | 0 | `1-2` | `9` | `0.0837 pF` |

Source evidence:

- [flow debug table](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke_debug/icts_test_output/flow/synthesis/clustered_mode_force_branch_buffered_realtech_smoke/cts.log:241)
- [build summary with failure reason](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/default_3_15_smoke_debug/icts_test_output/flow/synthesis/clustered_mode_force_branch_buffered_realtech_smoke/cts.log:251)

## Diagnosis

The failure is **not** caused by:

- missing characterization data
- actual-load legality rejection
- topology generation failure

All global branch-buffered candidates that reached selection were already actual-load legal.

The failure happens one step later: after exact real boundary-load evaluation, every candidate is filtered out by the coverage check:

- the candidate's characterized `leaf_driven_cap_idx` is below the required covering index derived from exact boundary-load capacitance

In other words:

- `(3,15)` still produces many branch-buffered candidates
- but their downstream drive-cap coverage is too weak for this clustered force-branch scenario
- so `FilterGlobalEntriesByActualBoundaryCoverage()` removes all of them before global selection

Relevant code path:

- [coverage filter](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:1773)

The first failing depth candidate already shows the concrete mismatch:

- required leaf-driven-cap covering index = `2`
- best available branch-buffered entry index = `1`
- required exact boundary load = `0.0107723 pF`

Shallower candidates are worse:

- the required covering index rises to `3`, `5`, and `9`
- while entry-side coverage only reaches `1` or `2`

## Practical Implication

`(3,15)` is still a good runtime/QoR region for the ARM9 non-clustered matrix, but it is **not** a universally safe default for the current real-tech smoke suite.

For the force-branch-buffered clustered path, the new default under-characterizes branch-buffered terminal coverage relative to the exact real boundary load.

## Next Validation Options

1. Keep `(3,15)` as the general default, but add a scenario-specific override for force-branch-buffered synthesis.
2. Test whether a denser branch-buffer-safe point such as `(4,15)` restores coverage for this scenario.
3. If smoke-suite stability is the primary requirement, do not finalize `(3,15)` as a blanket default until this path is handled explicitly.
