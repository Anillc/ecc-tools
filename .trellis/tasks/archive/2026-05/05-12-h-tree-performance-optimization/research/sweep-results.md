# ics55_dev H-tree Sweep Results

## Scope

Reference flow:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The sweep runner mirrors that flow and only changes the generated CTS config fields:

- `wirelength_iterations`
- `slew_steps`
- `cap_steps`

`steps` means `slew_steps == cap_steps`.

## Artifacts

- Runner: `research/generated/run_ics55_htree_sweep.py`
- Tcl harness: `research/generated/run_iCTS_dev_sweep.tcl`
- Generated CTS configs: `research/generated/configs/`
- Summary CSV: `research/results/ics55_dev_htree_sweep/summary.csv`
- Summary JSON: `research/results/ics55_dev_htree_sweep/summary.json`
- Per-case preserved outputs:
  - `run.log.gz`
  - `result/cts/cts.log`
  - `result/metric/iCTS_metrics.json`
  - `result/report/cts_stat.json`
  - `result/report/cts_stat.rpt`
  - `result/cts/sta/bp_be_top.rpt`

Large reproducible outputs were removed from the task directory after metric extraction: generated DEF/Verilog, GDS/SVG visualization files, wire-path JSONs, and STA netlist copies.

## Result Table

All 15 cases completed successfully.

| case | flow_runtime_s | memory_mb | buffers | total_clock_wl | setup_wns | hold_wns | htree_delay_ns | htree_power_uW | final_frontier | feasible |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| iter1_step5 | 39.060 | 7563 | 383 | 43313694 | 7.299161 | 0.003121 | 1.0240 | 449.440 | 16030 | 9618 |
| iter1_step10 | 44.522 | 8457 | 364 | 43197862 | 7.302364 | 0.007776 | 0.6574 | 295.984 | 130104 | 77914 |
| iter1_step15 | 56.645 | 10355 | 364 | 43240691 | 7.308061 | 0.024830 | 0.5509 | 252.143 | 314077 | 167480 |
| iter2_step5 | 39.376 | 7890 | 365 | 43693365 | 7.307738 | 0.006647 | 0.7182 | 329.833 | 21368 | 12814 |
| iter2_step10 | 42.628 | 8425 | 360 | 43561302 | 7.308504 | 0.025084 | 0.5404 | 234.300 | 68850 | 41210 |
| iter2_step15 | 55.122 | 9718 | 360 | 43151203 | 7.302292 | 0.008315 | 0.4981 | 218.096 | 240555 | 128077 |
| iter3_step5 | 39.554 | 7895 | 364 | 43902045 | 7.310351 | 0.024971 | 0.6145 | 271.557 | 16096 | 9616 |
| iter3_step10 | 45.469 | 8056 | 360 | 43561302 | 7.308504 | 0.025084 | 0.4929 | 215.158 | 68627 | 41182 |
| iter3_step15 | 60.784 | 9270 | 360 | 43151203 | 7.302292 | 0.008315 | 0.4959 | 217.271 | 243981 | 130294 |
| iter4_step5 | 43.030 | 7908 | 364 | 43902045 | 7.310351 | 0.024971 | 0.6145 | 271.557 | 16096 | 9616 |
| iter4_step10 | 54.281 | 8355 | 360 | 43561302 | 7.308504 | 0.025084 | 0.4929 | 215.158 | 68627 | 41182 |
| iter4_step15 | 78.507 | 9395 | 360 | 43151203 | 7.302292 | 0.008315 | 0.4959 | 217.271 | 243981 | 130294 |
| iter5_step5 | 52.160 | 7587 | 360 | 43645296 | 7.308646 | 0.025088 | 0.5802 | 262.282 | 11431 | 6867 |
| iter5_step10 | 83.638 | 8620 | 356 | 43623776 | 7.306082 | 0.024446 | 0.5043 | 212.879 | 67571 | 40713 |
| iter5_step15 | 138.931 | 9938 | 360 | 43151203 | 7.302292 | 0.008315 | 0.4956 | 217.271 | 267330 | 142398 |

## Highlights

- Fastest case: `iter1_step5`, 39.060s.
- Slowest case: `iter5_step15`, 138.931s.
- Best H-tree delay: `iter3_step10` and `iter4_step10`, 0.4929ns.
- Best H-tree power: `iter5_step10`, 212.879uW.
- Best setup WNS: `iter3_step5` and `iter4_step5`, 7.310351ns.
- Best hold WNS: `iter5_step5`, 0.025088ns.
- Fewest CTS buffers: `iter5_step10`, 356.
- Lowest total clock wirelength: `iter2_step15`, `iter3_step15`, `iter4_step15`, and `iter5_step15`, 43,151,203.
- All selected H-tree solutions used `selection_policy = global_frontier_pareto_power_median` and `used_boundary_fallback = false`.

## Observations

- Runtime scales strongly with both dimensions, but `steps=15` at high iteration is the most expensive region.
- Higher `steps` generally improves H-tree delay/power until about `iter3_step10`; beyond that, QoR gain is small or mixed while runtime rises sharply.
- `iter3_step10` / `iter4_step10` appear to be the best balanced points for this flow: near-best H-tree delay/power with moderate runtime.
- `iter5_step10` gives the lowest H-tree power and fewest buffers, but runtime rises to 83.638s and total clock wirelength is worse than the step15 cases.
- `iter5_step15` increases final frontier count to 267,330 and runtime to 138.931s without improving timing/QoR enough to justify the cost versus `iter3_step15` or `iter3_step10`.

## Algorithm Analysis

The H-tree build path is:

1. `HTree::build()` builds a topology from clock loads.
2. It runs characterization and fills the `CharacterizationLibrary`.
3. It derives per-level length plans with `BuildLevelPlans()`.
4. It collects required length indices and builds a `SegmentFrontierRequest`.
5. `SynthesizeSegmentFrontiers()` creates only required segment frontiers.
6. `SearchTopologyDepthCandidates()` evaluates each descending depth candidate.
7. Each depth candidate calls `BuildPatternSearch()`, which walks levels leaf-to-root:
   - make segment seed entries
   - hash-join compose seed/current frontiers
   - prune by H-tree state and delay/power dominance
   - apply root-driver compensation
   - filter boundary and sink-load-region legality
8. Global feasible/candidate pools are Pareto-filtered and selected by lower power median.
9. The selected pattern is materialized by `BuildEmbedding()`.

Performance-sensitive modules:

- `HashJoinConcat()` in `src/operation/iCTS/source/module/characterization/HashJoinEngine.hh`
  - Builds a downstream hash index, probes upstream, composes pattern IDs, and performs per-group dominance pruning.
  - With pruning enabled, each group insertion scans the existing group linearly for dominance and then scans again to erase dominated entries.
- `BuildStateFrontierImpl()` in `Frontier.hh`
  - Groups by state key, sorts each group by delay/power, then keeps power-improving entries.
  - This is efficient structurally, but repeated sort/group work appears in both segment and H-tree pruning paths.
- `SynthesizeSegmentFrontiers()` in `SegmentPruning.cc`
  - Solves required length closure via dynamic programming over split pairs.
  - It composes frontiers for candidate splits and keeps a best closure solution; high `wirelength_iterations` expands possible length states/splits.
- `BuildPatternSearch()` in `TopologyPruning.cc`
  - Rebuilds a topology pattern library and repeats leaf-to-root composition per depth candidate.
  - Current selected depth is always 5 in this sweep, while depth candidates are still evaluated, so there may be reusable work across depths.
- Sink-load-region legality filtering:
  - Runs over large raw/frontier pools after topology composition.
  - It is cache-backed, but high-frontier cases still feed many entries through legality checks.

## Optimization Leads

1. Reuse/compress repeated depth candidate work:
   - In this sweep, the selected depth remained 5 for all cases.
   - Investigate whether suffix/prefix frontier compositions can be shared across adjacent depth candidates.

2. Improve hash-join pruning group handling:
   - `HashJoinConcat()` does per-candidate linear dominance scans within a group.
   - For large frontiers, consider keeping group entries ordered by delay/power or using the same sort-then-scan frontier compression strategy after batched insertion.

3. Add metrics around join/prune stages:
   - Log per-depth seed sizes, join output candidate count before pruning, post-pruning count, and time per stage.
   - Current logs expose final frontier sizes but not enough timing attribution to pinpoint the most expensive join.

4. Bound over-large frontier regions:
   - `iter5_step15` produced 267,330 final entries and took 138.931s.
   - A configurable cap or adaptive coarsening policy could stop high-cost grids when QoR has plateaued.

5. Avoid repeated global filtering when selected result stabilizes:
   - Cases `iter3_step10` and `iter4_step10` produce identical QoR/frontier summaries but different runtime.
   - This suggests extra length iterations may be producing no useful additional selected candidates for this design.
