# Results: CTS fanout-4 optimization runtime convergence

## Source Change

The exact full-power clock-sizing solver now stops at the first valid batch that satisfies the configured skew target. It also exits without area-recovery search if the current clock-sizing state already satisfies the target.

Changed files:

- `src/operation/iCTS/source/flow/optimization/options/OptimizationOptions.hh`
- `src/operation/iCTS/source/flow/optimization/solver/OptimizationSolver.cc`

The new option is `stop_at_first_target_skew_batch = true`.

## Build And Unit Checks

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization iEDA
ctest --test-dir build --output-on-failure -R '^icts_test_'
```

Results:

- Focused build passed.
- 15 / 15 iCTS tests passed.

## Binary Validation

Both runs used:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

`scripts/design/ics55_dev/iEDA` was refreshed from the current `bin/iEDA` before validation. Build ID:

```text
60723b00ce989cbda4b3ba3de42980e2cd8877af
```

### Fanout 4

Config:

```json
"max_fanout": "4"
```

Saved logs:

```text
.trellis/tasks/05-20-cts-fanout4-optimization-runtime/fanout4_iEDA.stdout
.trellis/tasks/05-20-cts-fanout4-optimization-runtime/fanout4_iEDA.stderr
```

Key result:

| Metric | Value |
|---|---:|
| Flow status | finished |
| Solver | exact_full_power_batch |
| Initial skew | 0.0811 ns |
| Optimized skew | 0.0746 ns |
| Target met | true |
| Stop reason | target_met |
| Accepted batches | 1 |
| Accepted edits | 4 |
| Exact trials | 3 |
| Optimization elapsed | 0.625 s |
| CTS API elapsed | 16.944 s |
| Command real time | 38.70 s |

The decisive log line:

```text
Optimization: target skew reached by exact batch trial, iteration=1, candidate=3/423, total_trials=3
```

### Fanout 32

Config was changed only for this comparison run and restored to fanout 4 afterward:

```json
"max_fanout": "32"
```

Saved logs:

```text
.trellis/tasks/05-20-cts-fanout4-optimization-runtime/fanout32_iEDA.stdout
.trellis/tasks/05-20-cts-fanout4-optimization-runtime/fanout32_iEDA.stderr
```

Key result:

| Metric | Value |
|---|---:|
| Flow status | finished |
| Solver | exact_full_power_batch |
| Initial skew | 0.0883 ns |
| Optimized skew | 0.0689 ns |
| Target met | true |
| Stop reason | target_met |
| Accepted batches | 5 |
| Accepted edits | 7 |
| Exact trials | 170 |
| Optimization elapsed | 3.168 s |
| CTS API elapsed | 16.741 s |
| Command real time | 42.26 s |

The decisive log line:

```text
Optimization: target skew reached by exact batch trial, iteration=5, candidate=6/63, total_trials=170
```

## Conclusion

The fanout 4 runtime blow-up was caused by the exact solver continuing open-ended candidate scans for area recovery after the skew target had been reached. The fixed solver converges on the target-skew objective and prevents the fanout 4 run from reaching thousands of exact full-power trials.
