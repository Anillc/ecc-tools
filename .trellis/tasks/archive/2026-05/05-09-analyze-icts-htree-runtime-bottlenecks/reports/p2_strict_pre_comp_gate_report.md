# P2 Strict-Feasible Sink-Load Coverage Before Root Compensation

Date: 2026-05-09

> Superseded on 2026-05-09 by `rollback_to_opt3_report.md`. This file is a historical investigation report. The P2 debug/prototype code and focused tests described below were removed when production H-tree code was restored to `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`. P2 remains investigation-only and is not default-enabled.

## Scope

This P2 prototype tested whether iCTS H-tree strict-feasible sink-load-region legality and leaf-load coverage can be moved before root-driver compensation.

The key correctness risk is ordering: current `BuildPatternSearch` applies root-driver compensation, then rebuilds the H-tree state frontier with compensated delay/power, and only later applies sink-load-region filtering and global leaf-load coverage. A blind pre-compensation gate can change the retained frontier because compensation can affect same-state delay/power dominance.

## Implementation State

P2 is not enabled as the default production path.

The code now contains two opt-in paths:

- `ICTS_HTREE_DEBUG_STRICT_PRE_COMP_GATE=1`: non-invasive same-run equivalence machinery. It preserves the current post-compensation selection path and compares it against a candidate path that applies strict-feasible sink-load coverage before root compensation, then compensates and prunes.
- `ICTS_HTREE_ENABLE_STRICT_PRE_COMP_GATE=1`: an env-gated prototype optimized path. It is intentionally not the default because the proof is only established for the current `ics55_dev` fixture, not for a broader design matrix.

The debug helper compares Pareto-affecting sets, not only selected scalar metrics. The signature includes electrical boundary indices, compensated delay/power, and topology pattern id, so equal-cost but different topology patterns are not silently treated as equivalent.

Post-review hardening:

- The raw pre-compensation frontier is copied only for the debug equivalence path. The env-gated prototype keeps the deferred frontier by move, avoiding a redundant full-frontier copy.
- The env-gated prototype preserves the lazy fallback contract. If a global strict-feasible selection later fails, fallback materialization replays root-driver compensation and post-compensation state pruning over the unfiltered candidate frontier instead of reusing the strict pre-gated subset.
- The debug replay snapshots sink-load-region legality context before the current-path filter mutates it, so the candidate comparison starts from the same cache/pruning state as the current path.

## Focused Regression Coverage

Added unit coverage in `HTreeTest`:

- `StrictPreCompGateEquivalenceAcceptsIdenticalParetoAffectingSet`: validates the helper accepts identical Pareto-affecting sets.
- `StrictPreCompGateEquivalenceDetectsBlindPreFilteringDominanceChange`: constructs a small Pareto set where removing an intermediate point changes the Pareto-affecting set, proving the helper detects a risky blind pre-filter.

Existing P1 tests for tie-preserving global selection, per-depth Pareto independence, and lazy fallback still pass.

## Debug Equivalence Result

Command:

```bash
cd scripts/design/ics55_dev
ICTS_HTREE_DEBUG_STRICT_PRE_COMP_GATE=1 ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifacts:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p2_strict_pre_comp_gate_debug/run.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p2_strict_pre_comp_gate_debug/time.txt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p2_strict_pre_comp_gate_debug/cts.log`

Per-depth same-run comparison:

| Depth | Equivalent | Current covered strict entries | Candidate pre-gate entries | Candidate post-prune entries | Current Pareto | Candidate Pareto |
| ---: | :---: | ---: | ---: | ---: | ---: | ---: |
| 8 | yes | 168756 | 170436 | 168756 | 296 | 296 |
| 7 | yes | 79332 | 79332 | 79332 | 256 | 256 |
| 6 | yes | 48092 | 48092 | 48092 | 160 | 160 |
| 5 | yes | 66412 | 66412 | 66412 | 112 | 112 |

No `equivalent=false` line was emitted.

The depth 8 result is especially useful evidence: candidate pre-gate entries were larger than the current post-compensation pruned set, but after candidate compensation and state pruning the Pareto-affecting set matched the current path.

## Env-Gated Prototype Result

Command:

```bash
cd scripts/design/ics55_dev
ICTS_HTREE_ENABLE_STRICT_PRE_COMP_GATE=1 ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifacts:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p2_strict_pre_comp_gate_enabled/run.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p2_strict_pre_comp_gate_enabled/time.txt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p2_strict_pre_comp_gate_enabled/cts.log`

Compared with the current/debug path, the enabled prototype preserved:

| Metric | Value |
| --- | --- |
| selected depth | 5 |
| selected topology pattern id | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` |
| selected raw H-tree metric | `0.2897 ns / 192.458 uW` |
| selected compensated H-tree metric | `0.4959 ns / 217.271 uW` |
| selected physical root load | `0.1428 pF` |
| inserted H-tree buffers | 40 |
| final clock buffer count | 360 |
| total clock network wirelength | `43151.203 um` |
| setup WNS / hold WNS | `7.302 ns / 0.008 ns` |

Runtime in this single run:

| Run | Synthesis s | Total CTS s | External wall s |
| --- | ---: | ---: | ---: |
| debug/current path | 31.393 | 47.625 | 69.20 |
| env-gated prototype | 31.169 | 46.826 | 68.22 |

The runtime delta is small and should not be overinterpreted from one run. The main P2 outcome is correctness evidence and the ability to run broader proof-driven experiments with the env-gated path.

## Decision

Do not enable P2 as the default production optimization yet.

Reason:

- The current `ics55_dev` design/fixture passed both same-run Pareto-affecting equivalence and env-gated QoR consistency.
- However, the ordering risk is real: root-driver compensation can change delay/power dominance inside the H-tree state frontier, and this was only proven on one design/config.
- A focused unit test now demonstrates that blind pre-filtering can change a Pareto-affecting set, so broad enablement needs more design/config coverage or a stronger formal dominance proof.

The safe state after P2 is:

- current default path unchanged for production behavior;
- non-invasive debug equivalence machinery available;
- env-gated prototype path available for additional experiments;
- focused tests guard the equivalence helper.

## Recommended Next Step

Before promoting this to a default production optimization, run `ICTS_HTREE_DEBUG_STRICT_PRE_COMP_GATE=1` and `ICTS_HTREE_ENABLE_STRICT_PRE_COMP_GATE=1` on a small matrix:

- current `0.5/0.5` relaxed slew case;
- original `0.05/0.05` strict slew case;
- at least one forced-fallback or no-strict-feasible case;
- at least one design with different sink clustering/load distribution.

Promotion criteria:

- no per-depth `equivalent=false` debug result;
- selected topology pattern id, segment pattern ids, delay/power, root load, buffer count, wirelength, and timing metrics match or have an accepted bounded-drift policy;
- fallback cases prove the path returns to the current compensated fallback behavior instead of selecting uncompensated candidates.
