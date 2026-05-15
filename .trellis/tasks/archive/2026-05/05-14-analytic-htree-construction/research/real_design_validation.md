# Real-Design Analytical H-Tree Validation

Date: 2026-05-15

Design command root:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The final A/B runs used the same freshly rebuilt `iEDA` binary copied into the `ics55_dev` design directory. Analytical mode used `research/real_design_analytical_config.json`; native mode used `research/real_design_native_config.json`.

## Functional Result

Analytical mode runs to completion on the ics55 real design, selects an analytical H-tree candidate, builds the embedding, and completes final CTS evaluation without falling back to native search.

The production analytical route now uses:

- iter-1 unit `F/D/P/W` characterization only
- exact structural source-cap operators for driven capacitance
- function-level unit composition
- native materialization and legality validation
- delay-power Pareto selection with a low-power guard band

Research-only hardcoded native diagnostic sequence injection was removed from the production HTree path. The latest accepted analytical run reports all diagnostic sequence counters as zero because no diagnostic sequence is configured.

## Current Accepted Runs

Analytical result directory:

```text
scripts/design/ics55_dev/result_analytic_current_final_audit
```

Native result directory:

```text
scripts/design/ics55_dev/result_native_current_final_audit
```

Key analytical candidate generation summary:

| Metric | Value |
| --- | ---: |
| model_sets | 5 |
| rejected_fits | 0 |
| evaluated_segments | 1844410 |
| scored_segments | 1621490 |
| decomposition_rejections | 0 |
| generated_candidates | 208 |
| validated_candidates | 52 |
| validated_pareto_candidates | 29 |
| selected_depth | 11 |
| selection_engine | analytical |
| analytical_fallback_reason | none |
| diagnostic_direct_candidates | 0 |
| diagnostic_generated_candidates | 0 |

Both final runs completed CTS synthesis, instantiation, final STA-backed evaluation, report generation, and `/usr/bin/time` accounting.

## Prior Baseline

The earlier completed baseline was `scripts/design/ics55_dev/result_analytic_enabled_iter1_frontier_diag`. An intermediate same-binary native rerun at `scripts/design/ics55_dev/result_native_current_power_guard_compare` had previously been terminated after iSTA stopped progressing during final slew propagation. The final native rerun listed above completed successfully, so the final comparison now uses the same rebuilt binary on both sides.

## Runtime

| Metric | Native final audit | Analytical final audit | Analytical - Native |
| --- | ---: | ---: | ---: |
| HTree build | 6.941 s | 5.539 s | -1.402 s |
| Downstream HTree topology | 7.111 s | 5.616 s | -1.495 s |
| Source trunk topology | 6.805 s | 6.758 s | -0.047 s |
| CTS synthesis | 16.286 s | 14.699 s | -1.587 s |
| CTS evaluation | 3.110 s | 3.140 s | +0.030 s |
| CTS total | 19.472 s | 17.920 s | -1.552 s |
| `/usr/bin/time real` | 40.66 s | 39.77 s | -0.89 s |
| `/usr/bin/time user` | 58.16 s | 59.92 s | +1.76 s |
| `/usr/bin/time sys` | 7.93 s | 5.65 s | -2.28 s |

Current judgment: analytical is faster on HTree build, downstream HTree topology, CTS synthesis, CTS total, and wall-clock `real` time under the same binary. Final STA-backed evaluation time is effectively comparable, with analytical 30 ms slower in this run.

## H-Tree Selection And QoR

| Metric | Native final audit | Analytical final audit |
| --- | --- | --- |
| selected_topology_pattern_id | `3971862` | `20` |
| selected_level_segment_pattern_ids | `251617,238047,14099,24,13,5,2,0,1,0,1` | `244438,244823,14219,24,13,5,4,0,4,0,1` |
| selected_level_buffer_counts | `L0=0,L1=1,L2=1,L3=0,L4=1,L5=0,L6=1,L7=0,L8=1,L9=0,L10=1` | `L0=1,L1=0,L2=1,L3=0,L4=1,L5=0,L6=1,L7=0,L8=1,L9=0,L10=1` |
| inserted_insts | 1381 | 1379 |
| final_clock_buffer_count | 4392 | 4390 |
| final_buffer_area | 12592.160 um^2 | 14586.880 um^2 |
| total_clock_network_wirelength | 59190.091 um | 59274.006 um |
| selected_root_driver_cell_master | `BUFX20H7L` | `BUFX20H7L` |
| H-tree delay | 0.6165 ns | 0.5396 ns |
| H-tree power | 5.099 mW | 4.866 mW |
| raw H-tree metric | 0.5155 ns / 5.074 mW | 0.4712 ns / 4.841 mW |
| root-driver compensation | 0.1010 ns / 24.902 uW | 0.0684 ns / 24.875 uW |
| setup WNS | 7.307 ns | 7.309 ns |
| hold WNS | 0.028 ns | 0.034 ns |
| suggested frequency | 371.326 MHz | 371.572 MHz |

Current judgment: analytical improves selected H-tree metric delay by 76.9 ps and reduces H-tree power by 0.233 mW versus native, while preserving the same selected root-driver master. It inserts two fewer H-tree insts and two fewer final clock buffers. The main QoR tradeoff is higher final buffer area, because the analytical-selected segment mix uses stronger cells at several weighted levels.

## Evaluation STA: H-Tree Root To Leaf

| Metric | Native final audit | Analytical final audit | Analytical - Native |
| --- | ---: | ---: | ---: |
| leaf buffer output pin count | 912 | 912 | 0 |
| arrival samples | 912 | 912 | 0 |
| STA min | 0.4417 ns | 0.4427 ns | +0.0010 ns |
| STA max | 0.5274 ns | 0.5195 ns | -0.0079 ns |
| STA mean | 0.4913 ns | 0.4921 ns | +0.0008 ns |
| STA median | 0.5005 ns | 0.5023 ns | +0.0018 ns |
| selected H-tree delay - STA mean | +0.1252 ns | +0.0475 ns | -0.0777 ns |
| selected H-tree delay - STA median | +0.1160 ns | +0.0373 ns | -0.0787 ns |
| selected H-tree delay - STA max | +0.0891 ns | +0.0201 ns | -0.0690 ns |

Current judgment: accepted analytical preserves final STA root-to-leaf delay within about 2 ps of native mean/median and improves the selected metric's delay error against final STA.

## Final Commands Run

Focused build and tests:

```bash
cmake --build build --target iEDA icts_test_flow_synthesis_htree_analytical_solver icts_test_module_analytical_characterization -j 8
./bin/icts_test_flow_synthesis_htree_analytical_solver
./bin/icts_test_module_analytical_characterization
```

Results:

- `icts_test_flow_synthesis_htree_analytical_solver`: 10/10 passed.
- `icts_test_module_analytical_characterization`: 8/8 passed.

Fallback/bypass residue search:

```bash
rg -n "fallback_to_native|analytical_fallback_to_native|falling back to native search|MakeAnalyticalDiagnosticNativeSegmentPatternIds|diagnostic_segment_pattern_ids =" \
  src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/source/module/analytical_characterization -S
```

Result:

- no production hits.
- `SolutionReport.cc` still describes `analytical_fallback_reason`, but the report text explicitly says native fallback is not used in analytical mode.

Real design commands used the same rebuilt binary:

```bash
cp /home/liweiguo/project/ecc-tools-dev/bin/iEDA /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA

cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev

env INPUT_DEF=./result/bp_be_top_place.def.gz \
    RESULT_DIR=./result_analytic_current_final_audit \
    TOOL_REPORT_DIR=./result_analytic_current_final_audit/cts \
    OUTPUT_DEF=./result_analytic_current_final_audit/iCTS_result.def \
    OUTPUT_VERILOG=./result_analytic_current_final_audit/iCTS_result.v \
    DESIGN_STAT_TEXT=./result_analytic_current_final_audit/report/cts_stat.rpt \
    DESIGN_STAT_JSON=./result_analytic_current_final_audit/report/cts_stat.json \
    TOOL_METRICS_JSON=./result_analytic_current_final_audit/metric/iCTS_metrics.json \
    CTS_CONFIG=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-14-analytic-htree-construction/research/real_design_analytical_config.json \
    timeout 8m /usr/bin/time -p ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

env INPUT_DEF=./result/bp_be_top_place.def.gz \
    RESULT_DIR=./result_native_current_final_audit \
    TOOL_REPORT_DIR=./result_native_current_final_audit/cts \
    OUTPUT_DEF=./result_native_current_final_audit/iCTS_result.def \
    OUTPUT_VERILOG=./result_native_current_final_audit/iCTS_result.v \
    DESIGN_STAT_TEXT=./result_native_current_final_audit/report/cts_stat.rpt \
    DESIGN_STAT_JSON=./result_native_current_final_audit/report/cts_stat.json \
    TOOL_METRICS_JSON=./result_native_current_final_audit/metric/iCTS_metrics.json \
    CTS_CONFIG=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-14-analytic-htree-construction/research/real_design_native_config.json \
    timeout 8m /usr/bin/time -p ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Per user instruction, no `ecc_dev_tools` / ecc dev check was run.

## Rejected Analytical Tuning Runs

| Run | Result | Reason rejected |
| --- | --- | --- |
| `result_analytic_enabled_iter1_power_guard` | QoR good, runtime slower than desired | HTree build 7.696 s, CTS total 20.745 s |
| `result_analytic_enabled_iter1_power_guard_beam128` | Runtime good, QoR bad | STA mean 0.6587 ns, selected root driver regressed to BUFX8H7L |

The accepted tuning keeps per-level shortlist and unit-compose beam at 128, while reducing cross-level top-K to 128. Lowering per-level shortlist/unit-compose beam to 96 pruned the fast validated Pareto candidates and was rejected.

## Final Technical Judgment

The expert analytical route is functionally validated on the real design without native fallback or diagnostic hardcoded candidate injection. Under the same rebuilt binary, the analytical run is faster than native overall, has lower selected H-tree delay and power, and keeps final STA root-to-leaf mean/median effectively equivalent to native. The remaining risk is coverage breadth: validation is still a single real design, so broader regression coverage is needed before enabling analytical mode by default.

## Huge Design Case: `ics55_huge_dev`

Date: 2026-05-15

Design command root:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The huge case uses the same binary copied from `bin/iEDA`, local PDK files under `/home/liweiguo/PDK/icsprout55-pdk`, and the local huge SDC at `constraints.sdc`.

Important setup note: the provided huge DEF is a post-CTS Innovus snapshot. The original top-level `clock` net only has two non-FF loads, so direct use of `design.def` with `clock -> clock` makes iCTS skip H-tree generation with `no_h_tree_levels`. The final accepted A/B therefore uses a derived experiment input, `design_clock.def`, built from `design.def` by moving all 129494 DEF `CK` sink pin memberships onto the top-level `clock` net and removing those `CK` memberships from old post-CTS subnets. The SDC and CTS config both use `clock`, not `noc_clock`.

Validation of the derived input:

| Metric | Value |
| --- | ---: |
| original DEF size | 287 MB |
| derived DEF | `scripts/design/ics55_huge_dev/design_clock.def` |
| `clock` net CK pins | 129494 |
| all NETS-section CK pins | 129494 |
| top-level clock pin on `clock` | 1 |
| CTS clock nets | 1 |
| CTS total sinks | 129494 |
| CTS FF sinks | 129494 |

Earlier `noc_clock` runs are retained only as diagnostic evidence that the binary and huge setup were runnable. They are not the final comparison because the requested clock was `clock`.

Result directories:

```text
scripts/design/ics55_huge_dev/result_clock_native_huge_final
scripts/design/ics55_huge_dev/result_clock_analytic_huge_final
```

Huge-specific configs:

```text
.trellis/tasks/05-14-analytic-htree-construction/research/real_design_huge_native_config.json
.trellis/tasks/05-14-analytic-htree-construction/research/real_design_huge_analytical_config.json
```

Both final `clock` runs completed CTS synthesis, instantiation, final STA-backed evaluation, report generation, DEF/netlist export, and `/usr/bin/time` accounting. The timeout was raised to 240 minutes for both runs.

### Huge Clock Runtime

| Metric | Native clock | Analytical clock | Analytical - Native |
| --- | ---: | ---: | ---: |
| Prepare sink loads / clustering | 244.331 s | 254.878 s | +10.547 s |
| HTree search / analytical solve | 12.499 s native search | 1.705 s analytical solve | -10.794 s |
| HTree build | 21.884 s | 10.976 s | -10.908 s |
| Downstream HTree topology | 22.080 s | 10.981 s | -11.099 s |
| Commit sink domain layout | 2.537 s | 2.485 s | -0.052 s |
| Source trunk topology | 45.365 s | 45.350 s | -0.015 s |
| CTS synthesis | 325.653 s | 325.098 s | -0.555 s |
| CTS evaluation | 56.887 s | 56.394 s | -0.493 s |
| CTS API total | 388.421 s | 387.436 s | -0.985 s |
| `/usr/bin/time real` | 436.77 s | 435.37 s | -1.40 s |
| `/usr/bin/time user` | 537.01 s | 537.43 s | +0.42 s |
| `/usr/bin/time sys` | 38.99 s | 33.46 s | -5.53 s |

Current judgment: analytical materially reduces the H-tree solver/build stage, roughly 2x on this 129494-sink `clock` case. Whole-flow wall-clock improvement is still only about 1.4 seconds because common sink preparation/clustering dominates the synthesis run at about 245-255 seconds, and final STA-backed evaluation adds about 56 seconds. The lack of a large end-to-end speedup is therefore not because the H-tree solver failed to speed up; it is because this run is dominated by non-solver stages.

### Huge Clock H-Tree Selection And QoR

| Metric | Native clock | Analytical clock |
| --- | --- | --- |
| sink_count | 129494 | 129494 |
| htree_sinks after clustering | 44962 | 44962 |
| selection_engine | `native` | `analytical` |
| analytical_fallback_reason | `none` | `none` |
| analytical_model_sets | 0 | 5 |
| analytical_rejected_fits | 0 | 0 |
| analytical_candidates | 0 | 512 |
| analytical_validated_candidates | 0 | 128 |
| analytical_validated_pareto_candidates | 0 | 56 |
| analytical_selected_pareto_power_rank | 0 | 3 |
| selected_topology_pattern_id | `8155494` | `28` |
| selected_depth | 15 | 15 |
| selected_level_segment_pattern_ids | `13777,28,32,13,13,0,3,0,2,0,1,0,1,0,1` | `14533,28,28,9,13,0,4,0,4,0,4,0,4,0,3` |
| selected_level_buffer_counts | `L0=1,L1=1,L2=1,L3=1,L4=1,L5=0,L6=1,L7=0,L8=1,L9=0,L10=1,L11=0,L12=1,L13=0,L14=1` | `L0=0,L1=1,L2=1,L3=1,L4=1,L5=0,L6=1,L7=0,L8=1,L9=0,L10=1,L11=0,L12=1,L13=0,L14=1` |
| selected_root_driver_cell_master | `BUFX20H7L` | `BUFX20H7L` |
| H-tree inserted insts | 19677 | 19675 |
| final_clock_buffer_count | 64642 | 64640 |
| final_buffer_area | 182072.800 um^2 | 237323.520 um^2 |
| total_clock_network_wirelength | 853342.287 um | 852296.881 um |
| selected H-tree delay | 1.0717 ns | 0.7994 ns |
| selected H-tree power | 171.773 mW | 197.731 mW |
| root-driver compensation | 0.0408 ns / 12.415 uW | 0.1147 ns / 12.490 uW |
| clock_path_min_buffer | 13 | 12 |
| clock_path_max_buffer | 14 | 13 |
| setup WNS | 3.522 ns | 3.520 ns |
| hold WNS | 0.098 ns | 0.105 ns |
| suggested frequency | 154.359 MHz | 154.319 MHz |
| setup worst skew / avg worst skew | 0.075 ns / 0.072 ns | 0.061 ns / 0.059 ns |
| hold worst skew / avg worst skew | -0.081 ns / -0.070 ns | -0.068 ns / -0.063 ns |

Current judgment: analytical selects a lower-delay H-tree and slightly shorter wirelength, with almost identical final buffer count and better skew numbers. The main QoR regression is area and selected H-tree power: final buffer area rises by 55250.720 um^2, about +30.3%, and selected H-tree power rises by 25.958 mW, about +15.1%. This is consistent with the analytical power-guarded minimum-delay policy choosing a stronger-cell lower-delay pattern.

### Huge Clock Evaluation STA: H-Tree Root To Leaf

| Metric | Native clock | Analytical clock | Analytical - Native |
| --- | ---: | ---: | ---: |
| leaf buffer output pin count | 13383 | 13383 | 0 |
| arrival samples | 13383 | 13383 | 0 |
| STA root AT | 0.3448 ns | 0.3448 ns | 0.0000 ns |
| STA min | 0.7897 ns | 0.7565 ns | -0.0332 ns |
| STA max | 0.9148 ns | 0.8651 ns | -0.0497 ns |
| STA mean | 0.8496 ns | 0.8121 ns | -0.0375 ns |
| STA median | 0.8535 ns | 0.8141 ns | -0.0394 ns |
| selected H-tree delay - STA min | +0.2820 ns | +0.0429 ns | -0.2391 ns |
| selected H-tree delay - STA mean | +0.2221 ns | -0.0127 ns | -0.2348 ns |
| selected H-tree delay - STA median | +0.2182 ns | -0.0147 ns | -0.2329 ns |
| selected H-tree delay - STA max | +0.1569 ns | -0.0657 ns | -0.2226 ns |

Current judgment: final STA root-to-leaf delay is better for analytical on min/max/mean/median, and the selected analytical H-tree delay is much closer to the final STA mean/median than native. The analytical selected delay slightly underestimates STA mean/median by about 13-15 ps, while native overestimates by about 218-222 ps.

### Huge Clock Commands Run

```bash
cp /home/liweiguo/project/ecc-tools-dev/bin/iEDA \
   /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev/iEDA

cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev

env INPUT_DEF=./design_clock.def \
    RESULT_DIR=./result_clock_native_huge_final \
    TOOL_REPORT_DIR=./result_clock_native_huge_final/cts \
    OUTPUT_DEF=./result_clock_native_huge_final/iCTS_result.def \
    OUTPUT_VERILOG=./result_clock_native_huge_final/iCTS_result.v \
    DESIGN_STAT_TEXT=./result_clock_native_huge_final/report/cts_stat.rpt \
    DESIGN_STAT_JSON=./result_clock_native_huge_final/report/cts_stat.json \
    TOOL_METRICS_JSON=./result_clock_native_huge_final/metric/iCTS_metrics.json \
    CTS_CONFIG=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-14-analytic-htree-construction/research/real_design_huge_native_config.json \
    /usr/bin/time -p timeout 240m ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

env INPUT_DEF=./design_clock.def \
    RESULT_DIR=./result_clock_analytic_huge_final \
    TOOL_REPORT_DIR=./result_clock_analytic_huge_final/cts \
    OUTPUT_DEF=./result_clock_analytic_huge_final/iCTS_result.def \
    OUTPUT_VERILOG=./result_clock_analytic_huge_final/iCTS_result.v \
    DESIGN_STAT_TEXT=./result_clock_analytic_huge_final/report/cts_stat.rpt \
    DESIGN_STAT_JSON=./result_clock_analytic_huge_final/report/cts_stat.json \
    TOOL_METRICS_JSON=./result_clock_analytic_huge_final/metric/iCTS_metrics.json \
    CTS_CONFIG=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-14-analytic-htree-construction/research/real_design_huge_analytical_config.json \
    /usr/bin/time -p timeout 240m ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Per user instruction, no `ecc_dev_tools` / ecc dev check was run for this task.
