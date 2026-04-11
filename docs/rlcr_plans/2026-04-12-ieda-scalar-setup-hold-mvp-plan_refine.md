# iEDA Scalar Setup-Hold MVP Refined Plan

## Goal Description

Refine the current scalar setup/hold export path in iEDA so that exported setup and hold constraints stop collapsing into exporter-side `check_margin` only. The work must stay local to the ETM export path in `StaCharacterTiming`, preserve the current characterization-side data term, reuse existing iSTA clock-pair / clock-path / check-arc infrastructure, keep CPPR disabled for timing-model export semantics, and avoid reopening the previously rolled-back clock reconstruction branch. Preserved full-STA seq-check snapshots remain supporting evidence and matched-case fallback only; they are no longer assumed to be the universal scalar primary source.

## Acceptance Criteria

Following TDD philosophy, each criterion includes positive and negative tests for deterministic verification.

- AC-1: The scalar setup/hold exporter restores a real target-clock subtraction term from existing iSTA/exporter data instead of collapsing `clock_arrival_delta_ns` to `data_arrival_delta_ns`.
  - Positive Tests (expected to PASS):
    - The setup/hold export path in `StaCharacterTiming.cc` no longer assigns `clock_arrival_delta_ns = data_arrival_delta_ns` for the golden regression endpoint.
    - Trace or temporary diagnostics show the selected target-clock subtraction term and its provenance for the golden endpoint.
  - Negative Tests (expected to FAIL):
    - An implementation that still collapses the subtraction and therefore exports `constraint ~= check_margin` is rejected.
    - An implementation that introduces new generic clock propagation or exporter-local clock reconstruction state is rejected.

- AC-2: The scalar setup/hold exporter keeps `check_margin` sourcing honest and provenance-aware.
  - Positive Tests (expected to PASS):
    - Preserved full-STA `constrain_value` is only consumed when the preserved seq snapshot truly matches the exported endpoint/check/path case.
    - Temporary diagnostics clearly show whether each scalar value uses matched preserved seq data or falls back to exporter-side Liberty sampling.
  - Negative Tests (expected to FAIL):
    - An implementation that blindly swaps all scalar setup/hold arcs to preserved `constrain_value` is rejected.
    - An implementation that claims the preserved setup snapshot matches the golden input-port case when the snapshot still starts from an internal register clocked path is rejected.

- AC-3: The focused OpenROAD-alignment regression passes with the repaired scalar formula.
  - Positive Tests (expected to PASS):
    - `LibertyAlignmentTest.setup_hold_constraint_values_track_openroad_reference_scale` passes after the exporter path is updated.
    - The generated Liberty for the golden case shows scalar `setup_rising`, `setup_falling`, `hold_rising`, and `hold_falling` values aligned with OpenROAD reference scale.
  - Negative Tests (expected to FAIL):
    - A build that still emits the currently observed mismatched values such as `22.13791586 ps`, `5.59040786 ps`, `-8.69214739 ps`, and `1.09470014 ps` remains failing.
    - A build that passes only because of hard-coded pin names, library names, or benchmark-specific branching is rejected.

- AC-4: The MVP stays narrowly scoped to scalar export and does not mutate generic iSTA semantics.
  - Positive Tests (expected to PASS):
    - Code changes remain concentrated in `StaCharacterTiming.cc` and any minimal supporting declarations in `StaCharacterTiming.hh`, with test-only changes in the current alignment tests if needed.
    - The implementation keeps the existing Liberty re-sampling path as an explicit fallback for unmatched margin cases rather than deleting it outright.
  - Negative Tests (expected to FAIL):
    - Any change that introduces new generic propagation states, rewrites core graph/STA propagation semantics, or reopens exporter-local clock reconstruction is rejected.
    - Any change that hard-codes ASAP7-specific table names, units, template names, or benchmark pin behavior is rejected.

## Path Boundaries

Path boundaries define the acceptable range of implementation quality and choices.

### Upper Bound (Maximum Acceptable Scope)

The implementation introduces one minimal exporter-local path in `StaCharacterTiming` that restores a real target-clock subtraction term from existing iSTA/exporter data, removes the current subtraction-collapsing hack, and keeps `check_margin` sourcing honest. Preserved seq snapshots are only used when they actually match the exported case; otherwise the existing Liberty re-sampling path remains a clearly isolated fallback. Short-lived diagnostics prove which target-clock term and which margin source were selected for the golden endpoint.

### Lower Bound (Minimum Acceptable Scope)

The implementation changes only the scalar setup/hold export path needed for the golden regression so that the exported formula uses a real target-clock subtraction term and no longer degenerates to `check_margin` only. The focused regression passes, and no generic iSTA behavior is modified.

### Allowed Choices

- Can use:
  - Existing preserved seq-check snapshot data in `StaCharacterTiming`
  - Existing iSTA clock-pair / capture-edge information when it is already available through current objects
  - Local helper selection logic in `StaCharacterTiming.cc`
  - Minimal supporting declarations in `StaCharacterTiming.hh`
  - Focused diagnostics that can be removed after stabilization
  - The current golden regression in `LibertyAlignmentTest.cc`
- Cannot use:
  - Generic iSTA propagation rewrites
  - New exporter-local clock propagation/reconstruction states
  - Full-STA arrival/required snapshots as the primary scalar export formula
  - Benchmark-specific hard coding
  - ASAP7-specific template/unit assumptions baked into logic

## Feasibility Hints and Suggestions

> **Note**: This section is for reference and understanding only. These are conceptual suggestions, not prescriptive requirements.

### Conceptual Approach

The most direct path is:

1. Preserve the existing scalar regression as the failing guard.
2. In `StaCharacterTiming`, find the point where exported scalar setup/hold values currently collapse the subtraction to `check_margin`.
3. Recover the smallest existing target-clock subtraction term that already lives in current iSTA/exporter data rather than inventing a new propagation path.
4. Keep preserved seq snapshots only as matched-case evidence or fallback for `check_margin`; do not force them into unmatched setup cases.
5. Add minimal diagnostics to verify which target-clock term and margin source the golden endpoint is taking.
6. Rebuild `iSTATest`, rerun the focused regression, and inspect the generated Liberty once.

### Relevant References

- `/home/zhaoxueyan/code/write-lib_back/docs/ieda_plans/2026-04-11-ieda-minimal-arrival-subtraction-plan.md` - Current main plan and latest failure context
- `/home/zhaoxueyan/code/write-lib_back/docs/ieda_plans/2026-04-11-ieda-check-slew-pairing-note.md` - Why scalar MVP should avoid full pairing reconstruction
- `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.cc` - Main exporter repair surface
- `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.hh` - Snapshot struct and local declarations
- `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyAlignmentTest.cc` - Focused regression for scalar setup/hold alignment
- `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/CharacterTimingTestCommon.hh` - Golden-case helpers and output paths
- `/home/zhaoxueyan/code/write-lib_back/OpenROAD/src/sta/search/MakeTimingModel.cc` - Reference exporter-side formula semantics

## Dependencies and Sequence

### Milestones

1. Baseline Lock
   - Phase A: Confirm the current focused regression still fails for the known scalar setup/hold mismatch.
   - Phase B: Keep the test output clear enough to identify which of the four scalar values still diverge from OpenROAD.

2. Target-Clock Repair
   - Phase A: Identify the smallest existing quantity that matches OpenROAD-like target-clock subtraction semantics for the golden endpoint.
   - Phase B: Remove the subtraction-collapsing hack and route scalar setup/hold through that existing target-clock term.

3. Margin Guardrails and Verification
   - Phase A: Add short-lived diagnostics around target-clock selection, match selection, and fallback activation.
   - Phase B: Rebuild `iSTATest`, rerun the focused regression, inspect generated Liberty output, and remove any excess debug noise.

The baseline regression must exist before the exporter change is trusted. The target-clock repair must land before any margin-source discussion is meaningful. Verification is last and must use fresh build and test evidence.

## Task Breakdown

Each task must include exactly one routing tag:
- `coding`: implemented by Claude
- `analyze`: executed via Codex (`/humanize:ask-codex`)

| Task ID | Description | Target AC | Tag (`coding`/`analyze`) | Depends On |
|---------|-------------|-----------|----------------------------|------------|
| task1 | Confirm the focused scalar setup/hold regression is the active failing baseline and improve failure visibility only if needed | AC-2 | coding | - |
| task2 | Identify the exact scalar setup/hold decision point in `StaCharacterTiming.cc` and map the current mismatch to the missing target-clock subtraction term | AC-1 | analyze | task1 |
| task3 | Implement the minimal exporter-local fix that restores a real target-clock subtraction term and removes the subtraction-collapsing hack | AC-1, AC-4 | coding | task2 |
| task4 | Keep `check_margin` sourcing honest: use matched preserved seq data only when it really matches, and keep Liberty re-sampling as fallback otherwise | AC-2, AC-4 | coding | task3 |
| task5 | Add or tighten short-lived diagnostics so target-clock selection, match success, and fallback activation are visible during the golden regression | AC-2 | coding | task4 |
| task6 | Rebuild `iSTATest`, rerun the focused regression, inspect the generated Liberty once, and remove excess debug noise if present | AC-3, AC-4 | coding | task5 |

## Claude-Codex Deliberation

### Agreements

- The scalar MVP should not solve full Liberty table pairing before fixing the current exporter mismatch.
- The first hard failure is the collapsed subtraction; margin provenance stays secondary until that is repaired.
- Preserved full-STA seq-check `constrain_value` is useful evidence and may remain a matched-case fallback, but it is not the universal scalar primary source anymore.
- The implementation should stay local to `StaCharacterTiming` and avoid generic iSTA semantic changes.

### Resolved Disagreements

- Source of scalar truth: Earlier exploration over-committed to preserved full-STA `constrain_value` as the universal fix. The chosen resolution is now to first recover the missing target-clock subtraction term from existing iSTA/exporter data, then use preserved seq snapshots only where they truly match the exported case and leave Liberty re-sampling as fallback elsewhere.

### Convergence Status

- Final Status: `converged`

## Pending User Decisions

- None at this stage. The scope is intentionally constrained to scalar setup/hold MVP and already excludes full table construction.

## Implementation Notes

### Code Style Requirements

- Implementation code and comments must NOT contain plan-specific terminology such as `AC-`, `Milestone`, `Step`, `Phase`, or similar workflow markers.
- Use descriptive STA/export domain names instead of workflow names.
- Keep any temporary diagnostics easy to locate and remove after the focused regression stabilizes.
