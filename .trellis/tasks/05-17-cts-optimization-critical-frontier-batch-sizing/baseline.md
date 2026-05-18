# Baseline: current fast STA CTS binary flow

## Scope

Current fast STA baseline uses the saved `ics55_dev` logs produced after commit:

```text
2bc63329d feat(icts): add CTS fast STA timing and optimization
```

No code changes have been made after that commit except this task-document rewrite, so these logs are valid as the current pre-algorithm-change baseline.

Command represented by the logs:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Logs:

```text
scripts/design/ics55_dev/.trellis_fast_sta_opt_80ps.log
scripts/design/ics55_dev/.trellis_fast_sta_opt_40ps.log
scripts/design/ics55_dev/.trellis_fast_sta_opt_0ps.log
```

## Current Fast STA Optimization Matrix

| Target | Initial fast STA skew | Optimized fast STA skew | Improvement | Target met | Area delta | Accepted | Trials | Rejected | Cap rejected | Optimization runtime | Total CTS runtime | Final iSTA setup/hold skew |
|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---|
| 80ps | 0.0883 ns | 0.0800 ns | 0.0084 ns | true | +4.4800 um^2 | 6 | 6615 | 6596 | 0 | 12.5339 s | 25.300 s | setup 0.039 ns, hold -0.031 ns |
| 40ps | 0.0883 ns | 0.0800 ns | 0.0084 ns | false | +5.6000 um^2 | 5 | 5670 | 5652 | 0 | 10.2116 s | 22.647 s | setup 0.039 ns, hold -0.031 ns |
| 0ps | 0.0883 ns | 0.0800 ns | 0.0084 ns | false | +5.6000 um^2 | 5 | 5670 | 5652 | 0 | 10.0917 s | 21.846 s | setup 0.039 ns, hold -0.031 ns |

Transition distribution:

| Target | Transitions |
|---:|---|
| 80ps | `BUFX12H7L -> BUFX8H7L: 1`, `BUFX8H7L -> BUFX12H7L: 5` |
| 40ps | `BUFX8H7L -> BUFX12H7L: 5` |
| 0ps | `BUFX8H7L -> BUFX12H7L: 5` |

Interpretation:

- Current solver reaches 80ps but not 40ps/0ps.
- 40ps and 0ps converge to the same fixed-topology state because the current single-move greedy search finds no further cap-legal improving candidate.
- No cap rejection is observed in the current matrix.
- Optimization dominates runtime: roughly 10.1s to 12.5s of the 21.8s to 25.3s CTS runtime.

## Current Fast STA Characterization/Synthesis Baseline

Shared current values across the 80ps/40ps/0ps logs:

| Metric | Current fast STA value |
|---|---:|
| Char wirelength points | 3 |
| Char buffers | 4 |
| Char segment chars | 8030 |
| Char patterns | 87 |
| Char build runtime | 0.020 s |
| Downstream H-tree selected depth | 5 |
| Downstream H-tree inserted buffers/nets | 36 / 36 |
| Final clock buffer count | 315 |
| H-tree selected delay | 0.4971 ns |
| H-tree selected power | 167.442 uW |
| Selected root driver | `BUFX12H7L` |
| Source trunk inserted buffers/nets | 0 / 0 |

Synthesis runtime varies slightly between runs:

| Target | Synthesis runtime | Downstream H-tree build | Source trunk dispatch |
|---:|---:|---:|---:|
| 80ps | 8.246 s | 3.795 s | 2.541 s |
| 40ps | 8.152 s | 3.876 s | 2.477 s |
| 0ps | 7.755 s | 3.554 s | 2.341 s |

## Available Previous iSTA/Char-Backed Comparison Data

The best available saved pre-fast-STA binary comparison is:

```text
scripts/design/ics55_dev/result_analytical_selected_root_driver_cell_unify_current_power/run.log
scripts/design/ics55_dev/result_analytical_selected_root_driver_cell_unify_current_power/cts/cts.log
```

This run predates the fast STA migration and uses the old iSTA-backed characterization path. Evidence in the log includes `CharBuilderStaSampling`, iSTA clock propagation messages, and iPA characterization warnings.

Representative previous iSTA-backed char/synthesis values:

| Metric | Previous iSTA-backed value | Current fast STA value | Notes |
|---|---:|---:|---|
| Char wirelength points | 1 | 3 | Not a controlled A/B; old analytical mode forced iteration-1 char plus cap tail. |
| Char load points | 40 | 10 per lattice axis in setup | Old run expanded cap tail for analytical mode. |
| Char segment chars | 1180 | 8030 | Current covers more direct bins/patterns. |
| Char patterns | 5 | 87 | Current direct pattern coverage is much larger. |
| Char build runtime | 0.282 s | 0.020 s | Fast STA char is much faster despite more samples. |
| Selected H-tree depth | 5 | 5 | Same selected depth. |
| H-tree inserted buffers/nets | 20 / 20 | 36 / 36 | Topology/selection changed, so QoR comparison is not isolated to timing engine. |
| Selected H-tree delay | 0.381767 ns | 0.4971 ns | Different topology and timing口径. |
| Selected H-tree power | 409 uW | 167.442 uW | Different power model/topology; not a controlled power A/B. |
| Selected buffer masters | `BUFX16H7L:20` | selected root `BUFX12H7L`; H-tree uses 36 inserted buffers | Current concise log does not preserve a full selected-shape master histogram. |
| Final clock buffer count | 340 | 315 | Current flow has fewer final clock buffers despite more H-tree buffers. |
| Final iSTA setup skew | 0.044 ns | 0.039 ns | Current fast STA + optimization improves final iSTA setup skew by about 5ps in this comparison. |
| Final iSTA hold skew | -0.033 ns | -0.031 ns | Current improves hold magnitude by about 2ps. |
| Total CTS runtime | 7.361 s | 21.846s to 25.300s | Current includes the new optimization stage; not a like-for-like runtime comparison. |

Important limitation:

- This old run is useful for directionality but is not a controlled fast STA vs iSTA A/B. The old log used a different analytical char setup and did not include the current fast STA optimization stage.

## Previous Char-Backed Optimization Data Gap

No complete saved `ics55_dev` binary log for commit `2b88b04a6 feat(icts): add char-backed CTS buffer sizing optimization` was found in the active task artifacts or `scripts/design/ics55_dev` logs.

Available task documents describe the old char-backed optimization design, but they do not provide a complete 80ps/40ps/0ps result matrix:

```text
.trellis/tasks/05-17-cts-optimization-critical-branch-char-sizing/
```

To produce a controlled previous-optimization comparison, use a separate worktree from:

```text
2b88b04a6 feat(icts): add char-backed CTS buffer sizing optimization
```

and run the same command with the same `ics55_dev` config targets:

```bash
cd <old-commit-worktree>/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

This should be treated as a separate reproducibility step because it requires an old source tree and an old matching binary/build. The current `scripts/design/ics55_dev/iEDA` binary should not be reused for an old-commit comparison unless its build provenance is known to match that old commit.

## Baseline Conclusions

- Current fast STA char sampling is already much faster than the available old iSTA-backed char log: 0.020s vs 0.282s, while producing more segment chars and patterns.
- Current full CTS runtime is higher than the old representative run mainly because the current flow includes a 10s to 12.5s optimization stage and has a different H-tree/selection setup.
- Current fast STA optimization improves modeled skew from 88.3ps to 80.0ps, but the current single-move search stalls at the same 80.0ps floor for 40ps and 0ps targets.
- Final ordinary iSTA reporting after current fast STA optimization is about setup 39ps and hold -31ps, which is already slightly better than the representative old iSTA-backed run's setup 44ps and hold -33ps.
- A real previous char-backed optimization comparison still requires reproducing commit `2b88b04a6` in a separate worktree/build.
