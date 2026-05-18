# Design: CTS huge design optimization performance

## Objective

Replace the current "score many candidate batches, exact-check the first few" huge-case search with a topology-aware timing-control solver that constructs one or a few high-confidence batches per iteration and uses fast STA only as the ground-truth verifier.

The design goal is not to make a proxy timing engine authoritative. Proxy models may rank or filter candidates, but acceptance remains exact fast STA based.

## Recommended Strategy

Use a target-arrival-window, branch-purity, and adaptive-batch solver.

1. Snapshot current fast STA timing once per iteration.
2. Derive a target arrival window:
   - target spread: `CONFIG_INST.get_skew_bound()`;
   - default center: current arrival median or midpoint between current min/max;
   - late sinks: arrivals above the upper window;
   - early sinks: arrivals below the lower window.
3. Build topology intervals over the clock tree:
   - parent array already exists in `TopologyIndex`;
   - add a DFS/Euler view so each buffer output owns a sink subtree interval;
   - maintain prefix counts/sums for late, early, and in-window sinks.
4. Score actions by predicted window violation reduction, not by raw branch coverage:
   - upsize only helps branches dominated by late sinks;
   - downsize or delay-increase only helps branches dominated by early sinks;
   - mixed branches receive a high risk penalty or are excluded until lower-level refinements.
5. Solve batch selection as a constrained coverage/knapsack problem:
   - choose non-overlapping or ancestor-compatible actions;
   - maximize estimated violation reduction;
   - minimize area increase and mixed-branch risk;
   - limit action count per batch by graph size.
6. Exact-verify one primary batch and a small adaptive fallback set:
   - if primary batch improves skew, accept it;
   - if it fails, split by topology level or side and verify at most a small fixed retry budget;
   - accept only if exact fast STA state is valid and does not worsen skew/cap/slew.
7. Stop when target skew is met, no verified improvement exists, or runtime/trial budget is reached.

## Why This Should Fix The Current Failure Mode

The current scorer ranks candidates that reduce area and touch many frontier paths, even when the touched subtree contains both early and late sinks. On `ics55_huge_dev`, several top candidates legally reduce area but widen skew from `0.115 ns` to `0.30 ns`. The recommended solver treats mixed high-level subtrees as risky and favors lower-level, side-pure subtrees whose sinks are mostly on the same side of the target window.

This changes candidate generation from "try the highest heuristic scores" to "construct monotone latency moves against measured timing violations".

## Non-Regression Contract

Every accepted batch must satisfy all of the following under fast STA:

- `candidate.valid == true`;
- no new cap violation relative to the baseline;
- no new slew violation relative to the baseline;
- if target is not yet met: `candidate.skew < current.skew - epsilon`;
- if target is met: prefer smaller area without losing target compliance;
- if no candidate passes, leave the design unchanged.

The proxy model never commits changes. It only determines which candidates are worth exact verification.

## Timing Control Boundary

Allowed in the first implementation:

- buffer master sizing up/down;
- target-window balancing;
- topology-aware batch selection;
- exact fast STA timing-only verification.

Deferred unless first stage proves insufficient:

- inserting/deleting buffers;
- explicit delay cells;
- full iSTA or OpenSTA calls inside the loop;
- data-path useful-skew scheduling that requires full timing graph constraints.

## Runtime Shape

Expected per iteration:

- O(nodes + nets + sinks) to build arrivals and topology intervals;
- O(buffers * candidate_masters) to score legal sizing actions;
- O(k) exact fast STA trials, where `k` is a small constant, targeted around 2-6 on huge.

This should beat the current v3 huge profile:

- current: 32 exact trials, `456.9 s` trial eval;
- target: no more than roughly 6-12 exact trials for the same 80ps case, with better candidate quality.

## Compatibility

Small designs can use the same solver without losing QoR by setting the exact verification budget higher or by retaining the existing exact solver below a graph-size threshold. The acceptance gate guarantees no accepted regression either way.

