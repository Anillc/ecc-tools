# iCTS H-tree Runtime Optimization Report

Date: 2026-05-09

## Scope

This report covers the production optimization follow-up for the `0.5/0.5` iCTS H-tree runtime bottleneck found by the earlier temporary instrumentation run.

Run command for every experiment:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Runtime config confirmed in `scripts/design/ics55_dev/iEDA_config/cts_default_config.json`:

- `max_buf_tran = 0.5 ns`
- `max_sink_tran = 0.5 ns`
- `max_cap = 0.15 pF`

The temporary `HTREE_PROBE` instrumentation from the investigation phase was removed before the baseline and all optimization experiments. The only remaining source changes are production optimization code and focused tests.

## Implemented Optimizations

### 1. Exact O(N log N) delay/power Pareto extraction

Files:

- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`

Change:

- Replaced the nested O(N^2) delay/power dominance scan used by `SelectBestHTreeChar` and `SelectBestGlobalEntry` with an exact sort + grouped scan.
- Sort key is delay ascending, power ascending, then existing deterministic pattern/tie ordering.
- The scan is grouped by equal delay. Within one delay group it keeps every entry whose power equals the group minimum when no lower-delay entry has power `<=` it.
- This exactly matches the previous dominance relation: equal delay/equal power entries do not dominate each other, so tie multiplicity is preserved.

Correctness argument:

For two objectives where lower delay and lower power are better, an entry is dominated iff a previous lower-delay group has power `<= entry.power`, or an equal-delay entry has strictly lower power. The grouped scan encodes exactly those two conditions and keeps equal-delay/equal-power duplicates.

### 2. Exact per-depth Pareto compression before global selection

Files:

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`

Change:

- Added `BuildPerDepthDelayPowerParetoRefs`.
- After exact global sink-load coverage filtering, feasible refs are grouped by `candidate_index` and compressed to each depth candidate's exact delay/power Pareto set.
- Fallback candidate-pool compression is lazy: it only runs if no strict feasible global selection exists.

Correctness argument:

This compression is applied after sink-load legality and leaf-cap coverage filtering, so it does not remove a dominated entry that could survive legality while its dominator fails legality. A point dominated within the same depth by another already-covered point is also dominated in the global union and cannot appear in the final global delay/power Pareto set. Lazy fallback avoids doing candidate-pool work when strict feasible selection succeeds.

Experiment note:

An eager fallback variant was also tested and archived at `artifacts/opt2_per_depth_pareto/`; it was slower because it compressed the large fallback candidate pool even though the strict feasible pool selected successfully. The production patch uses the lazy variant reported below.

### 3. Root-driver compensation root-load signature cache

Files:

- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`

Change:

- Replaced the per-topology-pattern root-load cache with a `RootClosureLoadSignature` cache.
- Signature contains the root-to-leaf segment-pattern prefix up to and including the first real buffered segment. If no real root-side buffer exists, it contains the full level segment-pattern sequence and marks `ends_at_real_buffer = false`.
- The cache is scoped to one `RootDriverCompensationPass`, so it does not cross CTS runs or runtime configs.
- `beginCandidateBuild()` no longer clears root-load estimates; topology pattern ids are local to one depth, but the new signature is expressed in stable segment pattern ids.

Correctness argument:

`ResolveRootClosureLoadEstimate` computes terminals by walking from the H-tree root through unbuffered levels until the first real buffer. Downstream levels after the first root-side real buffer do not affect root closure load. For no-buffer cases, the full level sequence determines the active leaf boundary. Therefore this signature is conservative: it may miss some physically equivalent cases with different segment pattern ids, but it does not merge distinct root-closure structures.

## Runtime Results

Artifacts are under `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/`.

| Run | Synthesis s | Delta vs previous | Delta vs baseline | Total s | External wall s | Wall delta vs previous | Max RSS MB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Clean baseline | 91.532 | - | - | 107.114 | 128.67 | - | 6345.3 |
| Opt1 exact O(N log N) Pareto | 46.942 | -44.590, 48.7% faster | -44.590, 48.7% faster | 63.147 | 85.19 | -43.48 | 6339.3 |
| Opt2 per-depth Pareto, lazy fallback | 46.855 | -0.087, 0.2% faster | -44.677, 48.8% faster | 62.738 | 84.56 | -0.63 | 6345.4 |
| Opt3 root-load signature cache | 31.766 | -15.089, 32.2% faster | -59.766, 65.3% faster | 48.294 | 70.15 | -14.41 | 6345.9 |

Interpretation:

- Opt1 accounts for the largest improvement because the measured global selector hotspot was O(N^2) over 362k feasible refs.
- Opt2 is exact and useful structurally, but once Opt1 makes final Pareto extraction O(N log N), the incremental runtime gain in this single design is small. Its main value is bounding future global selection input and reducing risk if frontend pools grow further.
- Opt3 recovers most of the remaining root-driver compensation cost by removing repeated root-load physical estimates across equivalent root-side topology prefixes.
- Cumulative synthesis runtime improved from `91.532 s` to `31.766 s`, a `59.766 s` reduction or `65.3%` faster synthesis stage.
- Cumulative external wall time improved from `128.67 s` to `70.15 s`, a `58.52 s` reduction.

## QoR Consistency

The key selected H-tree and final CTS metrics were unchanged across all reported runs:

| Metric | Value |
| --- | --- |
| selected depth | 5 |
| selected topology pattern id | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` |
| selected delay | 0.4959 ns |
| selected power | 217.271 uW |
| raw H-tree char metric | 0.2897 ns / 192.458 uW |
| root-driver compensation | 0.2063 ns / 24.813 uW |
| selected physical root load | 0.1428 pF |
| final CTS buffer count | 360 |
| total clock network wirelength | 43151.203 um |
| setup WNS | 7.302292 ns |
| hold WNS | 0.008315 ns |

This validates that the implemented changes are exact for this benchmark: no selected topology, delay/power, root-load, buffer count, timing, or wirelength drift was observed.

## Validation

Commands run:

```bash
cmake --build build --target iEDA -j $(nproc)
cmake --build build --target iEDA icts_test_flow_synthesis_htree -j $(nproc)
./bin/icts_test_flow_synthesis_htree
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Validation results:

- `iEDA` release binary built successfully at `scripts/design/ics55_dev/iEDA`.
- `icts_test_flow_synthesis_htree` passed 6 tests, including new coverage for exact tie-preserving global selection and per-depth Pareto group independence.
- Full `src/operation/iCTS` checker passed with `0` in-scope findings. It still reports `4930` out-of-scope diagnostics from external database/liberty headers and related external modules, unchanged in scope and not introduced by this patch.
- All four runtime experiments completed successfully and saved `run.log`, `time.txt`, `cts.log`, `iCTS_metrics.json`, and `cts_stat.json` under their artifact directories.

## Current Worktree Notes

Current production source changes are limited to:

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh`
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc`

Task-local artifacts and reports remain under:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/`

No temporary instrumentation source file remains, and no other untracked Trellis task directory remains.
