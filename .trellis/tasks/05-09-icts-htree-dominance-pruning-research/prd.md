# research iCTS H-tree state-aware dominance pruning

## Goal

Research ways to reduce iCTS H-tree synthesis runtime without silently changing the accepted solution space.

As of 2026-05-11, the optimization/prototype loop is stopped. Source and focused test code for the iCTS H-tree/runtime-pruning experiments has been restored to the opt3 backup:

```text
refs/backups/icts-runtime-pre-next-optimizations-20260509-131717
558fb66cc7a06fd5b8aa731eea96ecbea6a8dfd5
```

The source-of-truth rollback note is:

```text
.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/reports/rollback_to_opt3_report.md
```

This task directory now keeps only compact planning/status documentation. Long experiment narratives, raw artifact logs, technology-specific second-design matrices, and superseded research-only helper scripts were removed from the active task directory.

## Current State

| Area | Current decision |
| --- | --- |
| Source/test code | Restored to the opt3 backup for the requested iCTS setup, H-tree, characterization, and focused test scopes. |
| Active optimization goal | Stopped/paused outside this task cleanup. Do not resume prototype work from these docs without a new loop preface. |
| Canonical attempt history | [`research/experiment_decision_table.md`](research/experiment_decision_table.md). |
| Status entry point | [`research/current_status_audit.md`](research/current_status_audit.md). |
| Raw technology-specific task artifacts | Removed from this active task directory. |

## Requirements For Any Future Loop

- Start from opt3 unless a new task explicitly chooses another baseline.
- Explain the exact code boundary of any proposed pruning rule before implementation.
- Prefer exact or provably safe pruning. Empirical sampling-space changes must be labeled as empirical.
- Add analyzer-first evidence before runtime behavior changes.
- Define the proof/evidence target, validation matrix, and stop condition before adding code.
- Do not run full `ecc_dev_tools`, `clang-tidy`, or IWYU during this research loop unless the user explicitly changes that constraint.
- Use only the relaxed-slew validation points `0.5/0.5`, `0.6/0.6`, and `0.75/0.75` unless the user changes the requirement.
- Any temporary analyzer or research-only C++ must carry:

```cpp
// TODO(icts-dominance-pruning-research): temporary analyzer; remove or promote after validation.
```

## Future Loop Preface

Before any new analyze/verify/implement/optimize loop, fill out a compact preface in the new research note or task:

| Field | Required content |
| --- | --- |
| Failed attempts reviewed | Which rows in the compact decision table the new direction must avoid repeating. |
| Why this is different | A short sequential-thinking conclusion describing the new assumption. |
| Hypothesis | One falsifiable statement about pruning value or proof feasibility. |
| Proof/evidence target | The exact certificate, equivalence check, or measured opportunity needed. |
| Validation matrix | The specific relaxed-slew runs and any focused tests. |
| Stop condition | The first mismatch, weak runtime ceiling, unsafe counterexample, or proof gap that ends the loop. |

## Out Of Scope

- Resuming the stopped optimization/prototype loop during this cleanup.
- Restoring deleted raw active-task artifacts.
- Promoting broad terminal-branch skip, suffix-skip, sparse-sampling, or manifest auto-enable behavior from this task.
- Running `ecc_dev_tools`, `clang-tidy`, or IWYU for this cleanup.
