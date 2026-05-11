# P5/P7/P8 Remaining H-tree Runtime Opportunities

Date: 2026-05-09

> Superseded on 2026-05-09 by `rollback_to_opt3_report.md` for current production-state claims. This file remains a historical report for P5/P7/P8 reasoning. After rollback, production H-tree code is restored to the opt3 backup state; P1/P6 and P2/P3/P4 post-opt3 code are no longer present in production source. P5/P7/P8 remain report-only and are not default-enabled.

## Scope

This report closes the remaining P5/P7/P8 investigation items for the iCTS H-tree runtime task:

- P5: monotone lattice dominance across compatible frontier states
- P7: demand-driven characterization
- P8: approximate depth screening / epsilon frontier caps

Historical note: at the time this report was written, the source tree contained default-enabled exact work for P1/P6 and default-off analyzer/prototype work for P2/P3/P4. That is no longer the current production state after the rollback to the opt3 backup ref.

No C++ changes were made for P5/P7/P8 in this pass. None of P5/P7/P8 is default-enabled.

## Current Baseline for This Decision

The historical latest post-opt3 default-enabled benchmark artifact was:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/opt5_p6_root_compensation_grouping/`

Key current measurements from that run:

| Metric | Value |
| --- | ---: |
| CTS synthesis elapsed | 31.637 s |
| CTS total elapsed | 47.599 s |
| External wall time | 68.94 s |
| HTree build elapsed | 17.475 s |
| CharBuilder build elapsed | 7.406 s |
| Segment chars | 19515 |
| Characterization patterns | 87 |
| Selected depth | 5 |
| Selected final frontier count | 243981 |
| Selected feasible solutions | 130294 |
| Selected feasible frontier entries | 126566 |
| Fallback candidate frontier | not materialized |
| Selected topology pattern id | 10297477 |
| Selected level segment pattern ids | `522717,468375,28,24,6` |
| Selected compensated metric | 0.4959 ns / 217.271 uW |
| Final clock buffer count | 360 |
| Total clock network wirelength | 43151.203 um |
| Setup WNS / Hold WNS | 7.302 ns / 0.008 ns |

Important context from earlier reports:

- Historical state only: P1 lazy fallback and P6 root-compensation grouping were default-enabled exact optimizations before rollback. They are not present in current production source after the opt3 rollback.
- P2 strict pre-compensation sink-load gate remains default-off because compensation-aware frontier dominance can change when filtering order changes.
- P3 sink-load coverage state remains analyzer-only because compose-time pruning needs proof over absolute topology level offsets and compensation-aware dominance.
- P4 depth reuse remains analyzer-only because current depth candidates share root-side prefixes, while the implementation builds leaf-to-root and topology pattern ids are candidate-local.

## Decision Summary

| Item | Implemented in this pass | Default enabled | Decision | Primary reason |
| --- | --- | --- | --- | --- |
| P5 monotone lattice dominance | Report only | No | Do not implement pruning yet | Exact dominance across different H-tree state keys is not proven; join compatibility uses exact lattice keys and current median selection can be affected by removed points. |
| P7 demand-driven characterization | Report only | No | Do not refactor characterization path yet | It can be exact in principle, but the current characterization library is eagerly materialized and broadly consumed; a reachability analyzer should come before any lazy builder. |
| P8 approximate depth screening / epsilon caps | Report only | No | Do not default-enable | This is solution-space-reducing unless backed by a formal lower bound; it can omit depths or Pareto points that affect global power-median selection. |

No additional analyzer was added because the useful low-risk analyzer designs for P5/P7/P8 still require new per-entry reachability or counter plumbing in hot H-tree paths. The current request prioritized a consolidated report, and adding incomplete counters would create maintenance surface without enough proof value.

## P5: Monotone Lattice Dominance Across Compatible Frontier States

### Current Implementation State

P5 is not implemented.

The current production frontier pruning remains exact-state pruning:

- `BuildSegmentStateFrontier` groups by exact `SegmentFrontierStateKey`.
- `BuildHTreeStateFrontier` groups by exact `HTreeFrontierStateKey`.
- Inside each exact state group, pruning is delay/power Pareto with deterministic pattern-id tie behavior.

The H-tree state key currently includes:

- `input_slew_idx`
- `driven_cap_idx`
- `leaf_load_cap_idx`
- `output_slew_idx`
- `load_cap_idx`
- `source_boundary_net_switch_power_w`
- `terminal_semantic`
- `monotonic_boundary_state`

The topology hash join also uses exact compatibility:

- downstream key: `(input_slew_idx, driven_cap_idx)`
- upstream probe: `(output_slew_idx, ceil(load_cap_idx / 2))`

### Why It Is Not Default-Enabled

P5 is only exact if a dominance relation across different state keys can prove that every future continuation accepted by the dominated entry is also accepted by the dominator and cannot produce a better final Pareto/median outcome.

That proof is not available yet. Several state dimensions look monotone locally but are not safely interchangeable under the current composition contract:

- `output_slew_idx`: lower slew is usually better electrically, but the join uses exact `downstream.input_slew_idx == upstream.output_slew_idx`. A lower-slew entry does not automatically join with downstream entries keyed to a higher exact input slew unless the downstream lookup is changed to range compatibility and the characterized downstream behavior is proven conservative.
- `load_cap_idx`: higher coverage may be better for fanout, but the join maps it through `ceil(load_cap_idx / 2)` and then exact-matches downstream driven cap. A larger load cap index can land in a different half-cap bucket and may join a different downstream subset.
- `leaf_load_cap_idx`: higher leaf-load coverage helps sink-load coverage, but it is part of the exact frontier state and may carry different delay/power. Removing a lower-cap point can change per-state Pareto shape and later global power-median ordering.
- `source_boundary_net_switch_power_w`: this is exact-keyed today. Treating it as monotone would need a power accounting proof, not just a delay/slew proof.
- `terminal_semantic` and `monotonic_boundary_state`: these encode buffer boundary behavior. They cannot be relaxed without proving all downstream materialization, legality, and root-compensation semantics remain equivalent.

There is also a selection-policy risk: iCTS does not simply choose minimum delay or minimum power. It builds a global delay/power Pareto set and selects the lower power-ordered median entry. Removing any Pareto-affecting point can shift that median even if the selected scalar optimum does not worsen.

### Potential Benefit

Potential benefit is high if an exact partial order exists:

- It would shrink frontier width before upper-level hash joins.
- It targets the remaining large topology search space rather than only final selection.
- It should be most useful under relaxed max slew/cap settings, where many lattice states survive.

The current data makes this worth researching:

- Selected-depth final frontier is still `243981`.
- P3 analyzer saw `325831 / 1008237` legal raw pre-comp entries fail leaf-load cap coverage across depths, showing many frontier states are near legality/dominance boundaries.
- Earlier instrumentation showed more than one million per-depth post-comp frontier entries across depth candidates.

### Required Proof or Experiment

Before any P5 pruning can be enabled, require all of the following:

1. Formal compatibility proof for each relaxed lattice dimension.
   The proof must state whether future composition is exact-key, range-key, or conservative-covering, and must cover `HTreeTraits::probeKey/buildKey`.

2. Exhaustive small-lattice equivalence tests.
   Enumerate small upstream/downstream H-tree and segment tables, run current exact-state pruning and proposed monotone pruning, and compare final per-depth and global Pareto-affecting sets.

3. Offline would-prune analyzer.
   Count how many entries each proposed rule would remove from existing final/intermediate frontiers, but do not change synthesis output.

4. Median-shift validation.
   Compare not only selected delay/power, but also global Pareto membership and power-median index. A rule that preserves min delay/min power can still change the selected median.

5. Multi-design/config run matrix.
   Include `0.5/0.5`, `0.05/0.05`, forced-fallback/no-strict-feasible cases, and at least one different sink distribution.

### Recommended Next Step

Add an offline P5 analyzer only after defining one concrete candidate dominance rule. The first analyzer should be read-only and should report:

- total entries scanned per level/depth;
- number of exact-state groups;
- candidate cross-state dominance comparisons attempted;
- would-prune count by rule;
- number of would-pruned entries that are in the current final covered Pareto set;
- whether the current selected pattern would be removed.

Do not add `ICTS_HTREE_ENABLE_*` pruning for P5 until the analyzer shows material opportunity and the proof/tests above are in place.

## P7: Demand-Driven Characterization

### Current Implementation State

P7 is not implemented.

The current characterization path remains eager:

1. HTree builds/adapts the characterization grid.
2. `CharBuilder::build()` enumerates configured wirelength bins, feasible topology patterns, input slew bins, and load cap bins.
3. `SynthesizeSegmentEntrySets()` consumes the completed `char_builder.get_segment_chars()` library.
4. Topology composition consumes synthesized segment frontier sets by aligned length.

The historical opt5/P6 benchmark generated:

| Wirelength | Generated chars | Generated patterns | Executed STA samples |
| ---: | ---: | ---: | ---: |
| 31.3571 um | 1110 | 5 | 1110 |
| 62.7142 um | 4260 | 19 | 4260 |
| 94.0713 um | 14145 | 63 | 14145 |
| Total | 19515 | 87 | 19515 |

CharBuilder build time in that historical run was `7.406 s`, roughly `42%` of the measured `17.475 s` HTree build in opt5/P6.

### Why It Is Not Default-Enabled

Demand-driven characterization can be exact in principle, but a production lazy refactor is not low risk in the current architecture.

Main blockers:

- The segment synthesis path currently expects a complete set of segment chars and then derives missing required lengths through DP/hash joins.
- Characterization samples are keyed by wirelength, pattern, input slew, and load cap. A lazy path must preserve the same lattice snapping and overflow semantics as the eager path.
- Some points that are not selected can still affect state-frontier pruning, later composed frontiers, and final Pareto median selection.
- The characterization builder owns STA/iPA sampling, circuit setup/teardown, pattern metadata storage, sample storage, and schema reporting. Refactoring the default path would cross module boundaries and needs broader tests.

The available data proves characterization is significant, but it does not yet prove that many characterized points are unreachable. The current three direct bins are all used to synthesize required H-tree level lengths. Skipping any bin or lattice point heuristically would be approximate unless reachability is proven before use.

### Potential Benefit

Potential benefit is moderate:

- It targets `7.406 s` of current HTree build time.
- Benefit would grow with more wirelength bins, more buffers, larger slew/cap lattices, or tighter design-specific reachable subsets.
- A demand cache could also help future experiments that evaluate multiple nearby H-tree configurations in one run.

Expected benefit for the current `ics55_dev 0.5/0.5` case is uncertain:

- Only 3 direct characterization bins are generated.
- All 3 bins appear in the adapted H-tree grid plan.
- The reported generated chars equal executed STA samples, so there is not an obvious large unused skip already visible from top-level counters.
- A reachability analyzer is needed before estimating savings.

### Required Proof or Experiment

Before changing the default characterization path, require:

1. Reachability accounting.
   Record every characterized tuple and every tuple actually consumed by segment entry synthesis, topology composition, selected frontiers, and selected pattern materialization.

2. Exact on-demand cache contract.
   Define a key that includes length index, pattern topology, buffer masters, input slew index, effective load cap, lattice setup, RC setup, and power-context availability.

3. Eager-vs-lazy equivalence tests.
   Run both paths in one debug mode and compare:
   - segment char set used by each required length;
   - segment frontier sets by length;
   - per-depth final frontier counts;
   - global Pareto set;
   - selected topology, delay/power, root load, buffer count, wirelength, and STA metrics.

4. Schema/report parity.
   The lazy path must still report skipped/overflow/executed sample counts in a way that remains understandable. Otherwise runtime reports become misleading.

5. Fallback coverage.
   Include cases where lazy search first misses a tuple and must characterize it during segment synthesis or topology composition.

### Recommended Next Step

Add a default-off P7 reachability analyzer before any lazy builder:

```bash
ICTS_HTREE_DEBUG_CHAR_REACHABILITY=1
```

Recommended analyzer counters:

- total characterized points by `(length_idx, pattern_id, input_slew_idx, load_cap_idx)`;
- points consumed by `SynthesizeSegmentEntrySets`;
- points surviving segment state-frontier pruning by length;
- points whose segment patterns participate in final topology frontiers;
- points present in the selected topology's segment pattern chain;
- unused characterized point ratio by wirelength/pattern/slew/cap dimension.

Only consider an exact lazy characterization implementation if the analyzer shows a material unused ratio and the eager-vs-lazy equivalence checks are available.

## P8: Approximate Depth Screening / Epsilon Frontier Caps

### Current Implementation State

P8 is not implemented.

The current production behavior evaluates the configured depth-candidate window exactly. For this task's benchmark:

- explored depths: `8, 7, 6, 5`;
- selected depth: `5`;
- final policy: global delay/power Pareto followed by lower power-ordered median selection;
- fallback selection remains available if strict feasible selection fails.

P4 already added a default-off depth reuse analyzer, but it does not screen or omit depth candidates. It showed:

| Pair | Smaller is root-side prefix | Exact reusable suffix estimate |
| --- | :---: | ---: |
| 8 -> 7 | yes | 0 |
| 7 -> 6 | yes | 0 |
| 6 -> 5 | yes | 0 |

### Why It Is Not Default-Enabled

P8 is approximate unless a formal lower-bound proof is added.

Depth screening risk:

- The selected depth was 5 in the measured run, but deeper depths may still contribute points to the global Pareto set.
- The global selector uses Pareto power-median ordering. Omitting a depth that does not contain the eventual selected point can still shift the median by changing the Pareto population.
- A cheap lower bound on best delay or best power is not enough unless it proves the whole omitted depth cannot contribute any Pareto-affecting point.

Epsilon cap risk:

- Top-K, epsilon dominance, and bin caps intentionally remove legal points.
- Removing interior Pareto points can shift the median selected point even if extremes are preserved.
- Caps can mask fallback behavior by removing the only candidate that satisfies a boundary or sink-load condition after later filters.

### Potential Benefit

Potential benefit can be high in large search spaces:

- Skipping an expensive depth can avoid a full topology build, root compensation, filtering, and selection for that depth.
- Frontier caps can directly reduce hash-join amplification and memory pressure.
- Earlier instrumentation showed depth 8 was expensive while depth 5 was selected, so screening is attractive as an experiment.

However, current exact optimizations already improved the benchmark substantially:

| Run | CTS synthesis | External wall |
| --- | ---: | ---: |
| opt0 clean baseline | 91.532 s | 128.67 s |
| opt5/P6 historical default-enabled state before rollback | 31.637 s | 68.94 s |

Because the remaining search is now in a healthier regime, solution-space-reducing default caps are not justified without explicit QoR policy.

### Required Proof or Experiment

For exact depth screening, require:

1. Lower-bound proof per depth.
   The bound must show an omitted depth cannot contribute any entry to the global covered delay/power Pareto set, not just that it cannot beat the current best scalar objective.

2. Median-invariance proof.
   If a depth can contribute Pareto points, prove their omission cannot shift the selected lower power-ordered median.

3. Omission-risk analyzer.
   Run exact builds, then simulate proposed depth screening offline and report:
   - omitted depths;
   - number of exact global Pareto entries from each omitted depth;
   - whether selected median index changes;
   - selected topology/QoR drift if the omitted depths are removed.

For approximate caps, require:

1. Explicit opt-in config or environment gate.
2. User-visible report fields recording cap mode, cap value, omitted entry counts, and approximate status.
3. QoR guardrails across a design/config matrix.
4. Rollback-to-exact mode for comparison.

### Recommended Next Step

Keep P8 as offline analysis only.

A useful default-off analyzer would be:

```bash
ICTS_HTREE_DEBUG_DEPTH_SCREENING_RISK=1
```

It should still run the exact search, then replay candidate screening decisions offline and report:

- depth lower-bound estimates;
- depth build cost;
- exact global Pareto contribution by depth;
- whether each screened depth would have affected the final median;
- selected-pattern drift under each proposed cap/screening rule.

Do not add a production `ICTS_HTREE_ENABLE_DEPTH_SCREENING` or frontier cap until the exact-risk analyzer can prove no Pareto/median drift for the intended rule, or until the project accepts an explicit approximate QoR tradeoff.

## Cross-Item Risk Notes

P5 and P8 both interact with global median selection. Any pruning that removes a Pareto-affecting point can change the selected entry even when min delay, min power, and feasibility remain acceptable.

P5 and P7 both interact with characterization lattice semantics. If P5 changes compatibility from exact keys to coverage/range keys, P7 must be careful not to skip characterization points that become reachable only under the new compatibility relation.

P7 and P8 both need exact/offline replay before default enablement. A demand-driven char path that only samples likely points plus a depth cap would compound approximation risk and make failures hard to attribute.

## Final Recommendation

Do not default-enable P5, P7, or P8 in the current task.

Recommended priority after the historical post-opt3 P1/P6 defaults and P2/P3/P4 analyzers:

1. P7 reachability analyzer.
   It is the safest next report-only counter because characterization is now a measured `7.406 s` HTree component and an exact lazy path is possible if unused ratio is high.

2. P5 offline dominance analyzer for one narrowly defined rule.
   Start with read-only would-prune counts and Pareto-impact checks. Do not implement pruning until a compatibility proof exists.

3. P8 omission-risk analyzer.
   Keep it offline and exact-run-based. Treat any cap/screening as approximate unless it proves no global Pareto/median drift.

At the time of this historical report, the recommended production state was:

- P1: historically enabled before rollback
- P2: disabled analyzer/prototype only
- P3: disabled analyzer only
- P4: disabled analyzer only
- P5: not enabled, report-only
- P6: historically enabled before rollback
- P7: not enabled, report-only
- P8: not enabled, report-only

After the rollback documented in `rollback_to_opt3_report.md`, this recommendation is superseded. The current validated production state is opt1/opt2/opt3 only: P1/P6 are removed, P2/P3/P4 are removed from production source, and P5/P7/P8 remain report-only.
