# Current Status Audit

Date: 2026-05-11

## Status

The active optimization/prototype loop is stopped. The requested source and focused test scopes have been restored to the opt3 backup ref, and the active Trellis task documentation has been compacted.

Opt3 rollback source of truth:

```text
.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/reports/rollback_to_opt3_report.md
```

Canonical compact attempt table:

```text
.trellis/tasks/05-09-icts-htree-dominance-pruning-research/research/experiment_decision_table.md
```

## Cleanup Result

| Item | Result |
| --- | --- |
| Source/test restore | Requested iCTS setup, H-tree, characterization, and focused test scopes match the opt3 backup ref. |
| Untracked experiment test | `src/operation/iCTS/test/module/characterization/CharBuilderPresamplingPolicyTest.cc` was removed. |
| Active task artifacts | Raw artifacts and research-only scripts were removed from this task directory. |
| Technology-specific material | Active-task files and artifact directories based on second-design testing were removed rather than kept as live references. |
| Failed-attempt documentation | Reduced to one compact decision table. |

## Current Decisions

| Area | Decision | Stop/revisit rule |
| --- | --- | --- |
| Runtime baseline | Treat opt3 as the restored code baseline for this cleanup. | Do not resume post-opt3 prototype code without a new task and proof plan. |
| Late H-tree frontier pruning | Stopped as a primary runtime path. | Revisit only with a proof that avoids expensive upstream characterization/materialization work. |
| Broad terminal skip | Rejected after cross-design relaxed-slew safety failure. | Do not promote broad topology-only skip. |
| Narrow terminal skip | Historical candidate only after this source restore. | Revisit only with explicit certificate-backed scope and default-off semantics. |
| Suffix skip / manifest path | Historical empirical prototype only after this source restore. | Revisit only in a new loop with verifier identity, fail-closed policy, and explicit opt-in semantics. |
| Sparse sampling | Historical empirical sampling-space branch, not exact dominance. | Stop on selected-structure, objective, or boundary-surface drift. |

## Next Work Rule

Any future pruning work must start from the compact table and write a new loop preface before code changes. The preface must state the failed attempts reviewed, why the new direction is materially different, the hypothesis, proof/evidence target, validation matrix, and stop condition.
