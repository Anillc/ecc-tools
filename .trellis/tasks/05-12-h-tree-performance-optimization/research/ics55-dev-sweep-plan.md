# ics55_dev H-tree Sweep Plan

## Reference Flow

The requested reference flow is:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The script:

- Uses workspace `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev`.
- Reads `./result/bp_be_top_place.def.gz`.
- Sources DB setup from `script/DB_script/*.tcl`, including environment override support.
- Runs CTS using `run_cts -config $IEDA_CONFIG_DIR/cts_default_config.json -work_dir $TOOL_REPORT_DIR`.
- Saves DEF, Verilog, DB summary, feature summary, feature tool metrics, and CTS report.

## Matrix

- `wirelength_iterations`: `1, 2, 3, 4, 5`
- `steps`: `5, 10, 15`
- For each `steps` value, set both `slew_steps` and `cap_steps` to that value.

## Execution Strategy

For each matrix point:

1. Copy `iEDA_config/cts_default_config.json` to a task-owned generated config.
2. Add/override:
   - `wirelength_iterations`
   - `slew_steps`
   - `cap_steps`
3. Run an adapted TCL script that mirrors `run_iCTS_dev.tcl` but points `run_cts` to the generated config and writes all outputs under a case-specific task output directory.
4. Preserve:
   - generated CTS config
   - run log
   - `iCTS_metrics.json`
   - `cts_stat.json`
   - `cts.log`
   - `cts/statistics/*`

## Metrics To Extract

- Process exit status and wall runtime.
- `result/metric/iCTS_metrics.json`:
  - buffer area/num
  - clock path buffer min/max
  - clock wirelength max/total
  - max level of clock tree
  - setup/hold WNS/TNS, suggested frequency
- `result/report/cts_stat.json`:
  - flow runtime and memory if available.
- `result/cts/cts.log`:
  - HTree synthesis overview rows, especially selected depth, frontier counts, delay/power, load group statistics, and whether boundary fallback was used.

## Output Location

Use task-owned generated files under:

```text
.trellis/tasks/05-12-h-tree-performance-optimization/research/generated/
.trellis/tasks/05-12-h-tree-performance-optimization/research/results/
```

This avoids overwriting the existing `scripts/design/ics55_dev/result` reference tree.
