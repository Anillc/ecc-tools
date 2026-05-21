# Run iCTS CTS Bench Cases Design

## Scope

This task primarily creates a benchmark workspace under `scripts/design`, but it
may also add temporary iCTS/iSTA/iPA report output needed to make the requested
metrics first-class and parseable. In particular, final design power and total
clock capacitance should be emitted during the final CTS evaluation path instead
of inferred from unrelated intermediate reports.

## Workspace Layout

Create `scripts/design/ics55_cts_bench` with this intended shape:

```text
scripts/design/ics55_cts_bench/
  README.md
  iEDA
  iEDA_config/
  script/
  cases/
    <case_name>/
      place.def
      place.v
      default.sdc
      result/
  reports/
    cts_bench_summary.csv
    cts_bench_failures.csv
  tools/
    prepare_cases.py
    run_cts_bench.py
    collect_cts_metrics.py
```

The scripts may be adjusted if the repository already has a stronger local
pattern, but the workspace must keep case inputs, per-case outputs, and summary
reports separate.

## Input Data

Source cases are currently flat:

- DEF: `/nfs/share/home/liweiguo/ecc_cts_test/def/<case_name>.def`
- Verilog: `/nfs/share/home/liweiguo/ecc_cts_test/verilog/<case_name>.v`

Inspection confirmed 93 DEF files and 93 Verilog files, with no missing
counterparts by case name.

## Clock Constraint Generation

Use the existing template shape:

```tcl
set clk_name  <clock_name>
set clk_port_name <clock_port_name>
set clk_expect_freq_mhz 100
set clk_period [expr 1000.0 / $clk_expect_freq_mhz]
set clk_io_pct 0.2

set clk_port [get_ports $clk_port_name]

create_clock -name $clk_name -period $clk_period  $clk_port
```

Each generated SDC constrains exactly one 100 MHz CTS clock. For single-clock
cases, `clk_name` and `clk_port_name` can both use the DEF top input clock
port/net name.

Clock inference rule:

- Parse the DEF `PINS` section for top-level `DIRECTION INPUT` pins.
- Prefer input pins whose pin or net name matches common clock names:
  `clk`, `clock`, `ck`, or contains `clk` / `clock`.
- Include upper-case `CK` to cover ISCAS-style sequential cases.
- For multi-clock cases, choose only one clock candidate: the candidate whose
  connected DEF net has the largest load/connectivity count. The count should
  be derived from the DEF `NETS` section by counting net connections beyond the
  top-level clock pin when available. If DEF net data is insufficient, fall back
  to the candidate's observed net connection count or a documented lexical
  tie-break.
- Preserve a per-case clock-selection manifest with all candidates, counts, and
  the selected `clock_name` so ambiguous cases are auditable.

Evidence from DEF inspection:

- 82 cases have exactly one `clk`/`clock` candidate.
- 9 ISCAS-style cases have no `clk`/`clock` candidate but do expose input `CK`.
- `top` has multiple candidates: `pclk`, `clk_12mhz`, and `clk`.
- `XSTop` has multiple candidates, including `soc_clock`, `noc_clock`,
  `clock`, `clint_clock`, and DFT-related clock-looking inputs.

## iCTS Configuration

Use `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev` as the
baseline for:

- `iEDA_config/cts_default_config.json`
- `iEDA_config/db_default_config.json`
- `iEDA_config/flow_config.json`
- `script/iCTS_script/run_iCTS_dev.tcl` or an adapted CTS-only variant

The huge-dev directory does not contain local `lib/`, `lef/`, or `tlef/`
folders. Its `db_default_config.json` references absolute NFS PDK paths for
tech LEF, LEFs, and Liberty files. Those paths are currently readable, so the
bench workspace should copy the config and rewrite only per-case fields:

- `INPUT.def_path`
- `INPUT.verilog_path`
- `INPUT.sdc_path`

The iEDA binary should be copied or referenced from the newest local executable
found during planning:

`/home/liweiguo/project/ecc-tools/scripts/design/ics55_dev/iEDA`

## Run Flow

Use a CTS-only run per case:

1. Prepare a per-case run directory under `cases/<case_name>/result`.
2. Write a per-case DB config or temporary generated config pointing to that
   case's `place.def`, `place.v`, and `default.sdc`.
3. Run the selected iEDA binary with the adapted `run_iCTS_dev.tcl`.
4. Capture stdout/stderr and wall runtime for the case.
5. Keep generated `cts.log`, STA reports, statistics reports, `cts.def`, and
   `cts.v` under the case result directory.
6. Continue to the next case after a failure, recording failure status in CSV.

## Metric Source Map

Existing reports already provide several requested fields directly:

| CSV Field | Existing Source | Notes |
| --- | --- | --- |
| `Latency (ps)` | `result/cts/cts.log`, `CTS Clock Latency Skew Overview` | Convert ns to ps for the selected single CTS clock. |
| `Skew (ps)` | `result/cts/cts.log`, `CTS Clock Latency Skew Overview`; also `sta/*_setup.skew` and `sta/*_hold.skew` | Convert ns to ps. |
| `#Buffer` | `result/cts/cts.log`, `CTS Key Results.final_clock_buffer_count`; also `statistics/cell_stats.rpt` | Direct. |
| `Buf-Area (um^2)` | `result/cts/cts.log`, `CTS Key Results.final_buffer_area`; also `statistics/cell_stats.rpt` | Direct. |
| `Pow. (uW)` | New final-evaluation iPA design-power output, preferably also echoed into `result/cts/cts.log` as a key result | Must be final design power from iPA. Do not use `HTree Synthesis Overview.power` except as a diagnostic/fallback note. Convert to uW. |
| `Clk-Cap (fF)` | New final clock-cap output, preferably echoed into `result/cts/cts.log` as a key result | Must represent selected/final clock-network pin cap + wire cap. Convert to fF. |
| `Clk-WL (um)` | `result/cts/cts.log`, `CTS Key Results.total_clock_network_wirelength`; also `statistics/wirelength.rpt` `Total` | Direct. |
| `Runtime (s)` | `result/cts/cts.log`, `CTS Key Results.elapsed_time`; also `CTS Runtime Overview.total` | Direct. |
| `WNS (ps)` | `result/cts/cts.log`, `CTS Clock Timing Overview.Setup WNS (ns)` | Convert ns to ps. |
| `TNS (ps)` | `result/cts/cts.log`, `CTS Clock Timing Overview.Setup TNS (ns)` | Convert ns to ps. |

Requested fields that are not already summarized as final CSV-ready values:

- `#Fanout Vio.`, `#Cap Vio.`, `#Slew Vio.`: prefer direct STA access over
  report parsing. The implementation should count violating driver/vertex
  objects through the STA graph and slack APIs if those APIs are available in
  the CTS final evaluation context. Only if direct access is not practical
  should the collector parse `*.fanout`, `*.cap`, and `*.trans`; parsed counts
  must be marked with source metadata because existing reports may be top-N
  rather than exhaustive.

## CSV Row Policy

The CSV is case-oriented: one row per source case. Multi-clock cases still have
one row because the generated SDC intentionally selects only the largest clock
net for CTS. Include `clock_name`, and optionally `clock_port`, to make the
selected clock explicit.

## Rollback

The task creates a new script/design workspace and task artifacts. Rollback is
limited to removing:

- `scripts/design/ics55_cts_bench`
- `.trellis/tasks/05-20-icts-cts-bench-cases`

No source case files under `/nfs/share/home/liweiguo/ecc_cts_test` should be
modified.
