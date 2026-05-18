# Results: batch-first fast STA CTS optimization

## Implementation Summary

- Added a FastSTA batch buffer-master change API so a complete sizing batch can be applied, timed, powered, and restored as a complete state.
- Added FastSTA slew status query and optimization-side no-new-violation checks for both cap and slew.
- Replaced exhaustive global single-buffer greedy search with bounded critical-frontier batch search:
  - late frontier: top-arrival sinks, upsize path/level/prefix batches;
  - early frontier: bottom-arrival sinks, downsize path/level/prefix batches;
  - all accepted states are validated by FastSTA timing, power, cap, slew, and skew.

## Validation Commands

Build:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization
ninja -C build iEDA
```

Binary pressure tests:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev
CTS_CONFIG_PATH=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_80ps.json ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl > .trellis_batch_opt_80ps.log 2>&1
CTS_CONFIG_PATH=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_40ps.json ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl > .trellis_batch_opt_40ps.log 2>&1
CTS_CONFIG_PATH=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_0ps.json ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl > .trellis_batch_opt_0ps.log 2>&1
```

Final check:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result: passed with 0 in-scope findings. The checker still reports out-of-scope diagnostics from external headers/modules, matching the existing repository baseline.

## Fast STA Optimization Matrix

| Target | Initial fast STA skew | Optimized fast STA skew | Improvement | Target met | Area delta | Accepted batches | Accepted mutations | Batch trials | Cap rejected | Slew rejected | Optimization runtime | Total CTS runtime | Final iSTA setup/hold skew |
|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 80ps | 0.0883 ns | 0.0763 ns | 0.0120 ns | true | +5.6000 um^2 | 6 | 7 | 328 | 0 | 0 | 4.339 s | 14.015 s | setup 0.020 ns, hold -0.033 ns |
| 40ps | 0.0883 ns | 0.0480 ns | 0.0403 ns | false | +22.4000 um^2 | 12 | 20 | 681 | 0 | 0 | 9.020 s | 18.804 s | setup 0.014 ns, hold -0.014 ns |
| 0ps | 0.0883 ns | 0.0480 ns | 0.0403 ns | false | +22.4000 um^2 | 12 | 20 | 681 | 0 | 0 | 8.968 s | 18.742 s | setup 0.014 ns, hold -0.014 ns |

Transition distribution:

| Target | Transitions |
|---:|---|
| 80ps | `BUFX12H7L -> BUFX8H7L: 1`, `BUFX8H7L -> BUFX12H7L: 6` |
| 40ps | `BUFX12H7L -> BUFX16H7L: 3`, `BUFX8H7L -> BUFX12H7L: 17` |
| 0ps | `BUFX12H7L -> BUFX16H7L: 3`, `BUFX8H7L -> BUFX12H7L: 17` |

## Baseline Comparison

| Target | Baseline optimized fast STA skew | Batch optimized fast STA skew | Delta | Baseline opt runtime | Batch opt runtime |
|---:|---:|---:|---:|---:|---:|
| 80ps | 0.0800 ns | 0.0763 ns | -3.7 ps | 12.5339 s | 4.339 s |
| 40ps | 0.0800 ns | 0.0480 ns | -32.0 ps | 10.2116 s | 9.020 s |
| 0ps | 0.0800 ns | 0.0480 ns | -32.0 ps | 10.0917 s | 8.968 s |

## Conclusions

- Batch-first search reaches the 80ps target with better modeled skew and lower optimization runtime than the single-move baseline.
- 40ps and 0ps now converge to 48.0ps instead of the old 80.0ps local floor.
- No cap or slew candidates were rejected in the final matrix, and all accepted final states are cap/slew legal.
- 40ps is still not met in the FastSTA modeled skew口径, but final ordinary iSTA setup/hold skew is about +/-14ps for the 40ps/0ps runs.
- The remaining 48ps FastSTA floor is a search/topology limitation under the current fixed-topology, resize-only implementation; this run does not prove physical unreachability.

## Huge Design Probe

Command setup:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev
cp -p ../ics55_dev/iEDA ./iEDA
RESULT_DIR=./result_trellis_huge_opt_80ps \
TOOL_REPORT_DIR=./result_trellis_huge_opt_80ps/cts \
OUTPUT_DEF=./result_trellis_huge_opt_80ps/iCTS_result.def \
OUTPUT_VERILOG=./result_trellis_huge_opt_80ps/iCTS_result.v \
DESIGN_STAT_TEXT=./result_trellis_huge_opt_80ps/report/cts_stat.rpt \
DESIGN_STAT_JSON=./result_trellis_huge_opt_80ps/report/cts_stat.json \
TOOL_METRICS_JSON=./result_trellis_huge_opt_80ps/metric/iCTS_metrics.json \
CTS_CONFIG=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_huge_80ps.json \
/usr/bin/time -p ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl > .trellis_huge_opt_80ps.log 2>&1
```

The copied binary hash matched `scripts/design/ics55_dev/iEDA`; the previous huge binary was saved as
`scripts/design/ics55_huge_dev/iEDA.pre_trellis_opt_backup`.

Observed result:

| Target | Status | Sink count | Synthesis runtime | Optimization status | Total elapsed before stop | Peak observed RSS | QoR |
|---:|---|---:|---:|---|---:|---:|---|
| 80ps | interrupted | 122010 | 144.638 s | did not finish after setup | 1820.45 s | ~44.3 GB | unavailable |

Notes:

- The run reached `CTS Optimization Setup` with `timing_source=cts_fast_sta_incremental`, `target_skew=0.0800 ns`, and `candidate_master_count=4`.
- No `CTS Optimization Clock Summary` was emitted before interruption, so optimized skew, area delta, accepted mutations, and cap/slew legality summary are unavailable for this run.
- The process stayed CPU-active at about one core and held roughly 44 GB RSS while the log remained unchanged after Optimization setup.
- 40ps and 0ps were not run because they would use the same or tighter optimization path after the 80ps case already failed to complete within 30 minutes.
- This indicates a huge-design scalability blocker in the current fast-STA-backed optimization flow before useful QoR can be measured on `ics55_huge_dev`.
