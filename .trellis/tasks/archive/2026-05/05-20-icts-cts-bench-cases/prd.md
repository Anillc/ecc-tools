# Run iCTS CTS Bench Cases

## Goal

Create a reproducible iCTS CTS benchmark workspace for the existing ics55 CTS
cases, run each case with the latest available iEDA/iCTS binary, and collect
the requested clock-tree QoR and timing metrics into a CSV.

## Requirements

- Reorganize the flat case source at `/nfs/share/home/liweiguo/ecc_cts_test`
  into `scripts/design/ics55_cts_bench`, using one directory per case.
- Each case directory must be named by `case_name` and contain:
  - `place.def`, copied from the matching source DEF.
  - `place.v`, copied from the matching source Verilog.
  - `default.sdc`, generated for one selected 100 MHz clock.
- Generate each `default.sdc` from case DEF clock information, using
  `scripts/design/ics55_dev/default.sdc` as the template shape.
- For cases with multiple clock-like inputs, constrain and run CTS for only one
  clock: the clock net with the largest DEF net load/connectivity count. Record
  the selected clock in the CSV and per-case notes.
- Copy or adapt the ics55 huge development CTS benchmark configuration from
  `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev` into
  `scripts/design/ics55_cts_bench`.
- Run each case with the latest available iEDA/iCTS binary in this workspace.
- Produce one CSV with one row per case containing, at minimum:
  - `case_name`
  - `clock_name`
  - `Latency (ps)`
  - `Skew (ps)`
  - `#Buffer`
  - `Buf-Area (um^2)`
  - `Pow. (uW)`
  - `Clk-Cap (fF)`
  - `Clk-WL (um)`
  - `Runtime (s)`
  - `#Fanout Vio.`
  - `#Cap Vio.`
  - `#Slew Vio.`
  - `WNS (ps)`
  - `TNS (ps)`
- Treat the duplicated requested `#Slew Vio.` column as one CSV column unless
  the user later asks for separate setup/hold slew counts.
- Preserve per-case run logs and reports so failed cases can be inspected.

## Acceptance Criteria

- [x] All 93 DEF files in `/nfs/share/home/liweiguo/ecc_cts_test/def` have
      matching Verilog files in `/nfs/share/home/liweiguo/ecc_cts_test/verilog`.
- [x] `scripts/design/ics55_cts_bench` exists and contains one case directory
      per source case, each with `place.def`, `place.v`, and `default.sdc`.
- [x] Each generated `default.sdc` creates one 100 MHz clock constraint for the
      selected clock port. Multi-clock cases select the largest clock net and
      intentionally do not constrain the other clock-like ports.
- [x] The CTS config and run scripts in `scripts/design/ics55_cts_bench` use
      the ics55 huge development config as the baseline and point each run at
      the selected case files.
- [x] The benchmark runner executes cases one by one, records pass/fail status,
      and leaves per-case logs/reports under the benchmark workspace.
- [x] The final CSV is written under `scripts/design/ics55_cts_bench`, has the
      requested metric columns, and includes enough status/error fields to
      identify cases whose reports could not be parsed.
- [x] Metric units are normalized to the requested units: ps, um^2, uW, fF, um,
      and seconds.
- [x] The implementation documents which metrics came directly from existing
      reports and which required derived parsing or additional calculation.
- [x] `Pow. (uW)` is reported from final design power by invoking iPA during the
      final STA/evaluation stage, not from the HTree synthesis candidate power.
- [x] `Clk-Cap (fF)` is reported as clock pin capacitance plus clock wire
      capacitance for the selected/final clock network.
- [x] Fanout, capacitance, and slew violation counts are obtained from STA
      interfaces when possible; report parsing is only used as an explicit
      fallback and must be marked in notes.

## Notes

- Confirmed source data count: 93 DEF files and 93 Verilog files, all matched
  by case name.
- The target directory `scripts/design/ics55_cts_bench` now exists locally and
  holds the generated benchmark workspace. The repository-wide `/scripts`
  ignore rule means the large generated workspace was not force-added as a
  normal tracked source directory.
- The latest local iEDA/iCTS binary found under `/home/liweiguo/project` is
  `/home/liweiguo/project/ecc-tools/scripts/design/ics55_dev/iEDA`, timestamped
  `2026-05-20 14:21:37`.
- The copied ics55 huge development `db_default_config.json` uses absolute NFS
  PDK paths for tech LEF, LEFs, and Liberty files. Those paths currently exist.
- User policy update: multi-clock designs should run CTS on only the largest
  clock net; final design power should come from iPA during final evaluation;
  clock capacitance should have its own output口径; violation counts should use
  STA interfaces first and report parsing only if no direct interface is
  available.
- User later requested skipping power collection for the remaining benchmark
  reruns because iPA was hanging. The runner supports `--skip-power`; rows
  produced under that mode leave `Pow. (uW)` empty and mark
  `power_source=missing_iPA_json`.
- Final collected benchmark status after failure-debug fixes: 93 rows total,
  93 passed, and `cts_bench_failures.csv` contains only the header row.
