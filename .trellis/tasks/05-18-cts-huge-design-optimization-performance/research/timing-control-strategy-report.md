# CTS Huge Design Optimization Timing-Control Strategy Report

## Executive Summary

The best next direction is a topology-aware target-arrival-window solver with exact fast STA acceptance.

The current huge optimization can now finish, but it spends too many exact timing-only trials on candidates whose direction is wrong. The v3 run accepted one legal batch and improved skew from `0.1154 ns` to `0.1060 ns`, but it spent `456.9133 s` in `32` exact trials and missed the `0.0800 ns` target. The saved log shows that many high-ranked candidates were legal but worsened skew to `0.17-0.32 ns`.

The recommended replacement is:

- derive a per-iteration target arrival window;
- classify subtrees as late-dominated, early-dominated, or mixed;
- only generate monotone actions on side-pure branches;
- solve a small coordinated batch as a coverage/knapsack problem;
- verify with fast STA timing-only updates;
- accept only if exact skew/cap/slew does not regress.

This keeps the current safety contract, but uses tree structure to avoid obviously bad candidates before exact STA.

## Current Baseline Evidence

Committed baseline:

```text
0a332101d perf(icts): improve CTS optimization huge runtime
```

Huge v3 log:

```text
scripts/design/ics55_huge_dev/.trellis_scalable_huge_80ps_v3.log
```

Important metrics:

| Metric | Value |
|---|---:|
| fast STA nodes | 211483 |
| fast STA nets | 44737 |
| sinks | 122010 |
| optimizable buffers | 44736 |
| target skew | 0.0800 ns |
| initial skew | 0.1154 ns |
| optimized skew | 0.1060 ns |
| exact timing-only trials | 32 |
| accepted mutations | 4 |
| batch trial eval runtime | 456.9133 s |
| optimization runtime | 502.000 s |

Examples of bad top-ranked candidates:

| Candidate Result | Candidate Skew |
|---|---:|
| rejected, no improvement | 0.300956 ns |
| rejected, no improvement | 0.299430 ns |
| rejected, no improvement | 0.302587 ns |
| rejected, no improvement | 0.270607 ns |
| rejected, no improvement | 0.325074 ns |

The problem is not cap/slew legality. The v3 run had:

```text
cap_rejected_count = 0
slew_rejected_count = 0
```

The problem is candidate timing direction.

## External Research Notes

OpenROAD/TritonCTS is relevant because it emphasizes characterization-driven CTS. TritonCTS 2.0 performs on-the-fly characterization and lets users configure slew/cap characterization ranges, which supports the principle that CTS optimization should be driven by current slew/load behavior rather than static heuristics alone: https://openroad-test.readthedocs.io/en/latest/main/src/cts/README.html

Clock skew scheduling literature treats intentional clock latency assignment as a timing optimization problem rather than pure zero-skew minimization. The multi-parameter clock skew scheduling paper describes CSS as a powerful optimization technique and discusses specialized formulations that outperform generic LP/convex solvers for clock-period and slack objectives: https://www.scholars.northwestern.edu/en/publications/multi-parameter-clock-skew-scheduling-3

Delay insertion in clock skew scheduling is relevant because it shows intentional delay can be a first-class control variable instead of only upsizing late paths. The ISPD 2005 work reports average clock period improvement from delay insertion over standard clock skew scheduling: https://researchdiscovery.drexel.edu/esploro/outputs/conferenceProceeding/Delay-insertion-method-in-clock-skew/991014877828304721?institution=01DRXU_INST&recordUsage=false&skipUsageReporting=true

Dynamic programming is a classic buffer insertion/sizing technique in EDA. It is useful conceptually because CTS buffer sizing has discrete master choices, capacitance states, and delay states; however, a full DP over the huge committed tree would need careful pruning to avoid state explosion: https://www.sciencedirect.com/topics/computer-science/buffer-insertion

Multi-domain clock skew scheduling is especially relevant to huge CTS because it avoids arbitrary per-sink latency assignments and instead groups sinks into a small number of implementable latency domains. The DATE 2011 paper notes the practical limitation of arbitrary clock delays and motivates domain-limited schedules: https://users.eecs.northwestern.edu/~haizhou/publications/date11zhi.pdf

## Strategy Options Considered

### Option A: Increase Exact Trial Budget

This was already tested implicitly:

- 4 trials: no improvement;
- 8 trials: `0.1154 ns -> 0.1105 ns`;
- 16 trials: `0.1154 ns -> 0.1060 ns`.

This improves QoR but scales poorly because each huge exact timing-only trial costs about `14 s`. It is not a sufficient strategy.

### Option B: Full Useful-Skew Scheduling

Use a timing graph and solve sink latency ranges from setup/hold constraints, then drive CTS arrivals into those ranges.

Pros:

- theoretically strong;
- aligns with useful-skew literature;
- can optimize real timing instead of only clock spread.

Cons:

- requires data-path timing constraints and likely full STA data;
- user explicitly does not want full iSTA in the optimization loop;
- mapping arbitrary sink latencies onto an existing tree can be hard.

Recommendation:

- not first implementation;
- consider one-time STA-derived target intervals later, not per-candidate STA.

### Option C: Multi-Domain Arrival Balancing

Group sinks into a small number of arrival domains or windows, then tune branches toward those domains.

Pros:

- practical for implementation because only a few latency values are needed;
- generalizes to huge designs;
- can be approximated from one fast STA snapshot;
- avoids arbitrary per-sink targets.

Cons:

- current optimization objective is min spread, not setup/hold timing closure;
- domain assignment still needs robust mapping to tree branches.

Recommendation:

- use a simplified one-domain or two-sided target window first;
- extend to multi-domain if useful-skew data becomes available.

### Option D: Dynamic Programming Over Tree Sizing

At each subtree, enumerate possible `(arrival delta, cap, slew, area)` states and combine them bottom-up.

Pros:

- natural fit for discrete buffer masters and tree structure;
- can provide stronger optimality than greedy scoring.

Cons:

- state explosion on `44736` buffers unless heavily pruned;
- requires a proxy timing model for state transitions;
- exact verification still needed.

Recommendation:

- use DP-like pruning locally within a branch or level, but do not start with full-tree DP.

### Option E: Topology-Aware Target Window Batch Solver

Build branch statistics from exact current fast STA arrivals and choose actions only where subtree timing composition makes the direction safe.

Pros:

- directly addresses observed bad candidates;
- no full iSTA loop;
- O(N) tree analysis plus O(B * M) action scoring;
- exact fast STA acceptance prevents regression;
- works on huge and small designs.

Cons:

- proxy scoring still approximate;
- exact trial cost remains high if candidate quality is poor;
- requires new topology interval data structure.

Recommendation:

- best first implementation.

## Recommended Algorithm

### 1. Arrival Window

For each iteration:

```text
spread_target = config skew_bound
center = median(arrival) or (min_arrival + max_arrival) / 2
lo = center - spread_target / 2
hi = center + spread_target / 2
```

Define:

- late sink: `arrival > hi`;
- early sink: `arrival < lo`;
- in-window sink: otherwise.

If target is very tight, use a relaxed staged target:

```text
iteration_target = max(config_target, current_skew * shrink_ratio)
```

This avoids overreacting to a far-away 0ps target.

### 2. Subtree Classification

For each buffer output node:

- compute subtree sink count;
- compute late count and violation sum;
- compute early count and violation sum;
- compute mixed risk:

```text
mixed_risk = min(late_count, early_count) / max(1, late_count + early_count)
```

Classify:

- late-pure: many late sinks, almost no early sinks;
- early-pure: many early sinks, almost no late sinks;
- mixed: both late and early significant;
- neutral: mostly in-window.

Only pure branches should generate high-priority actions.

### 3. Action Direction

When target is not met:

- late-pure branch:
  - allow upsize;
  - allow only if parent cap headroom remains legal or exact verify will reject.
- early-pure branch:
  - allow downsize/delay increase only if it does not overlap late sinks;
  - downsize should be lower priority than fixing late branches unless current max path cannot be improved.
- mixed branch:
  - reject at high levels;
  - defer to child branches.

When target is met:

- area recovery becomes primary;
- downsize is allowed if exact fast STA keeps target compliance.

### 4. Batch Selection

Build a batch using non-overlap rules:

- do not include both ancestor and descendant unless explicitly in a same-path staged move;
- prefer one action per topological branch per iteration;
- cap max actions by design size.

Score:

```text
benefit = reduced_late_violation + reduced_early_violation
risk = mixed_risk_weight + cap_risk + slew_risk + ancestor_overlap_risk
cost = max(0, area_delta) when target unmet; absolute area after target met
score = benefit / (1 + cost + risk)
```

Exact scoring coefficients should be calibrated from `ics55_dev` and `ics55_huge_dev`.

### 5. Adaptive Verification

Instead of verifying the first 16 sorted candidates, verify:

1. primary combined batch;
2. late-only batch;
3. early-only batch;
4. if primary fails, split by topological depth;
5. if a side batch fails, binary-shrink by action score.

Hard cap exact trials per iteration:

- huge: 4-8;
- small: can use exact solver or 8-16 trials.

### 6. Acceptance Gate

Accept only when exact fast STA says:

```text
state.valid
cap.legal
slew.legal
candidate.skew < current.skew - epsilon   // target not met
```

If target is already met:

```text
candidate.skew <= target + epsilon
candidate.area < current.area - epsilon
```

This is the no-worsening guarantee.

## Why This Generalizes

Small designs:

- exact solver remains available;
- target-window solver can run with a higher exact budget;
- acceptance gate prevents regression.

Huge designs:

- candidate scoring is O(tree size), not O(candidate exact trials);
- exact trial count is bounded;
- branch-purity filtering avoids global skew explosions from mixed high-level moves.

Different topologies:

- the algorithm uses parent/child/subtree intervals, not H-tree-specific assumptions;
- it applies to any committed CTS clock tree represented in fast STA.

## Expected Improvement

Compared with v3:

| Metric | Current v3 | Target |
|---|---:|---:|
| exact trials | 32 | 6-12 |
| batch trial eval | 456.9 s | roughly 85-180 s if trial cost remains 14 s |
| optimized skew | 106.0 ps | closer to 80 ps; first target is below 100 ps |
| cap/slew regressions | 0 accepted | 0 accepted |

The main expected QoR gain comes from rejecting high-level mixed early-downsize moves before exact trial.

## Risks

- A pure subtree by sink count can still contain the global min/max after exact timing changes because slew propagates nonlinearly.
- Upsizing a buffer can increase upstream pin cap and slow sibling paths.
- Downsize can improve area but worsen skew if applied too high.
- Current exact trial cost is still high, so bad proxy quality remains expensive.

Mitigation:

- exact fast STA acceptance;
- cap/slew baseline checks;
- branch purity threshold;
- fallback shrink;
- log per-iteration candidate class distribution.

## Recommended Next Task Scope

Implement Option E first:

1. topology interval/subtree statistics;
2. target-window action scoring;
3. pure-branch risk filter;
4. adaptive batch verification;
5. run 80ps/40ps/0ps on `ics55_dev` and `ics55_huge_dev`.

Do not implement full useful-skew scheduling or topology-changing delay insertion in the first pass.
