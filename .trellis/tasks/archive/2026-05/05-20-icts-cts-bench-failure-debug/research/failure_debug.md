# iCTS CTS Bench Failure Debug Record

## Baseline

Latest parent benchmark reports before this task:

- Summary CSV: `scripts/design/ics55_cts_bench/reports/cts_bench_summary.csv`
- Failure CSV: `scripts/design/ics55_cts_bench/reports/cts_bench_failures.csv`
- Run status CSV: `scripts/design/ics55_cts_bench/reports/run_status.csv`
- Total rows: 93
- Passed: 84
- Failed: 9, all `cts_failed`

`cts_failed` means the iEDA process completed and produced `cts.log`, but
`CTS Key Results.status` was not `finished`.

## Per-Case Baseline

| Case | Selected clock | Stage | First visible reason | Baseline log |
| --- | --- | --- | --- | --- |
| `ad_top` | `clk` | read_data | `clock_trace_no_targets`; `AD_inst.clk` has 22 sequential CK sinks but is unowned by the SDC clock | `scripts/design/ics55_cts_bench/cases/ad_top/result/cts/cts.log` |
| `mpw_asic_top` | `mpw_clk_i_pad` | read_data | `clock_trace_no_targets`; selected pad clock owns no CTS target nets while many `mpw_clk_*` / `fanout_net_*` clock-like nets are unowned | `scripts/design/ics55_cts_bench/cases/mpw_asic_top/result/cts/cts.log` |
| `retrosoc_asic` | `xi_i_pad` | read_data | `clock_trace_no_targets`; selected fallback input owns no CTS target nets while many `fanout_net_*` / `s_sys_clk_*` clock-like nets are unowned | `scripts/design/ics55_cts_bench/cases/retrosoc_asic/result/cts/cts.log` |
| `top` | `clk_12mhz` | read_data | `clock_trace_no_targets`; selected clock reaches buffers with zero sequential CK sinks while `fixio_net_8_*` and `fixio_net_11` are unowned clock-like nets | `scripts/design/ics55_cts_bench/cases/top/result/cts/cts.log` |
| `ascon` | `clk` | synthesis | SDC trace succeeds; H-tree builder reports `no_h_tree_levels` and outer topology reports `unknown_h_tree_failure` for one sink domain with 5 sinks | `scripts/design/ics55_cts_bench/cases/ascon/result/cts/cts.log` |
| `s1488` | `CK` | synthesis | SDC trace succeeds; H-tree builder reports `no_h_tree_levels` and outer topology reports `unknown_h_tree_failure` for one sink domain with 6 sinks | `scripts/design/ics55_cts_bench/cases/s1488/result/cts/cts.log` |
| `serdes_top` | `clk` | synthesis | SDC trace succeeds; H-tree builder reports `no_h_tree_levels` and outer topology reports `unknown_h_tree_failure` for one sink domain with 3 sinks | `scripts/design/ics55_cts_bench/cases/serdes_top/result/cts/cts.log` |
| `ip2_TJUT_TOP` | `clk` | synthesis | H-tree search rejects all strict root-boundary candidates; final reason `no_strict_boundary_feasible_solution_any_depth` | `scripts/design/ics55_cts_bench/cases/ip2_TJUT_TOP/result/cts/cts.log` |
| `XSTop` | `noc_clock` | synthesis | 20 sink domains discovered; 14 finished and 6 failed with `no_strict_boundary_feasible_solution_any_depth` | `scripts/design/ics55_cts_bench/cases/XSTop/result/cts/cts.log` |

## Running Notes

Use `--skip-power` for debug reruns. The known power/iPA hang is outside this
task; CTS pass/fail is still determined by `CTS Key Results.status`.

## Final Outcome

Final reports after fixes:

- Summary CSV: `scripts/design/ics55_cts_bench/reports/cts_bench_summary.csv`
- Failure CSV: `scripts/design/ics55_cts_bench/reports/cts_bench_failures.csv`
- Run status CSV: `scripts/design/ics55_cts_bench/reports/run_status.csv`
- Total rows: 93
- Passed: 93
- Failed: 0

Power collection stayed disabled for debug reruns (`--skip-power`), so rerun
cases without an existing iPA JSON keep an empty `Pow. (uW)` field. Clock
capacitance is still reported from Liberty pin capacitance plus CTS RC wire
capacitance when available.

## Fix Summary

### SDC / Clock Target Corrections

Cases: `ad_top`, `mpw_asic_top`, `retrosoc_asic`, `top`.

Root cause: generated SDCs selected a visible top input/pad clock name that did
not own any CTS target sink after tracing. The actual active clock domain was on
an internal clock pin/net with sequential CK sinks. This was a benchmark setup
issue, not a CTS source bypass.

Fix:

- Added explicit per-case clock overrides in
  `scripts/design/ics55_cts_bench/clock_overrides.json`.
- Updated `scripts/design/ics55_cts_bench/tools/prepare_cases.py` so overrides
  can target either top ports or specific internal pins and emit valid
  `create_clock` SDC.
- Generated SDCs now create the single CTS clock on the real internal clock
  target for these cases:
  - `ad_top`: `{AD_inst.clk}` on `{AD_inst.cnt_0__reg_p/CK}`
  - `mpw_asic_top`: `{fanout_net_1}` on
    `{u_TopForXip.sc32b.jtagBridge_1._zz_jtag_tap_isBypass[0]_reg_p/CK}`
  - `retrosoc_asic`: `{net7538}` on
    `{u_retrosoc.u_ip_natv_wrapper.u_nmi_i2s.u_rx_async_fifo.r_mem[90][4]_reg_p/CK}`
  - `top`: `{fixio_net_11}` on `{capture.addr_0__reg_p/CK}`

Result: all four cases reran with `CTS Key Results.status = finished`.

### Single-Load H-Tree Degeneration

Cases: `ascon`, `s1488`, `serdes_top`.

Root cause: after upstream sink clustering, these domains reduced to one H-tree
anchor. `HTree::build()` treated `loads.size() == 1` as `no_h_tree_levels`,
which is an invalid failure for a legal trivial topology.

Fix:

- Updated `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc` to treat a
  single-load domain as a legal trivial H-tree:
  - `success = true`
  - `selected_depth = 0`
  - no inserted H-tree instances or nets
  - schema reason `trivial_single_load`
- Updated
  `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc` with
  `SingleLoadBuildsTrivialTopology`.

Result: all three cases reran with `CTS Key Results.status = finished`.

### Root-Driver Boundary Closure

Case: `ip2_TJUT_TOP`.

Root cause: root-driver compensation compared characterization buckets by exact
bucket equality. That rejected candidates where the selected characterization
bucket covered the physical root boundary but was not the exact same bucket.

Fix:

- Updated
  `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
  to use coverage semantics:
  - raw capacitance bucket covers physical source boundary bucket:
    `raw_cap_bucket_idx >= physical_source_boundary_bucket_idx`
  - raw input slew bucket covers root output slew bucket:
    `raw_input_slew_idx >= root_output_slew_bucket_idx`

Result: `ip2_TJUT_TOP` reran with `CTS Key Results.status = finished`.

### Full-Design STA Loop Handling

Case: `XSTop`.

Root cause: after the H-tree boundary fix, CTS synthesis and instantiation
completed, but final full-design STA encountered many combinational timing
loops through clock-gate/latch/register feedback paths. The old
`StaCombLoopCheck` found and disabled at most a narrow loop frontier, which left
large-loop coverage incomplete and made final propagation vulnerable to very
long runtime or external termination.

Fix:

- Updated `src/operation/iSTA/source/module/sta/StaCheck.cc` to break loops
  across both start-to-end and end-to-start traversals.
- The new check disables loop delay arcs structurally through STA graph arcs;
  it does not match instance/net name substrings and does not weaken benchmark
  pass/fail criteria.
- `StaSlewPropagation` and `StaDelayPropagation` already skip
  `is_loop_disable()` delay arcs, so the fix feeds the existing propagation
  semantics.

Result:

- Clean `XSTop` rerun passed:
  `python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --case XSTop --force --skip-power --timeout 3600`
- Runtime status: `passed`, exit code 0, runner runtime 520.62 s.
- CTS runtime: 297.400 s; evaluation runtime: 253.252 s.
- Final STA loop check disabled 1584 delay arcs (`fwd=4`, `bwd=1580`) before
  slew/delay propagation.
- `CTS Key Results.status = finished`.

## Final Per-Case Status

| Case | Root cause category | Resolution | Final status |
| --- | --- | --- | --- |
| `ad_top` | SDC clock target selected no CTS sinks | per-case internal CK pin clock override | passed |
| `mpw_asic_top` | SDC clock target selected no CTS sinks | per-case internal CK pin clock override | passed |
| `retrosoc_asic` | SDC clock target selected no CTS sinks | per-case internal CK pin clock override | passed |
| `top` | SDC clock target selected no CTS sinks | per-case internal CK pin clock override | passed |
| `ascon` | single-load H-tree treated as failure | trivial single-load topology support | passed |
| `s1488` | single-load H-tree treated as failure | trivial single-load topology support | passed |
| `serdes_top` | single-load H-tree treated as failure | trivial single-load topology support | passed |
| `ip2_TJUT_TOP` | root-driver boundary exact-bucket rejection | bucket coverage semantics | passed |
| `XSTop` | full-design STA loop coverage incomplete | structural bulk loop-arc disable before propagation | passed |

## Validation

Commands run:

```bash
ninja -C build iEDA icts_test_flow_synthesis_htree -j4
bin/icts_test_flow_synthesis_htree --gtest_filter='HTreeTest.*'
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --case XSTop --force --skip-power --timeout 3600
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --all --force --skip-power --timeout 3600
python3 scripts/design/ics55_cts_bench/tools/collect_cts_metrics.py
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
rg -n "use_netlist|set_use_netlist|is_use_netlist|\"net_list\"" \
  src/operation/iCTS scripts/design/sky130_gcd scripts/design/ihp130_gcd
```

Results:

- Build succeeded.
- `HTreeTest.*`: 8/8 passed.
- Final benchmark summary: 93/93 passed.
- Final `ecc_dev_tools` check: 0 in-scope findings across format, tidy,
  headers, cmake, and iwyu.
- Forbidden `use_netlist` / `net_list` strings: no matches in checked paths.
