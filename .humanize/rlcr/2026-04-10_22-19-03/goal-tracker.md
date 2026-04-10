# Goal Tracker

<!--
This file tracks the ultimate goal, acceptance criteria, and plan evolution.
It prevents goal drift by maintaining a persistent anchor across all rounds.

RULES:
- IMMUTABLE SECTION: Do not modify after initialization
- MUTABLE SECTION: Update each round, but document all changes
- Every task must be in one of: Active, Completed, or Deferred
- Deferred items require explicit justification
-->

## IMMUTABLE SECTION
<!-- Do not modify after initialization -->

### Ultimate Goal
Bring iEDA's exported harden timing-model Liberty for `NV_NVDLA_partition_m` up to the project's staged alignment standard against OpenROAD, first closing structural and timing-semantic gaps and then advancing toward quantitative and downstream replaceability parity without hardcoding any single PDK's Liberty traits into the main flow.

Source plan: docs/rlcr_plans/2026-04-10-ieda-liberty-alignment-plan.md

### Acceptance Criteria
<!-- Each criterion must be independently verifiable -->
<!-- Claude must extract or define these in Round 0 -->

1. Baseline generation and regression harness are deterministic: the golden-case Liberty export runs from fixed inputs, writes to controlled artifact paths, and produces machine-checkable failure signals.
2. Structural Liberty correctness is restored: duplicated pin names are zero, logical pin coverage is complete, and bus/type semantics are preserved when design context is available.
3. Library-level metadata is complete: emitted Liberty includes required units, `delay_model`, and other top-level serializer outputs needed for downstream interpretation.
4. Timing semantics are complete: every `timing()` block has the required `timing_type`, delay arcs serialize their real type, and setup plus hold arcs coexist in the exported model.
5. Export behavior is configuration-driven and PDK-agnostic: units, thresholds, templates, index grids, and interface semantics come from input libraries, database context, or explicit config instead of benchmark- or PDK-specific hardcoded rules.
6. Automated OpenROAD-vs-iEDA comparison reports layered progress across structure, semantics, and quantitative behavior, with quantitative checks targeting the acceptance thresholds once table semantics exist.

---

## MUTABLE SECTION
<!-- Update each round with justification for changes -->

### Plan Version: 1 (Updated: Round 0)

#### Plan Evolution Log
<!-- Document any changes to the plan with justification -->
| Round | Change | Reason | Impact on AC |
|-------|--------|--------|--------------|
| 0 | Initial plan | - | - |

#### Active Tasks
<!-- Map each task to its target Acceptance Criterion and routing tag -->
| Task | Target AC | Status | Tag | Owner | Notes |
|------|-----------|--------|-----|-------|-------|
| Task 1: Freeze baseline and add machine-checkable alignment tests | AC-1, AC-2, AC-3, AC-4 | pending | coding | claude | Establish deterministic harness and failing regressions for today's known gaps. |
| Task 2: Refactor Liberty serialization so one logical pin maps to one emitted block | AC-2 | pending | coding | claude | Eliminate duplicate pin emission and preserve stable ordering. |
| Task 3: Emit complete library metadata, bus definitions, and type definitions | AC-2, AC-3, AC-5 | pending | coding | claude | Promote existing IR metadata into real serializer output and preserve bus structure. |
| Task 4: Complete timing semantics, especially delay `timing_type` and hold arcs | AC-4, AC-5 | pending | coding | claude | Merge max/min export semantics and serialize missing delay arc type information. |
| Task 5: Separate export configuration from PDK specifics and add table configuration | AC-5 | pending | coding | claude | Introduce explicit export config instead of benchmark-specific implicit defaults. |
| Task 6: Add LUT/template/index generation as second-phase characterization improvement | AC-5, AC-6 | pending | coding | claude | Build configurable multi-sample characterization and explicit scalar fallback. |
| Task 7: Automate OpenROAD-vs-iEDA comparison and gate rollout with acceptance metrics | AC-6 | pending | coding | claude | Normalize structure/semantic/value comparisons and publish parity summaries. |

### Completed and Verified
<!-- Only move tasks here after Codex verification -->
| AC | Task | Completed Round | Verified Round | Evidence |
|----|------|-----------------|----------------|----------|

### Explicitly Deferred
<!-- Items here require strong justification -->
| Task | Original AC | Deferred Since | Justification | When to Reconsider |
|------|-------------|----------------|---------------|-------------------|

### Open Issues
<!-- Issues discovered during implementation -->
| Issue | Discovered Round | Blocking AC | Resolution Path |
|-------|-----------------|-------------|-----------------|
