# Debug iCTS CTS Bench Failures Implementation Plan

## Preconditions

- Parent benchmark workspace exists at `scripts/design/ics55_cts_bench`.
- Latest benchmark reports show 9 failed cases.
- Existing uncommitted parent-task changes are preserved and not reverted.

## Checklist

- [x] Record baseline failure details for all 9 cases in
      `.trellis/tasks/05-20-icts-cts-bench-failure-debug/research/failure_debug.md`.
- [x] Inspect SDC/DEF/Verilog/logs for the `clock_trace_no_targets` cases:
      `ad_top`, `mpw_asic_top`, `retrosoc_asic`, `top`.
- [x] Fix per-case SDC or clock-selection generation where the generated clock
      target is wrong or incomplete.
- [x] Rerun each SDC-fixed case with `--skip-power` and collect metrics.
- [x] Inspect `no_h_tree_levels` cases:
      `ascon`, `s1488`, `serdes_top`.
- [x] Implement and test a legitimate source fix if a single-anchor clock
      domain should be legal.
- [x] Inspect strict-boundary infeasible cases:
      `ip2_TJUT_TOP`, `XSTop`.
- [x] Implement a source fix only if the fix keeps strict timing/electrical
      semantics coherent; otherwise write a concrete整改方案.
- [x] Rebuild affected iCTS targets.
- [x] Run focused tests for changed source behavior.
- [x] Rerun all initially failed cases.
- [x] Regenerate benchmark summary/failure CSVs.
- [x] Verify no `use_netlist`, `net_list`, or name-pattern CTS behavior was
      introduced.
- [x] Summarize per-case problem, solution, and effect for the user.

## Validation Commands

Use exact flags discovered from local scripts; expected commands include:

```bash
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --case <case> --skip-power
python3 scripts/design/ics55_cts_bench/tools/collect_cts_metrics.py
ninja -C build iEDA icts_test_flow icts_test_flow_synthesis -j8
bin/icts_test_flow --gtest_filter='FlowTest.SdcClock*:FlowTest.Wrapper*:FlowTest.ClockTreeMaterialization*'
bin/icts_test_flow_synthesis --gtest_filter='TopologyTest.*'
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Risk Points

- `ad_top`, `mpw_asic_top`, `retrosoc_asic`, and `top` may expose that the
  one-clock SDC generator selected a pad/alias rather than the internal clock
  domain. Fixing this should be explicit in generated SDC or case metadata.
- Treating single-anchor H-tree domains as success is only valid if the source
  still creates the necessary upstream connection and does not hide empty-clock
  cases.
- Strict-boundary failures may require a larger H-tree legality strategy. Do
  not force success by relaxing status checks or omitting failed sink domains.
