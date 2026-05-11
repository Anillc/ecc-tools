# H-tree Dominance-Pruning Compact Decision Table

Date: 2026-05-11

This is the canonical active attempt history for the stopped iCTS H-tree dominance-pruning loop. It intentionally keeps only decision-grade rows. Long narratives, raw run logs, artifact matrices, technology-specific second-design files, and superseded research notes were removed from the active task directory.

## Compact Attempt Table

| Status | Attempt direction | Motivation | Evidence / experiment | Conclusion | Stop / revisit rule |
| --- | --- | --- | --- | --- | --- |
| Baseline restored | Opt3 source/test rollback | Stop the post-opt3 optimization branch and return active code to the stable runtime baseline. | Requested iCTS setup, H-tree, characterization, and focused test scopes were restored from `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`; diff versus that ref is empty for those scopes. | Active code is opt3 for the restored scopes. | Do not reapply post-opt3 prototype code without a new task and proof plan. |
| Keep signal | Runtime attribution | Find the runtime lever before designing more pruning. | Profiling showed characterization dominated the H-tree build while selected-depth final-join/frontier work was much smaller. | Runtime wins must remove characterization or earlier search-space work, not only final survivors. | Reject ideas that only shrink late frontiers after expensive work already ran. |
| Low value | Late exact-frontier pruning family | Reduce selected-depth frontier, exact-state product size, or local continuation carriers. | D1/result-state skyline, structural-profile, factorized product, continuation/rootward, lower-bound, and cross-context families either had weak wall-time value, high overhead, or insufficient exactness. | Late/local frontier cleanup is not the dominant lever under the strong exact-frontier contract. | Revisit only with a proof that also avoids expensive upstream materialization. |
| Rejected | Broad terminal-branch pre-sampling skip | Generalize the original single-benchmark terminal skip into a broader runtime rule. | A relaxed-slew cross-design counterexample showed missing witness/frontier drift even when selected QoR appeared matched. | Broad topology-only skip is unsafe. | Do not promote broad skip; stop on any missing witness, frontier drift, or skip-signature drift. |
| Historical candidate | Narrow observed-rank terminal skip | Recover part of the broad-skip benefit while excluding the known unsafe rank assignments. | The certificate-backed matrix passed in the prior prototype loop, but the code was removed by the opt3 source restore. | Potential exact candidate, but setup-bound and not active source. | Revisit only as a new default-off, certificate-backed loop with explicit scope. |
| Historical prototype | Output-slew overflow suffix skip | Avoid STA samples after observed output-slew overflow for a fixed topology/load/input-slew sweep. | Prior verifier stacks matched generated-characterization, segment-frontier, selected-surface, and metric signatures on the tested relaxed-slew matrix, but the proof remained empirical and design-dependent. | Strongest characterization-side prototype, but not an exact dominance proof and not active source after restore. | Revisit only with explicit empirical opt-in or a formal predicate-local certificate. |
| Stopped | Strict/tolerance certificate paths | Try to turn suffix skip into auto-enable behavior. | Strict Liberty monotonicity was too strong; margin/tolerance and pre-STA slew infeasibility lacked a conservative lower-bound propagation proof. | No safe unconditional auto-enable path. | Do not retry without a materially different proof contract. |
| Stopped | Exact STA request memoization | Check whether characterization work repeats exact STA requests. | Exact request duplicate analysis found no useful duplicate lever on the measured setup. | Memoization is not a runtime path for the observed workload. | Revisit only if a new design shows repeated exact signatures or a separately proven coarser key. |
| Stopped by audit | Internal max-cap feasibility | Check whether cap-feasibility pruning was missing before STA. | Code audit found internal driver-stage cap checks and external-load ceiling skips already implemented. | This exact pruning opportunity already exists. | Revisit only for a new feasibility dimension with a proof target. |
| Empirical/defer | Sparse slew/grid sampling and role-aware context | Reduce characterization by sampling fewer electrical points. | Sparse low-set and root/non-root variants could preserve selected results in some cases, but other subsets drifted and the branch changes the electrical lattice. | Empirical sampling-space thinning, not exact dominance. | Stop on selected-structure, objective, boundary, or surface drift. |
| Hygiene rule | Analyzer cleanup | Prevent rejected experiments from bloating active source and task docs. | Source was restored to opt3 and active docs were reduced to PRD, status audit, and this table. | Cleanup is hygiene, not a runtime optimization result. | Keep future attempts in compact tables; avoid append-heavy narratives. |

## Future Loop Preface

Each new loop must start by filling this out before code or analyzer work:

| Field | Required content |
| --- | --- |
| Failed attempts reviewed | Name the rows above that the new direction must avoid repeating. |
| Why this is different | State the new assumption and why old failures do not apply. |
| Hypothesis | One falsifiable statement about pruning value or proof feasibility. |
| Proof / evidence target | The exact certificate, equivalence check, or measured opportunity needed. |
| Validation matrix | Use relaxed-slew points unless the user changes the requirement. |
| Stop condition | The first mismatch, weak ceiling, unsafe counterexample, or proof gap that ends the loop. |
