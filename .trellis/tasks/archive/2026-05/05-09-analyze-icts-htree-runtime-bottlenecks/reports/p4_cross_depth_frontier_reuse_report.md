# P4 Cross-Depth Topology Frontier Reuse Feasibility

Date: 2026-05-09

> Superseded on 2026-05-09 by `rollback_to_opt3_report.md`. This file is a historical analyzer report. The P4 analyzer code and focused tests described below were removed when production H-tree code was restored to `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`. P4 remains investigation-only and is not default-enabled.

## Decision

P4 is not implemented as topology frontier reuse and is not enabled by default.

Only a disabled-by-default analyzer was added:

```bash
ICTS_HTREE_DEBUG_DEPTH_REUSE_OPPORTUNITY=1
```

The analyzer records adjacent depth-candidate aligned-length sequences, exact prefix/suffix overlap, conservative exact-suffix reusable
frontier estimates, and diagnostic root-side prefix frontier sizes. It does not change candidate evaluation, pattern id assignment,
root-driver compensation, materialization, selection, or reporting.

## Why No Reuse Prototype Was Added

`TopologyPatternLibrary` pattern ids are candidate-local. Each depth candidate builds a fresh topology pattern library and assigns
`PatternId::topology(local_id)` from zero. Directly reusing topology frontier entries across depth candidates would therefore need a proven
pattern-id remap for every carried entry and every composed node.

The risk is not limited to ids:

- tie-breaks use pattern id packs after delay/power and state keys are equal;
- materialization walks candidate-local topology nodes;
- root-driver compensation depends on the candidate's topology pattern library and segment sequence;
- sink-load legality and reporting also resolve the selected topology pattern through the selected candidate-local library.

Because the measured `ics55_dev` opportunity is not an exact reusable suffix frontier, there is no low-risk exact subset worth
prototyping in this pass.

## Instrumentation

Files:

- `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc`

Analyzer behavior:

- Uses the task-local environment switch `ICTS_HTREE_DEBUG_DEPTH_REUSE_OPPORTUNITY`.
- Treats only the string value `1` as enabled; missing, empty, or any other value leaves the analyzer disabled.
- Records each candidate's canonical root-to-leaf aligned-length sequence.
- Captures per-level leaf-to-root frontier snapshots only when `ICTS_HTREE_DEBUG_DEPTH_REUSE_OPPORTUNITY=1`.
- After each adjacent depth pair, logs:
  - common root-side prefix level count;
  - common leaf-side suffix level count;
  - whether the smaller candidate is an exact prefix of the larger candidate;
  - whether the smaller candidate is an exact suffix of the larger candidate;
  - estimated reusable suffix frontier entries;
  - diagnostic prefix frontier size, reported only as a materialization/remap risk signal and not as a reusable frontier estimate.

The log field for root-side prefix size is `diagnostic_prefix_frontier_entries`. It is intentionally not named `estimated_reusable_*`
because the current leaf-to-root composition order cannot directly reuse complete smaller-depth root-side prefix frontiers.

The default path pays no snapshot storage cost because the vector stays empty unless the analyzer env var is set.

## Experiment

Command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
ICTS_HTREE_DEBUG_DEPTH_REUSE_OPPORTUNITY=1 ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifacts:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p4_depth_reuse_opportunity_debug/run.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p4_depth_reuse_opportunity_debug/time.txt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p4_depth_reuse_opportunity_debug/cts.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p4_depth_reuse_opportunity_debug/iCTS_metrics.json`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p4_depth_reuse_opportunity_debug/cts_stat.json`

Runtime for the analyzer run:

| Metric | Value |
| --- | ---: |
| external wall | 68.28 s |
| max RSS | 6180720 KB |
| CTS elapsed_time summary | 46.686 s |

## Measured Opportunity

The current `ics55_dev` explored depths `8/7/6/5`. The canonical aligned-length sequences are:

| Depth | Canonical aligned-length sequence |
| ---: | --- |
| 8 | `5,5,3,3,2,1,1,1` |
| 7 | `5,5,3,3,2,1,1` |
| 6 | `5,5,3,3,2,1` |
| 5 | `5,5,3,3,2` |

Adjacent overlap results:

| Pair | Common prefix levels | Common suffix levels | Smaller is exact prefix | Smaller is exact suffix | Estimated reusable suffix frontier entries | Diagnostic prefix frontier entries |
| --- | ---: | ---: | :---: | :---: | ---: | ---: |
| 8 -> 7 | 7 | 2 | yes | no | 0 | 220756 |
| 7 -> 6 | 6 | 1 | yes | no | 0 | 169883 |
| 6 -> 5 | 5 | 0 | yes | no | 0 | 243981 |

Interpretation:

- The smaller candidates are exact root-side prefixes of the larger candidates.
- They are not exact leaf-side suffixes.
- The existing topology composition builds from leaf to root, so the reusable frontier that would naturally be available is a suffix frontier,
  not a root-side prefix frontier.
- For this `ics55_dev` case, the exact reusable suffix frontier estimate is `0` for every adjacent pair.

There are small common suffix fragments in two pairs:

- `8 -> 7`: common suffix sequence `1,1`, frontier snapshot size `4650` in both candidates.
- `7 -> 6`: common suffix sequence `1`, frontier snapshot size `1110` in both candidates.

These fragments are not full smaller-depth candidates and would only avoid a tiny part of the candidate build. They also would not remove the
need to build the remaining root-side levels, assign candidate-local topology ids, run root compensation, filter sink-load legality, and select
from the final candidate frontier.

## Benefit Estimate

Current measured opportunity is limited.

The large diagnostic prefix sizes (`220756`, `169883`, `243981`) are not safe reusable frontier sizes. They are complete smaller-depth final
frontiers whose root-to-leaf sequences are prefixes of larger candidates. Reusing them would require reversing or restructuring the composition
order, plus remapping topology pattern libraries and preserving tie-break semantics.

The only exact leaf-side suffix fragments observed are:

- two levels for `8 -> 7`, `4650` entries;
- one level for `7 -> 6`, `1110` entries;
- none for `6 -> 5`.

These are small compared with final frontiers:

| Depth | Final frontier count |
| ---: | ---: |
| 8 | 377232 to 380607 range across prior/debug runs before/after root compensation |
| 7 | 220756 |
| 6 | 169883 |
| 5 | 243981 |

Even an exact suffix-entry-set cache would save only early leaf-side seed/compose work in this benchmark. It would not address the largest
remaining costs after P1/P2/P3/opt work.

## Risk Assessment

Risk is high for direct frontier reuse:

- **Pattern ids:** topology ids are local to each candidate library; direct reuse would corrupt materialization unless every pattern node and
  every entry is remapped.
- **Tie-break drift:** equal delay/power/state entries can be ordered by pattern id. Remapping or changing insertion order can alter deterministic
  selected topology ids.
- **Materialization:** selected patterns are materialized from the selected candidate's library. Reused frontiers must point at nodes in that same
  library.
- **Root compensation:** root closure load and compensation evaluate candidate-local topology patterns. Reused entries would need either exact
  recomputation or a proof that stable segment-sequence signatures are sufficient.
- **Reporting:** selected topology pattern ids and selected level segment pattern ids must remain explainable and candidate-local.

The only low-risk exact subset identified is read-only analysis of immutable aligned-length sequences and frontier sizes. No safe production
reuse subset was implemented.

## Validation

Focused checks run:

```bash
cmake --build build --target icts_test_flow_synthesis_htree -j $(nproc)
./bin/icts_test_flow_synthesis_htree
cmake --build build --target iEDA -j $(nproc)
```

Results:

- `icts_test_flow_synthesis_htree` passed `13/13` tests.
- Added focused coverage:
  - `AdjacentDepthReuseOpportunitySeparatesPrefixFromSuffix`
  - verifies that an `8 -> 7` prefix overlap is not reported as exact suffix reuse;
  - verifies reusable suffix estimate remains zero while prefix frontier size is only diagnostic.
- `iEDA` was rebuilt successfully before the analyzer run.
- Analyzer run completed successfully and preserved selected result:
  - selected depth `5`;
  - final frontier count `243981`;
  - feasible solutions `130294`;
  - candidate frontier fallback was `not_materialized`;
  - final clock buffer count `360`;
  - total clock network wirelength `43151.203 um`.

## Recommendation

Do not implement P4 frontier reuse now.

If P4 is revisited, the next safe investigation should be a read-only analyzer that compares complete canonical segment-pattern suffixes, not
only aligned-length suffixes. A production prototype should only be considered after proving one of these exact subsets:

1. immutable segment entry-set reuse keyed by stable segment pattern ids and electrical state, without topology pattern ids;
2. read-only composed suffix reuse with a full candidate-local pattern id remap and deterministic tie-break preservation;
3. a composition-order refactor that intentionally builds reusable root-side prefixes while proving equivalence against the current
   leaf-to-root search.

Promotion criteria should include a multi-design/config matrix and explicit checks that selected topology, selected delay/power, root
compensation, inserted buffers, reporting ids, and STA metrics remain unchanged unless a deliberate QoR tradeoff is accepted.
