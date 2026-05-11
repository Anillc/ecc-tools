# iCTS H-tree Transformational Optimization Strategy

Date: 2026-05-09

## Scope

This report records the recommended next directions after rolling production H-tree source back to the opt3 state. It is report-only work. No source code or `.trellis/spec/` file is changed by this strategy.

Current validation baseline:

- artifact: `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/rollback_opt3_validation/`
- command: `cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- exit code: `0`
- CTS synthesis runtime: `31.756 s`
- CTS total runtime: `47.649 s`
- selected depth: `5`
- selected topology pattern id: `10297477`
- selected level segment ids: `522717,468375,28,24,6`
- selected delay/power: `0.4959 ns / 217.271 uW`
- final clock buffer count: `360`
- total clock network wirelength: `43151.203 um`
- setup/hold WNS: `7.302292 ns / 0.008315 ns`

## Strategic Position

Opt1, opt2, and opt3 already removed the obvious runtime cliffs:

- opt1 replaced the old quadratic delay/power Pareto selector with an exact sort/scan selector.
- opt2 bounded final global selection input through exact per-depth Pareto compression.
- opt3 removed repeated per-entry physical root-load route estimation by caching root-load signatures.

The remaining work should not be another small default-on cleanup unless new measurements show a clear residual hotspot. The next useful changes are transformational because they change when work is generated, represented, or proven unnecessary. They must either preserve the exact solution space or have an explicit proof that the global Pareto/power-median selected result cannot change.

Recommended order:

1. exact reachability analyzer for demand-driven characterization;
2. exact frontier-state legality pruning with an equivalence harness;
3. pattern-id-independent lazy/backpointer topology representation;
4. optional depth screening only with Pareto/median proof;
5. keep every proposal framed by opt3 Amdahl limits.

## Amdahl Frame

The rollback validation and opt3-era logs put the current runtime budget in this range:

| Component | Time | Share of CTS total | Share of synthesis | Note |
| --- | ---: | ---: | ---: | --- |
| CTS total | `47.649 s` | `100.0%` | n/a | `cts.log` total runtime |
| CTS synthesis | `31.756 s` | `66.6%` | `100.0%` | primary remaining stage |
| HTree build | about `17.7 s` | about `37.2%` | about `55.7%` | validation log reports `17.737 s`; historical opt3 report was `17.715 s` |
| Characterization | about `8.0 s` | about `16.8%` | about `25.2%` | validation log reports `8.015 s` CharBuilder build |
| Non-HTree synthesis | about `14.0 s` | about `29.4%` | about `44.1%` | synthesis minus HTree build |

This matters for prioritization:

- Perfectly removing HTree build would only reduce CTS total from `47.649 s` to about `29.9 s`, a theoretical `1.59x` CTS-total speedup.
- Perfectly removing characterization alone would reduce CTS total to about `39.6 s`, only about `1.20x` on CTS total.
- Cutting HTree build in half would save about `8.9 s`, reducing CTS total to about `38.8 s`, about `1.23x`.
- Once HTree work drops much below `17.7 s`, the roughly `14 s` non-HTree synthesis floor becomes equally important.

Therefore the right target is not "optimize one helper until it is tiny." The right target is to avoid generating unreachable or illegal search states, while preserving enough exactness proof that CTS QoR does not drift silently.

## 1. Exact Reachability Analyzer for Demand-Driven Characterization

Direction: add a default-off analyzer before any lazy characterization refactor.

Why this comes first:

- Characterization is now a visible `~8 s` cost, roughly half of HTree build.
- Demand-driven characterization can be exact if every tuple is generated before any consumer needs it.
- The current evidence does not yet prove many characterized tuples are unused, so implementing lazy characterization first would be speculative architecture work.

The analyzer should record the reachability lifecycle of each characterization tuple:

```text
(wirelength_bin, segment_pattern_signature, input_slew_idx, load_cap_idx, buffer_chain_signature)
```

Minimum counters:

- total characterized tuples by length bin and pattern;
- tuples consumed by segment frontier synthesis;
- tuples surviving segment frontier pruning;
- tuples consumed by topology composition;
- tuples present in final per-depth frontiers;
- tuples present in the selected topology's level segment chain;
- unused ratio by dimension, not just one global unused percentage.

Exactness contract for later lazy characterization:

- The lazy key must include every input that affects STA/liberty sampling, RC setup, overflow behavior, power availability, and lattice snapping.
- A debug dual mode should run eager and lazy characterization in one process and compare segment frontiers, topology frontiers, global Pareto membership, selected topology ids, selected delay/power, root load, root driver, and final CTS metrics.
- A lazy implementation is only justified if the analyzer shows material unused work or repeated reachable work that can be cached safely.

Acceptance gate:

- promote only an analyzer first;
- require a report proving unused tuple volume and exact eager-vs-lazy parity before any default path changes.

## 2. Exact Frontier-State Legality Pruning With Equivalence Harness

Direction: move exact legality knowledge earlier in topology composition, but only behind a harness that proves equivalence to the current opt3 result.

The key opportunity is that the current flow still carries large frontier populations until late filters:

- selected-depth final frontier count: `243981`
- selected-depth candidate frontier entries before feasible filtering: `236991`
- selected-depth feasible solutions: `130294`

Sink-load-region legality and leaf-load-cap coverage are exact predicates, but their current placement interacts with compensation-aware frontier pruning and global Pareto median selection. That means "the predicate is exact" is not enough; moving it earlier must also prove that no Pareto-affecting legal entry is removed or reordered.

Recommended state to push into the frontier:

- whether the bottom-most buffered boundary has been fixed;
- absolute topology level of that boundary, not only a relative level;
- boundary segment structural signature;
- required leaf-load cap covering index;
- terminal branch buffer semantics;
- enough context to reproduce the existing post-hoc sink-load-region legality signature exactly.

Equivalence harness requirements:

- Run old opt3 post-hoc legality and new incremental legality in the same debug execution.
- For every final candidate, compare old and new legality signatures and coverage decisions.
- Compare per-depth covered feasible sets and covered candidate sets.
- Compare per-depth local Pareto sets, global Pareto set, power-median index, selected topology pattern id, selected level segment ids, raw delay/power, compensated delay/power, root load, root driver cell, final buffer count, wirelength, setup WNS, and hold WNS.
- Include a forced fallback/no-strict-feasible case, because a strict-feasible-only proof can hide candidate fallback drift.

Implementation principle:

- First analyzer: no pruning, only equality checks and would-prune counts.
- Second prototype: prune only states that the incremental signature proves illegal for every possible remaining continuation.
- Promotion gate: exact equality on small exhaustive fixtures plus the `ics55_dev` validation matrix.

Expected payoff:

- This attacks hash-join amplification, not just final filtering.
- If illegal or under-capacity states are removed before upper levels, downstream joins shrink multiplicatively.
- It is likely more valuable than another root-compensation micro-optimization because it reduces generated state volume.

## 3. Pattern-Id-Independent Lazy/Backpointer Topology Representation

Direction: separate topology identity from candidate-local pattern ids and materialize full topology patterns only when needed.

Why the current representation blocks deeper wins:

- Pattern ids are candidate-local and assignment-order-sensitive.
- P4-style reuse across depth candidates becomes risky because a reused frontier may refer to ids from a different candidate's pattern library.
- Late materialization and equivalence checking are harder because ids are not stable structural identities.
- Full topology patterns can be built and stored for entries that never reach the selected global Pareto median.

Recommended representation:

- Use immutable topology backpointer nodes for composed patterns.
- Give each node a structural key independent of local id assignment:

```text
(level, segment_structural_signature, downstream_backpointer_key, terminal_semantic, sink_legality_state)
```

- Keep compact frontier entries pointing to backpointer nodes rather than eagerly materialized full pattern ids.
- Assign legacy pattern ids lazily at report/materialization boundaries, with deterministic ordering.
- Preserve an adapter layer so existing report fields can still print selected topology pattern id and selected level segment ids during migration.

Benefits:

- Enables exact cross-depth or cross-candidate reuse by structural key rather than by local id.
- Makes legality and reachability analyzers more reliable because they can compare structural identity directly.
- Reduces memory and materialization work for entries that are filtered before final selection.
- Creates a cleaner foundation for lazy characterization because selected/used segment structures can be traced backward from final states.

Risks:

- This is a representation refactor, not a one-line optimization.
- Deterministic tie behavior must be preserved, because the global selection policy depends on stable Pareto and median ordering.
- Report compatibility matters: the selected pattern id may remain externally useful even if it becomes a lazily assigned compatibility id.

Promotion gate:

- Introduce the backpointer representation under a debug/equivalence path first.
- Materialize both old and new topology patterns for the same selected entry and compare level segment ids, inserted H-tree buffers, physical root load, root driver compensation, and final CTS metrics.
- Only remove eager id-coupled paths after equivalence holds across strict and fallback cases.

## 4. Optional Depth Screening Only With Pareto/Median Proof

Direction: keep depth screening offline or explicitly approximate until a proof covers global Pareto membership and median selection.

Why this is risky:

- The selected depth is `5`, but deeper depths may still contribute entries to the global delay/power Pareto set.
- The policy does not choose a simple scalar minimum. It Pareto-filters delay/power and selects the lower power-ordered median entry.
- Omitting a depth can change the median even if it does not contain the final selected entry in the exact run.

Exact proof requirement:

- For a screened depth, prove no entry from that depth can enter the covered global Pareto set.
- If a depth can contribute Pareto entries, prove their omission cannot change the lower power-ordered median.
- The proof must cover strict feasible and fallback candidate paths.

Recommended analyzer:

- Run the exact opt3 search first.
- Replay proposed depth-screening rules offline.
- Report omitted depths, exact global Pareto contribution by depth, median-index movement, selected-entry drift, and QoR deltas.
- Record lower-bound assumptions in the report, including whether they are formal bounds or heuristics.

Default policy:

- Do not add default-on depth screening or epsilon caps without the proof above.
- If the project accepts approximate behavior later, make it explicit in config/report output and include omitted-entry counts and an exact-mode comparison hook.

## Near-Term Work Plan

1. Add the characterization reachability analyzer and run it on opt3 `ics55_dev 0.5/0.5`.
2. Add the frontier legality equivalence harness with no pruning and run it on the same baseline plus one forced fallback case.
3. If reachability shows material unused characterization work, prototype exact lazy characterization behind eager-vs-lazy dual comparison.
4. If legality harness shows large would-prune volume with exact signature equality, prototype incremental legality pruning before upper-level composition.
5. Start the backpointer representation only after the analyzers clarify which structural keys must be stable.

## Bottom Line

After rollback, opt3 is a valid and measured production baseline: `31.756 s` synthesis and `47.649 s` total CTS runtime on `ics55_dev`.

The next runtime gains should come from proving work unreachable or illegal before it is generated. The safest path is analyzer-first: characterize reachability, prove frontier legality equivalence, then refactor topology identity away from candidate-local pattern ids. Depth screening remains attractive, but it should stay offline or explicitly approximate until it proves no global Pareto or power-median drift.
