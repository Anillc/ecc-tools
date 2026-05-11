# iCTS H-tree Runtime Complexity and Next Optimization Opportunities

Date: 2026-05-09

> Superseded on 2026-05-09 by `rollback_to_opt3_report.md` for next-step recommendations. This report was written before the later P1/P2/P3/P4/P6 experiments. After those experiments, production H-tree code was restored to the opt3 backup state because P1/P6 had negligible runtime impact in this fixture and P2/P3/P4 remained investigation-only.

## Scope

This report analyzes the current iCTS H-tree runtime after the opt3 root-load signature cache experiment and recommends remaining algorithmic optimization opportunities in priority order.

This is report-only work. No CTS source code was changed for this report.

Inputs reviewed:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/prd.md`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/instrumentation_report.md`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/reports/runtime_optimization_report.md`
- opt0/opt1/opt2/opt3 artifacts under `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc`
- `src/operation/iCTS/source/module/characterization/Frontier.hh`
- `src/operation/iCTS/source/module/characterization/HashJoinEngine.hh`
- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh`
- `src/operation/iCTS/source/module/characterization/SegmentTraits.hh`

## Executive Summary

After opt3, the original dominant bottlenecks have largely been removed:

- Exact global delay/power Pareto selection was changed from `O(N^2)` to exact sort/scan `O(N log N)`.
- Per-depth exact Pareto compression now bounds final global selection input to small per-depth Pareto sets.
- Root-driver compensation no longer performs one physical root-load FLUTE estimate per topology entry; it reuses estimates by root-prefix load signature.

Measured production runtime improved from:

| Run | Synthesis runtime | Total CTS runtime | External wall time |
| --- | ---: | ---: | ---: |
| opt0 clean baseline | 91.532 s | 107.114 s | 128.67 s |
| opt1 exact Pareto scan | 46.942 s | 63.147 s | 85.19 s |
| opt2 per-depth Pareto lazy fallback | 46.855 s | 62.738 s | 84.56 s |
| opt3 root-load signature cache | 31.766 s | 48.294 s | 70.15 s |

The remaining optimization space is no longer one single `O(N^2)` hotspot. It is mostly search-space organization:

1. The fallback candidate path is still partly eager even when strict feasible selection succeeds.
2. Sink-load-region legality and leaf-cap coverage are still applied after large frontiers are generated and after root-driver compensation.
3. Depth candidates are evaluated independently even though adjacent depths share level sequences and segment frontiers.
4. The H-tree state frontier still carries many lattice states and pattern variants to the root before some exact legality facts are known.
5. Characterization is now a large fixed component relative to the optimized H-tree build.

Highest priority recommendation: make the fallback candidate pipeline lazy end-to-end, then prototype strict-feasible sink-load coverage before root-driver compensation with explicit equivalence checks. The fallback-laziness direction is exact for final selection when strict feasible selection succeeds. The pre-compensation coverage direction attacks a real remaining scan/frontier cost, but it needs a proof or exhaustive comparison because the current code performs compensation-aware state-frontier pruning before sink-load coverage.

## Current opt3 Runtime Composition

### CTS-Level Runtime

From `artifacts/opt3_root_load_signature_cache/cts.log`:

| Stage | Elapsed time | Share of total CTS |
| --- | ---: | ---: |
| read_data | 8.577 s | 17.8% |
| synthesis | 31.766 s | 65.8% |
| instantiation | 0.011 s | ~0.0% |
| evaluation | 7.937 s | 16.4% |
| total | 48.294 s | 100.0% |

External process timing from `time.txt`:

| Metric | Value |
| --- | ---: |
| wall_seconds | 70.15 s |
| user_seconds | 92.22 s |
| sys_seconds | 15.62 s |
| max_rss_kb | 6,498,200 KB |

Synthesis is still the largest CTS stage, but after opt3 it is not dominated by the old global selection or root-load FLUTE loop.

### HTree-Level Runtime

From opt3 `run.log` schema stage timestamps:

- `[HTree][START] build`: 10:45:05.343438
- `[HTree][RUNNING] characterization`: 10:45:05.345769
- HTree characterization overview emitted: 10:45:13.515383
- HTree synthesis overview emitted: 10:45:23.058912
- `[HTree][FINISHED] build`: elapsed `17.715 s`

This gives a coarse opt3 breakdown:

| Component | Time | Basis | Interpretation |
| --- | ---: | --- | --- |
| HTree build total | 17.715 s | schema stage elapsed | 55.8% of CTS synthesis |
| HTree characterization | ~8.170 s | log timestamp delta | about 46% of HTree build |
| HTree search/selection/embedding/reporting after characterization | ~9.545 s | residual within HTree build | about 54% of HTree build |
| Other synthesis work outside HTree | ~14.051 s | `31.766 - 17.715` | clustering/source-trunk/flow overhead outside this H-tree build |

Important limitation: opt3 was not rerun with detailed substage probes. The table above is coarse stage timing. Detailed substage attribution below uses the earlier instrumented run plus measured production deltas from opt1/opt2/opt3.

### Search-Space Size That Still Exists

The opt3 run still reports the same selected H-tree and QoR metrics as opt0/opt1/opt2:

| Metric | Value |
| --- | ---: |
| sink count | 8751 |
| topology loads after sink clustering | 319 |
| full topology depth | 8 |
| depth candidates evaluated | 4 |
| selected depth | 5 |
| selected topology pattern id | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` |
| selected-depth final frontier count | 243981 |
| selected-depth candidate frontier entries | 236991 |
| selected-depth feasible solutions | 130294 |
| selected-depth feasible frontier entries | 126566 |
| selected H-tree inserted buffers | 40 |
| final CTS buffer count | 360 |
| setup WNS | 7.302292 ns |
| hold WNS | 0.008315 ns |

From the earlier detailed instrumentation run, the same search regime had:

| Quantity | Value |
| --- | ---: |
| segment chars | 19515 |
| per-depth final frontier total | 1011852 |
| per-depth compensated entries | 1015227 |
| global feasible refs before coverage | 536458 |
| global feasible refs after coverage | 362592 |
| global candidate refs before coverage | 1004862 |
| global candidate refs after coverage | 679256 |
| sink-region legality real evaluations | 31 |
| sink-region cache hits/monotone prunes | 3082611 / 10717 |

The selected-depth frontier count did not shrink after opt1/opt2/opt3. The improvements came from reducing selection complexity and avoiding redundant root-load estimation, not from reducing the number of generated topology states.

## Current Theoretical Complexity

Definitions:

- `D`: number of depth candidates. Current run: `D = 4`.
- `L_d`: H-tree levels in depth candidate `d`. Current candidates include depths 8, 7, 6, and 5.
- `C`: segment characterization count. Current run: `C = 19515`.
- `P_char`: characterized segment pattern count across all direct wirelength bins. Current run: `P_char = 87`.
- `S_slew` / `S_cap`: slew and cap lattice steps. Current run: `15 / 15`.
- `C_sta`: average STA/liberty query cost per characterization sample.
- `F_d`: final post-compensation topology frontier size for depth candidate `d`.
- `G_f`: global strict-feasible candidate refs before global sink-load coverage.
- `G_c`: global fallback candidate refs before global sink-load coverage.
- `K_d`: exact delay/power Pareto size for depth candidate `d` after coverage.
- `J_l`: hash-join matches while composing one level.
- `W_l`: average state-frontier width inside one pruner group.
- `S`: number of distinct sink-load legality signatures.
- `R`: number of distinct root-load signatures after opt3.

### Characterization

Current behavior:

- `RunCharacterizationFlow` creates segment chars across wirelength bins, slew bins, cap bins, and buffer patterns.
- opt3 produced `19515` segment chars and `19515` executed STA samples.

Complexity:

```text
T_char = O(P_char * S_slew * S_cap * C_sta)
```

For this run, characterization is now a major fixed HTree component at roughly `8.17 s`. It is not exploding with topology frontier size, but after opt1/opt3 it has become a larger fraction of remaining HTree time.

### Segment Frontier Synthesis

Current behavior:

- `SynthesizeSegmentEntrySets` groups base segment chars by length and synthesizes missing required lengths with dynamic programming.
- Composition uses `HashJoinConcat<SegmentChar, SegmentTraits>`.
- The hash join indexes downstream entries by `(input_slew_idx, driven_cap_idx)` and probes upstream by `(output_slew_idx, load_cap_idx)`.
- State frontier pruning groups by exact electrical state and keeps non-dominated delay/power entries.

Complexity:

```text
T_segment = O(C log C + sum_over_required_length_splits(|U| + |D| + J * W_l))
```

Measured earlier:

- Segment entry synthesis was `0.355 s`.
- Source-to-root segment selection was about `2 ms` despite `24152` strict candidates.

Conclusion: segment frontier synthesis and source-trunk segment selection are not current priority bottlenecks.

### Topology Pattern Search

Current behavior:

- `BuildPatternSearch` builds each depth candidate from leaf to root.
- For each level it creates seed entries from the segment frontier and composes them with the current topology frontier through `HashJoinConcat<HTreeTopologyChar, HTreeTraits>`.
- `HTreeTraits` joins on:
  - `downstream.input_slew_idx == upstream.output_slew_idx`
  - `downstream.driven_cap_idx == ceil(upstream.load_cap_idx / 2)`
- The pruner groups by exact H-tree frontier state:
  - root input slew
  - driven cap
  - leaf load cap
  - output slew
  - root load cap
  - source-boundary switching power
  - terminal semantic
  - monotonic boundary state

Complexity:

```text
T_pattern(d) = O(sum_l(|seed_l| + |frontier_{l+1}| + J_l * W_l))
T_depth_search = O(sum_d T_pattern(d))
```

Earlier instrumentation showed topology pattern search before the opt3 root-load cache:

| Depth | Pattern search total | Compose | Root compensation | Final frontier |
| ---: | ---: | ---: | ---: | ---: |
| 8 | 8.916 s | 2.731 s | 6.115 s | 377232 |
| 7 | 4.975 s | 1.481 s | 3.452 s | 220756 |
| 6 | 3.680 s | 1.162 s | 2.485 s | 169883 |
| 5 | 5.064 s | 1.461 s | 3.550 s | 243981 |
| total | 22.635 s | 6.835 s | 15.602 s | 1011852 |

After opt3, the repeated physical root-load estimate portion has largely disappeared, but the topology composition/search space itself still exists. This is the main remaining algorithmic search-space area.

### Root-Driver Compensation

Before opt3:

```text
T_comp_old = O(F_total * (resolve_root_load + direct_cost_lookup/cache))
```

Observed:

- `1015227` compensated entries.
- `1015227` load resolutions.
- `1015227` FLUTE route estimates.
- only `68` unique direct Liberty cost keys.
- `15.077 s` of `15.602 s` compensation time was root-load resolution.

After opt3:

```text
T_comp_opt3 = O(F_total * (signature_build + cell_master_resolution + cache_lookup + entry_update)
                + R * resolve_root_load
                + U_cost * direct_cost_lookup)
```

The expensive route estimate is now keyed by `RootClosureLoadSignature`, which contains the root-to-leaf segment-pattern prefix up to the first real buffer, or the full sequence when no real root-side buffer exists. This is exact if that signature fully determines root closure terminals and wire load.

Remaining cost is expected to be per-entry signature construction, root-driver cell-master resolution, compensation detail lookup, and applying compensated delay/power to each kept entry. That is much smaller than the old route-estimate loop but still proportional to frontier size.

### Sink-Load-Region Filtering

Current behavior:

- `FilterSinkLoadRegionLegalEntries` scans every input entry and calls `ResolveSinkLoadRegionLegality`.
- `ResolveSinkLoadRegionLegality` materializes the topology pattern, resolves a signature based on bottom-most buffered level and segment pattern, then uses a cache.
- Actual cluster electrical evaluation is already strongly cached.
- A separate global coverage filter later scans global feasible and candidate ref pools again and checks `entry.leaf_load_cap_idx >= required_leaf_load_cap_covering_idx`.

Complexity:

```text
T_sink_filter = O(M * signature_resolution + S * exact_cluster_eval)
```

`S` is tiny in this run (`31` real evaluations), but `M` is large. The earlier probe saw more than `3.0M` cache hits/monotone prunes. This means the remaining issue is not cluster electrical evaluation; it is repeated scan/signature/filter organization.

### Global Selection

Before opt1:

```text
T_select_old = O(N^2)
```

The old global selector reduced `362592` feasible refs to `120` Pareto entries in `46.764 s`.

After opt1/opt2:

```text
T_select_opt3 = O(sum_d N_d log N_d + K log K)
```

where `N_d` are covered refs per depth and `K = sum_d K_d`. Earlier local exact Pareto sizes were small:

| Depth | Local Pareto size |
| ---: | ---: |
| 8 | 224 |
| 7 | 264 |
| 6 | 216 |
| 5 | 248 |
| total | ~952 |

Global delay/power selection is no longer a priority bottleneck. However, the covered candidate fallback pool is still computed eagerly before strict feasible selection is known, which is now a visible algorithmic organization issue.

## What the Implemented Optimizations Changed

| Opt | Change | Complexity before | Complexity after | Measured effect | QoR risk observed |
| --- | --- | --- | --- | ---: | --- |
| opt1 | Exact delay/power Pareto sort/scan | `O(N^2)` global/local selector | `O(N log N)` exact selector | synthesis `91.532 -> 46.942 s`, `-44.590 s` | none observed |
| opt2 | Per-depth exact Pareto compression before final global selection; candidate compression lazy | final global selector could see hundreds of thousands of refs | final strict selector sees sum of per-depth Pareto refs after per-depth compression | synthesis `46.942 -> 46.855 s`, `-0.087 s` | none observed |
| opt3 | Root-load signature cache for root-driver compensation | one root-load route estimate per topology entry | one root-load route estimate per root-prefix signature | synthesis `46.855 -> 31.766 s`, `-15.089 s` | none observed |

Key interpretation:

- opt1 removed the actual `O(N^2)` bottleneck.
- opt2 is structurally valuable but low incremental benefit after opt1 because final selection is already cheap.
- opt3 removed most repeated physical root-load estimation, but did not reduce generated frontier size.

## Remaining Bottlenecks and Optimization Space

### 1. Fallback Candidate Pipeline Is Still Partly Eager

Current `HTree::build` computes both:

- `covered_global_feasible_pool`
- `covered_global_candidate_pool`

before it attempts strict feasible selection. But in the opt3 run:

- `used_boundary_fallback = false`
- strict feasible selection succeeded
- fallback candidate selection was unnecessary

`SelectBestGlobalEntry` for the candidate pool is lazy after opt2, but `FilterGlobalEntriesBySinkLoadRegionCoverage` for the global candidate pool is still eager.

Earlier instrumentation measured global coverage scan time:

| Pool | Input | Kept | Rejected | Time |
| --- | ---: | ---: | ---: | ---: |
| global feasible | 536458 | 362592 | 173866 | 2.354 s |
| global candidate | 1004862 | 679256 | 325606 | 4.412 s |

The absolute time includes instrumentation overhead, but the scan volume is real. This is the lowest-risk remaining algorithmic cleanup.

### 2. Candidate-Side Per-Depth Sink-Region Filtering Is Also Eager

Inside `EvaluateCandidateBuild`, when boundary constraints exist, the code computes:

- candidate sink-load-region legal entries over the full topology frontier
- feasible raw frontier by top input slew
- feasible sink-load-region legal entries

If strict global feasible selection succeeds, the full candidate fallback frontier is not needed for final selection. Earlier per-depth measurements showed candidate sink-region filtering was consistently larger than feasible filtering:

| Depth | Candidate sink-region filter | Feasible sink-region filter |
| ---: | ---: | ---: |
| 8 | 1.705 s | 0.900 s |
| 7 | 0.995 s | 0.518 s |
| 6 | 0.739 s | 0.396 s |
| 5 | 1.081 s | 0.570 s |
| total | 4.520 s | 2.384 s |

This suggests a strict-first/fallback-second flow can skip more work on normal successful runs.

### 3. Sink-Load Coverage Is Applied After Compensation

Leaf-cap coverage rejects entries based on:

```text
entry.leaf_load_cap_idx >= required_leaf_load_cap_covering_idx
```

This condition depends on topology pattern sink-load-region signature and the entry's leaf-load cap index. The coverage predicate itself does not depend on root-driver compensation delay/power.

Currently, root-driver compensation is applied inside `BuildPatternSearch` before sink-load-region filtering and before global leaf-cap coverage. That means many entries receive compensation even though they will later fail exact sink-load coverage.

Earlier global coverage removed:

- `173866 / 536458 = 32.4%` of strict feasible refs
- `325606 / 1004862 = 32.4%` of fallback candidate refs

After opt3 the route-estimate part of compensation is cached, but compensation still performs per-entry work. Moving coverage before compensation may reduce per-entry compensation, memory, and later scans.

Important caveat: this is not yet proven to be exact in the current implementation. `BuildPatternSearch` applies root-driver compensation and then rebuilds the H-tree state frontier, and the state-frontier dominance uses compensated delay/power. Filtering before compensation would change the order of coverage filtering versus compensation-aware dominance pruning. It should therefore be treated as a proof/experiment candidate, not as already-established no-solution-loss behavior.

### 4. Topology Composition Still Generates Large Frontiers Before Some Exact Legality Facts Are Known

The topology search still builds roughly the same `~1.0M` post-compensation frontier entries across depths, and earlier raw topology pattern counts were much larger:

| Depth | Topology patterns generated before final frontier | Final frontier |
| ---: | ---: | ---: |
| 8 | 21094640 | 377232 |
| 7 | 12530486 | 220756 |
| 6 | 8660110 | 169883 |
| 5 | 11699271 | 243981 |

Many exact facts are known only after the full pattern exists:

- bottom-most buffered level
- sink-load-region signature
- required leaf-load cap index
- global strict feasibility and fallback need

Pushing exact legality state into the frontier composition can reduce the number of states carried to root-level joins.

### 5. Depth Candidates Are Independent Even When They Share Structure

`SearchTopologyDepthCandidates` evaluates each depth candidate independently. Adjacent depth candidates share:

- the same segment characterization library
- many required length bins
- similar or overlapping level-plan suffixes/prefixes
- the same sink-load-region legality context
- the same root-driver compensation pass

The code shares some caches, but it does not memoize composed topology frontiers by level-plan suffix/prefix. This leaves exact reuse opportunities across depth 8/7/6/5.

### 6. Characterization Is Now a Major Fixed Cost

Characterization was not the original bottleneck, but after opt1/opt3 it is roughly `8.17 s` of a `17.715 s` HTree build. Its current complexity is bounded by the lattice:

- 3 direct characterization bins
- 15 slew bins
- 15 cap bins
- 87 patterns
- 19515 executed STA samples

This is not a search-space explosion like the old global selector, but it is now large enough to justify targeted demand-driven experiments after the remaining exact HTree search organization wins are exhausted.

## Priority Recommendations

### P1 - Make the Fallback Candidate Pipeline Lazy End-to-End

Classification: exact/no-solution-loss.

Current issue:

- Candidate fallback global coverage is computed even when strict feasible selection succeeds.
- Candidate per-depth sink-region filtering is also computed eagerly.
- opt2 made candidate Pareto compression lazy, but not candidate coverage/filter construction.

Recommended change:

1. Run a strict-feasible path first:
   - build or retain raw topology frontier
   - apply boundary feasible filtering
   - apply sink-load-region legality and leaf-cap coverage
   - perform per-depth Pareto compression and global selection
2. Only if strict global selection fails, build the fallback candidate path:
   - candidate sink-region filtering
   - candidate global coverage
   - candidate per-depth Pareto compression
   - fallback selection

Expected benefit:

- Skips the global candidate coverage scan on normal strict-feasible runs.
- Potentially skips candidate per-depth sink-region scans on normal strict-feasible runs.
- Based on the earlier instrumented run, the skipped candidate-side scan budget was up to `~8.9 s` before accounting for instrumentation overhead.
- In opt3 production terms, the realistic gain is likely lower, but still high priority because current non-characterization HTree residual is only about `9.5 s`.

QoR risk:

- Low. The fallback path is only needed when strict feasible selection fails.
- If strict feasible selection succeeds, fallback candidate data cannot affect the selected result.

Validation experiment:

- Add temporary counters:
  - `strict_selection_succeeded`
  - `fallback_candidate_pipeline_executed`
  - candidate sink filter entry count
  - candidate global coverage entry count
  - final selected pattern/depth/delay/power
- Run `ics55_dev` at `0.5/0.5`.
- Expected result:
  - `fallback_candidate_pipeline_executed = false`
  - selected topology pattern id remains `10297477`
  - selected metrics, buffer count, wirelength, WNS/HWNS unchanged
  - runtime decreases relative to opt3
- Also run one forced fallback scenario, for example artificially strict top input slew or boundary constraints, to prove fallback still executes and matches old behavior.

### P2 - Prototype Strict-Feasible Sink-Load Coverage Before Root Compensation

Classification: promising exact candidate, but proof required before production. The sink-load coverage predicate is exact, but moving it before compensation changes its order relative to compensation-aware state-frontier pruning.

Current issue:

- Root-driver compensation is applied to all full topology frontier entries.
- Later filters remove a large fraction by boundary feasibility and sink-load coverage.
- Leaf-cap coverage itself does not depend on root compensation.
- The current post-compensation state frontier does depend on compensated delay/power.

Recommended change:

- Split topology assembly into raw electrical topology frontier and compensation-applied selected frontier.
- For the strict path, apply:
  - top input slew feasibility
  - sink-load-region legality signature
  - leaf-load cap coverage
- Then run root-driver compensation only on entries that survived the strict-feasible legality gates.
- If strict path fails, run the fallback candidate path and compensate fallback candidates then.

Expected benefit:

- In the measured search space, covered strict feasible refs were `362592` out of `536458`, and strict feasible refs were already much smaller than the full per-depth final frontier total `~1.0M`.
- If the strict path succeeds, compensation can be reduced from "all generated final entries" toward "strict legal covered entries".
- After opt3 this no longer saves one FLUTE estimate per entry, but it still removes per-entry signature build, cell-master resolution, cache lookup, and compensated-entry mutation.
- It also reduces later per-depth/global selection input and memory pressure.

QoR risk:

- Medium until equivalence is proven.
- The strict coverage predicate is independent of root compensation, but the current algorithm prunes same-state entries after compensation. If an entry that fails sink-load coverage currently helps remove another same-state entry after compensation, or if compensation changes local delay/power dominance ordering, an early-filtered path could retain a different frontier and potentially alter global median selection.
- Implementation risk is also medium because current `CandidateBuildEvaluation` stores compensated frontier entries and local best-char data. The refactor must keep reporting semantics clear.

Validation experiment:

- Instrument counts before and after each gate:
  - raw final frontier
  - boundary-feasible raw frontier
  - sink-region legal raw frontier
  - leaf-cap covered raw frontier
  - compensated entries
- Compare old opt3 and new strict-first flow on:
  - selected depth and pattern id
  - selected raw and compensated delay/power
  - root physical load and root-driver cell
  - inserted buffers/nets
  - final CTS metrics
- Add a debug equivalence check that runs both paths on the same candidate frontiers and compares the final covered feasible/candidate set, or at least the final per-depth/global Pareto sets, before accepting the refactor as exact.
- Add a forced fallback regression to verify fallback compensation is still exact.

### P3 - Push Sink-Load Coverage State Into Frontier Composition

Classification: exact/no-solution-loss if the state is complete.

Current issue:

- Sink-load-region signature is resolved only after a full topology pattern has been materialized.
- From a leaf-to-root composition perspective, the bottom-most buffered level becomes known as soon as the first buffered segment is encountered from the leaf side.
- Once that happens, required leaf-load cap coverage for that sink-load-region boundary is fixed and can be checked earlier.

Recommended change:

- Extend topology composition metadata with a compact sink-region state:
  - whether bottom-most buffered boundary is already fixed
  - bottom-most buffered relative level
  - boundary segment pattern id
  - required leaf-load cap covering index, once resolvable
- When the state is fixed, drop entries whose `leaf_load_cap_idx` cannot cover the required cap before composing more root-side levels.

Expected benefit:

- Reduces `J_l` in upper-level hash joins, not just final filtering.
- More valuable than post-filtering on larger designs or relaxed slew/cap grids because it prevents invalid subtrees from being carried through more joins.
- The measured global coverage rejection ratio was about `32%`; if those invalid states can be eliminated earlier, the downstream join savings can be larger than `32%` because joins amplify state counts.

QoR risk:

- Low in theory, medium in implementation.
- The key proof obligation is that the pushed state exactly matches `ResolveSinkLoadRegionLegalitySignature` and coverage semantics, and that the new ordering is compared against the current compensation-aware frontier pruning behavior.
- Mistakes in relative level indexing or segment-pattern identity would silently remove legal candidates.
- If currently-illegal entries have been participating in same-state delay/power pruning before later coverage removal, pushing coverage earlier can change the retained legal frontier. That may be desirable, but it must be measured rather than assumed no-op.

Validation experiment:

- Build a debug mode that records old post-hoc legality signature and new incremental signature for every final candidate, then assert equality.
- Compare old and new final covered feasible/candidate sets, and the final per-depth/global Pareto sets, on small deterministic topology fixtures.
- Add randomized composition tests where all full patterns are enumerated and incremental pruning is compared against post-hoc filtering.
- Run `ics55_dev` and compare selected topology/QoR.

### P4 - Reuse Topology Frontiers Across Adjacent Depth Candidates

Classification: exact/no-solution-loss if memoized frontiers preserve pattern-library semantics.

Current issue:

- Depth 8, 7, 6, and 5 are evaluated as independent searches.
- The same required length bins and similar level-plan sequences are recomposed multiple times.
- Existing caches help root compensation and sink legality, but not topology frontier composition itself.

Recommended change:

- Memoize composed topology frontiers by a canonical key:
  - ordered level aligned-length sequence
  - segment entry selection mode
  - boundary constraint mode
  - relevant composition state contract version
- Reuse suffix or prefix frontiers across depth candidates where the level-plan sequence matches.

Expected benefit:

- Moderate on the current design; potentially higher when the depth explore window grows.
- Earlier instrumentation showed depth 8/7/6/5 pattern search total before opt3 was `22.635 s`, with `6.835 s` in compose and `15.602 s` in root compensation. opt3 removed most root compensation, so shared compose reuse becomes more relevant.

QoR risk:

- Low algorithmically, medium-to-high engineering risk.
- `TopologyPatternLibrary` pattern ids are local to a candidate build. Reusing frontiers requires either shared library ownership or remapping pattern ids safely.

Validation experiment:

- First add instrumentation only:
  - canonical level sequence for each depth
  - number of shared suffixes/prefixes
  - estimated frontier sizes available for reuse
- Then prototype reuse for a narrow case with identical suffix sequences.
- Compare complete per-depth final frontier counts and selected output against opt3.

### P5 - Add Monotone Lattice Dominance Across Compatible Frontier States

Classification: exact only if dominance proof is complete; otherwise approximate.

Current issue:

- Current state-frontier pruning only compares delay/power inside an exact state key.
- Exact state keys include several lattice dimensions:
  - output slew
  - load cap
  - leaf load cap
  - driven cap
  - monotonic boundary state
- Some dimensions may have monotone meaning. For example, lower output slew and higher load-cap coverage can be better for future composition, provided downstream compatibility is represented as a coverage range rather than an exact equality.

Recommended research:

- Prove a safe partial order for H-tree frontier states:
  - delay lower is better
  - power lower is better
  - output slew lower may dominate only if all future joins accepting the worse slew also accept the better slew or if lattice snapping preserves equivalence
  - leaf-load cap higher may dominate for sink-load coverage, but may interact with driven cap and power
  - load-cap/root-cap state must preserve composition correctness
- If proof holds, replace exact-state-only Pareto with state-range dominance.

Expected benefit:

- Potentially high because it reduces frontier width before joins.
- Especially useful under relaxed max slew where many more slew/cap lattice states survive.

QoR risk:

- Medium to high until the dominance relation is formally proven and tested.
- Incorrect monotone pruning would remove legal candidates and can change QoR.

Validation experiment:

- Implement an offline analyzer first:
  - inspect final frontier states
  - count how many entries would be removed by candidate monotone rules
  - do not change synthesis output
- For each proposed dominance rule, run exhaustive small-lattice randomized tests comparing final Pareto selections with and without the rule.

### P6 - Group Remaining Root Compensation Work by Full Compensation Signature

Classification: exact/no-solution-loss.

Current issue:

- opt3 caches physical root-load estimate by root-prefix signature.
- Per entry, the code still resolves:
  - root closure load signature
  - root driver cell master
  - root compensation key
  - compensation cache lookup
  - entry mutation

Recommended change:

- Precompute per-pattern metadata once:
  - root-load signature
  - root-driver cell master
  - first real buffer level
- Group entries by `(root_load_signature, root_driver_cell_master, input_slew, clock_period, load bucket/load cap)`.
- Evaluate compensation detail once per group and apply it to the group.

Expected benefit:

- Low-to-moderate after opt3, but exact and useful if frontier sizes continue growing.
- This is less important than lazy fallback and early legality pruning because it does not reduce generated search space.

QoR risk:

- Low if the grouping key exactly matches `RootDriverCompensationCacheKey` plus the load-estimate identity.

Validation experiment:

- Add counters:
  - entries compensated
  - distinct root-load signatures
  - distinct cell masters
  - distinct full compensation groups
  - cache hit ratios
- Compare selected metrics and root-driver report fields bitwise against opt3.

### P7 - Demand-Driven Characterization

Classification: exact if all eventually needed lattice points are characterized before use; approximate if lattice points are skipped by heuristic.

Current issue:

- Characterization is now about `8.17 s` of the `17.715 s` HTree build.
- It still characterizes the configured lattice eagerly before topology search.

Recommended research:

- Identify which wirelength bins, slew bins, cap bins, and buffer patterns are actually reachable through required segment lengths and boundary constraints.
- Characterize lazily on first use by segment frontier synthesis and topology composition.
- Cache characterization results within the run.

Expected benefit:

- Moderate. It targets a fixed cost that is now visible after the major search bottlenecks were removed.
- Benefit depends on how many lattice points are never used by the required length closure and legal frontiers.

QoR risk:

- Low if demand-driven characterization is exact.
- Medium implementation risk because characterization data currently behaves like a complete library.

Validation experiment:

- Add no-behavior-change reachability counters first:
  - total characterized points
  - points used by segment frontier
  - points used in selected/final topology frontiers
- Only implement lazy characterization if unused-point ratio is material.

### P8 - Approximate Depth Screening or Epsilon Frontier Caps

Classification: approximate/solution-space-reducing unless a formal lower-bound proof is added.

Current issue:

- All depth candidates are built before final global selection.
- Depth 8 was the most expensive depth in the earlier instrumented run, but selected depth was 5.

Possible approach:

- Use a cheap first-pass lower-bound estimate per depth to decide which depths deserve exact build.
- Or cap frontier width by delay/power bins, epsilon dominance, or top-K per state.

Expected benefit:

- Potentially high if deeper expensive candidates are often not selected.

QoR risk:

- Medium to high.
- The final policy is global Pareto plus power-median ordering. A depth that does not contain the minimum delay or minimum power can still affect the median of the global Pareto set.
- This should not be a default production optimization without explicit QoR guardrails and config gating.

Validation experiment:

- Run as an experimental mode only.
- Compare against exact opt3 on multiple designs/configs:
  - selected topology
  - skew/transition/cap
  - WNS/HWNS
  - buffer count and wirelength
  - runtime
- Track whether omitted depths would have contributed to the exact global Pareto set.

## Recommended Next Experiment Plan

The next experiment should focus on exact lazy fallback and proof-driven pre-compensation strict gating.

### Experiment A - Measure Current opt3 Residual Substages

Add temporary probes only, then remove or keep them task-local for review:

- HTree build phase:
  - characterization
  - segment frontier synthesis
  - per-depth pattern search compose
  - root compensation apply
  - per-depth candidate sink filter
  - per-depth feasible sink filter
  - global feasible coverage
  - global candidate coverage
  - per-depth Pareto compression
  - final selection
- Counters:
  - raw final frontier entries per depth
  - boundary-feasible entries per depth
  - sink-legal entries per depth
  - leaf-cap covered entries per depth
  - compensated entries
  - root-load signatures
  - full compensation groups
  - fallback candidate pipeline executed or skipped

Purpose:

- Confirm how much of opt3's `~9.5 s` post-characterization HTree time is candidate-side fallback scanning and coverage.

### Experiment B - Lazy Fallback Candidate Pipeline

Prototype behavior:

- Do not compute fallback candidate global coverage until strict feasible selection fails.
- If practical, also defer candidate per-depth sink filtering until strict feasible global selection fails.

Success criteria:

- `used_boundary_fallback=false` run skips fallback candidate path.
- Selected topology and final CTS metrics remain identical.
- Runtime improves over opt3.

### Experiment C - Strict Coverage Before Compensation

Prototype behavior:

- On strict path, apply exact boundary feasibility, sink legality, and leaf-cap coverage before root-driver compensation.
- Compensate only strict covered entries.
- Fall back to candidate compensation only if strict global selection fails.

Success criteria:

- Compensated entry count drops materially.
- Debug equivalence shows the reordered path preserves the final covered feasible/candidate sets, or at minimum preserves every member that can affect the final per-depth/global Pareto selection.
- Selected topology and final metrics remain identical.
- Runtime improves over Experiment B.

## What Not To Prioritize Now

- Replacing containers or changing reserve sizes without reducing scanned/generated frontier entries.
- Source-to-root segment selector optimization; it was measured at about `2 ms`.
- Topology generation optimization; H-tree topology build was measured at about `2 ms`.
- Further global final selector work; opt1/opt2 already removed the `O(N^2)` bottleneck and final selection input is structurally bounded.
- Root-load FLUTE route optimization by itself; opt3 already removed the per-entry FLUTE loop.

## Bottom Line

The current opt3 runtime is in a much healthier complexity regime: synthesis is `31.766 s` instead of `91.532 s`, and the old `O(N^2)` selector plus per-entry FLUTE root-load estimate are no longer dominant.

There is still meaningful optimization space, but it is now mostly about avoiding unnecessary candidate/fallback work and moving legality gates earlier in the topology search. The safest next production direction is:

1. strict-first lazy fallback pipeline,
2. strict sink-load coverage before root compensation after equivalence proof,
3. then, if still needed, push sink-load coverage state into the frontier composition DP.

The first item preserves the exact final-selection behavior for strict-feasible runs. The second and third target the same large scans/frontiers, but should be accepted as exact only after the proposed equivalence checks prove that moving legality gates earlier does not alter compensation-aware frontier dominance or final global Pareto selection.
