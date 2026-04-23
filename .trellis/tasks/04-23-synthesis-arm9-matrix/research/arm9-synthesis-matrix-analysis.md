# ARM9 Synthesis Matrix Analysis

## Scope

Run the CTS synthesis real-tech ARM9 full-sink matrix and evaluate whether the current runtime default region is reasonable.

Target test:

- `bin/icts_test_flow_synthesis_realtech --gtest_filter=ClockSynthesisRealTechSmokeTest.Arm9FullSinkNonClusteredExperimentMatrix`

Output root:

- `.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/icts_test_output/`

Key artifacts:

- Matrix summary: [matrix_report.txt](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/icts_test_output/flow/synthesis/clock_synthesis_arm9_full_sink_matrix/matrix_report.txt)
- Flow log: [cts.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/icts_test_output/flow/synthesis/clock_synthesis_arm9_full_sink_matrix/cts.log)
- Test stdout/stderr capture: [arm9_matrix_test.stdout.log](/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/04-23-synthesis-arm9-matrix/artifacts/arm9_matrix_test.stdout.log)

## Environment Notes

- Real-tech ARM9 assets were detected successfully under `scripts/design/ics55_dev`.
- All 8 matrix points passed.
- No point used boundary fallback.
- `selected_depth=9` for all points.
- `char_grid_adapted=true` for all points.

## Default Reference

The effective default is currently from [Config.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/database/config/Config.hh), not from `cts_default_config.json`:

- `wire_length_iterations = 5`
- `slew_steps = 10`
- `cap_steps = 10`

So the default point inside this matrix is:

- `(iter=5, step=10)`

## Raw Matrix

| iter | step | runtime_s | frontier_count | delay_ns | power_w |
|---|---:|---:|---:|---:|---:|
| 2 | 10 | 4.884650 | 2700 | 0.657732 | 0.001360 |
| 2 | 15 | 8.249429 | 8735 | 0.498340 | 0.000979 |
| 3 | 10 | 5.576430 | 2220 | 0.499119 | 0.001274 |
| 3 | 15 | 13.433759 | 4320 | 0.358189 | 0.000888 |
| 4 | 10 | 16.170247 | 490 | 0.391558 | 0.001242 |
| 4 | 15 | 35.275905 | 4725 | 0.343696 | 0.000881 |
| 5 | 10 | 47.708990 | 590 | 0.379964 | 0.001238 |
| 5 | 15 | 101.407123 | 3840 | 0.293859 | 0.000880 |

Total test runtime:

- gtest case body: `251.869 s`
- full test executable wall time: `256.38 s`

## Dominance / Pareto View

Pareto points under `{min runtime, min delay, min power}`:

- `(2,10)`
- `(2,15)`
- `(3,10)`
- `(3,15)`
- `(4,15)`
- `(5,15)`

Strictly dominated points:

- `(4,10)` is dominated by `(3,15)` and `(4,15)`
- `(5,10)` default is dominated by `(3,15)` and `(4,15)`

This means the current default `(5,10)` is not a balanced compromise in this ARM9 scenario. It spends more time than several alternatives while delivering worse delay and worse power.

## Key Relative Comparisons

### Default `(5,10)` vs better points

`(3,15)` compared with default:

- runtime `13.43 s` vs `47.71 s`, `71.8%` faster
- delay `0.358 ns` vs `0.380 ns`, `5.7%` better
- power `0.000888 W` vs `0.001238 W`, `28.3%` better

`(4,15)` compared with default:

- runtime `35.28 s` vs `47.71 s`, `26.1%` faster
- delay `0.344 ns` vs `0.380 ns`, `9.5%` better
- power `0.000881 W` vs `0.001238 W`, `28.8%` better

`(5,15)` compared with default:

- runtime `101.41 s` vs `47.71 s`, `2.13x` slower
- delay `0.294 ns` vs `0.380 ns`, `22.7%` better
- power `0.000880 W` vs `0.001238 W`, `28.9%` better

### Step Increase: `10 -> 15`

At the same `iter`, increasing `step` is consistently beneficial for QoR and expensive for runtime:

- `iter=2`: runtime `+68.9%`, delay `-24.2%`, power `-28.0%`
- `iter=3`: runtime `+140.9%`, delay `-28.2%`, power `-30.3%`
- `iter=4`: runtime `+118.2%`, delay `-12.2%`, power `-29.1%`
- `iter=5`: runtime `+112.6%`, delay `-22.7%`, power `-28.9%`

Interpretation:

- More electrical resolution is buying real QoR.
- The benefit is much stronger than merely increasing `iter` at `step=10`.

### Iter Increase: `n -> n+1`

At `step=10`:

- `2 -> 3`: runtime `+14.2%`, delay `-24.1%`, power `-6.3%`
- `3 -> 4`: runtime `+190.0%`, delay `-21.6%`, power `-2.5%`
- `4 -> 5`: runtime `+195.0%`, delay `-3.0%`, power `-0.3%`

At `step=15`:

- `2 -> 3`: runtime `+62.8%`, delay `-28.1%`, power `-9.3%`
- `3 -> 4`: runtime `+162.6%`, delay `-4.1%`, power `-0.8%`
- `4 -> 5`: runtime `+187.5%`, delay `-14.5%`, power `-0.1%`

Interpretation:

- `iter=3` is the last clearly efficient jump.
- After `iter=3`, runtime grows much faster than power improvement.
- `iter=5` mainly buys extra delay quality when paired with `step=15`, but it is expensive.

## Why Runtime Grows So Fast

Per-case characterization logs show that runtime is increasingly dominated by characterization itself:

- `(2,10)`: characterization elapsed `4.29 s`, about `87.8%` of total point runtime
- `(3,15)`: characterization elapsed `12.03 s`, about `89.5%`
- `(4,15)`: characterization elapsed `33.72 s`, about `95.6%`
- `(5,10)`: characterization elapsed `47.29 s`, about `99.1%`
- `(5,15)`: characterization elapsed `100.25 s`, about `98.9%`

Scaling signals from characterization:

- `patterns`: `24 -> 87 -> 279 -> 831` as `iter` grows from `2 -> 3 -> 4 -> 5`
- `segment_chars`:
  - `(5,10)`: `14350`
  - `(5,15)`: `35070`
- `output_slew_overflow_samples`:
  - `(5,10)`: `68700`
  - `(5,15)`: `151830`

Interpretation:

- The extra time is not mainly from late-stage H-tree evaluation.
- The cost explosion is mostly coming from characterization lattice growth.
- Budget spent on large `iter` values is especially expensive once pattern count reaches `831`.

## Judgment

The current default `(5,10)` is not reasonable for this ARM9 full-sink synthesis scenario.

Reasons:

- It is strictly dominated by `(3,15)` and `(4,15)`.
- It spends a large amount of characterization time but does not capture the QoR gains that come from a finer electrical grid.
- It over-invests in wire-length iterations relative to electrical resolution.

## Suggested Sweet Spots

### Recommended balanced sweet spot: `(3,15)`

Why:

- It is much faster than default.
- It is better than default in both delay and power.
- Compared with `(4,15)`, it avoids a `2.63x` runtime jump for only about `4.0%` extra delay improvement and about `0.8%` extra power improvement.

Use when:

- You want a practical default or near-default setting for regular runs.

### Conservative higher-QoR sweet spot: `(4,15)`

Why:

- It still beats default on all three axes.
- It offers slightly better delay than `(3,15)`.

Tradeoff:

- The runtime jump from `(3,15)` to `(4,15)` is large.
- This looks better as an opt-in “quality” preset than as a general default.

### Extreme QoR mode: `(5,15)`

Why:

- Best delay and best power in the tested matrix.

Tradeoff:

- Runtime is over `2x` default and about `7.5x` `(3,15)`.
- Suitable only if this scenario justifies very aggressive characterization cost.

## Recommendation

If the goal is a more rational default for ARM9-like full-sink synthesis:

- Prefer moving the default region toward `iter=3, step=15`

If you want two presets:

- Balanced/default: `iter=3, step=15`
- High-quality: `iter=4, step=15`

Avoid keeping `iter=5, step=10` as the default for this workload, because the matrix shows it is paying a high cost in the wrong dimension.

## Spec Update Judgment

No `.trellis/spec/` update for this task.

Reason:

- The result is strong enough to guide the next config-tuning task.
- But it is still a single ARM9 real-tech workload and not yet broad enough to codify as a project-wide executable rule.
- The safer next step is to treat this as benchmark evidence, then validate the same direction on at least one additional representative workload before promoting it into a shared spec or default-setting convention.
