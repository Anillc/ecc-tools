# ics55 CTS Bench

This workspace prepares and runs the `/nfs/share/home/liweiguo/ecc_cts_test`
CTS benchmark cases with the latest local iEDA/iCTS binary referenced by
`./iEDA`.

## Layout

```text
cases/<case_name>/
  place.def
  place.v
  default.sdc
  clock_selection.json
  run_config/db_default_config.json
  result/

reports/
  clock_selection.csv
  run_status.csv
  cts_bench_summary.csv
  cts_bench_failures.csv
```

## Commands

Prepare cases:

```bash
python3 scripts/design/ics55_cts_bench/tools/prepare_cases.py
```

Run one smoke case:

```bash
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --case gcd --force
```

Run all prepared cases sequentially:

```bash
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --all --force
```

Collect metrics:

```bash
python3 scripts/design/ics55_cts_bench/tools/collect_cts_metrics.py
```

## Metric Notes

- Multi-clock cases intentionally run CTS on one clock only: the DEF clock-like
  input whose connected net has the largest connection count.
- `Pow. (uW)` is parsed from the final post-CTS iPA `report_power -json` output.
- `Clk-Cap (fF)` is derived as clock pin cap plus clock wire cap:
  - pin cap comes from final `cts.v` clock-net loads and Liberty input pin
    capacitance,
  - wire cap comes from iCTS final clock wirelength times the reported routing
    unit capacitance.
- Violation counts use the latest binary's generated STA violation reports.
  The collector marks these as `report_top_n_parse` because the direct STA
  violation-count interfaces are not exposed as Tcl commands in the binary.
