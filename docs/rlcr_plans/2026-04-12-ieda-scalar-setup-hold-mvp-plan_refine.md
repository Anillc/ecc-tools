# iEDA Scalar Setup-Hold MVP Refined Plan

## Goal Description

Refine the current scalar setup/hold export path in iEDA so that exported setup and hold constraints consume the preserved full-STA scalar `constrain_value` directly instead of being dominated by exporter-side Liberty re-sampling. The work must stay local to the ETM export path in `StaCharacterTiming`, preserve the current characterization-side data term, keep CPPR disabled for timing-model export semantics, and avoid reopening the previously rolled-back clock reconstruction branch.

## Acceptance Criteria

Following TDD philosophy, each criterion includes positive and negative tests for deterministic verification.

- AC-1: The scalar setup/hold exporter uses preserved full-STA seq-check data as the primary source of `check_margin`.
  - Positive Tests (expected to PASS):
    - The setup/hold export path in `StaCharacterTiming.cc` resolves a preserved seq-check snapshot for the golden regression endpoint and reads its `constrain_value`.
    - Trace or temporary diagnostics show the golden endpoint taking the preserved full-STA scalar path instead of the exporter-local Liberty re-sampling path.
  - Negative Tests (expected to FAIL):
    - An implementation that still computes the golden scalar `check_margin` primarily through `getDelayOrConstrainCheckNs(...)` is rejected.
    - An implementation that uses full-STA `getArriveTime()` or `getRequireTime()` snapshots as the main exported scalar formula inputs is rejected.

- AC-2: The focused OpenROAD-alignment regression passes with the scalar setup/hold values driven by the preserved full-STA margin.
  - Positive Tests (expected to PASS):
    - `LibertyAlignmentTest.setup_hold_constraint_values_track_openroad_reference_scale` passes after the exporter path is updated.
    - The generated Liberty for the golden case shows scalar `setup_rising`, `setup_falling`, `hold_rising`, and `hold_falling` values aligned with the preserved full-STA scalar margin path.
  - Negative Tests (expected to FAIL):
    - A build that still emits the previously observed mismatched values such as `483.14308 ps` / `-355.85882 ps` for the golden regression remains failing.
    - A build that passes only because of hard-coded pin names, library names, or benchmark-specific branching is rejected.

- AC-3: The MVP stays narrowly scoped to scalar export and does not mutate generic iSTA semantics.
  - Positive Tests (expected to PASS):
    - Code changes remain concentrated in `StaCharacterTiming.cc` and any minimal supporting declarations in `StaCharacterTiming.hh`, with test-only changes in the current alignment tests if needed.
    - The implementation keeps the old Liberty re-sampling path as an explicit fallback for unmatched cases rather than deleting it outright.
  - Negative Tests (expected to FAIL):
    - Any change that introduces new generic propagation states, rewrites core graph/STA propagation semantics, or reopens exporter-local clock reconstruction is rejected.
    - Any change that hard-codes ASAP7-specific table names, units, template names, or benchmark pin behavior is rejected.

- AC-4: Unit handling and fallback behavior stay explicit and debuggable during the MVP.
  - Positive Tests (expected to PASS):
    - The scalar primary path does not depend on second-axis pre-conversion or exporter-side table re-sampling.
    - Temporary diagnostics, if added, clearly show match success, selected preserved `constrain_value`, and fallback activation.
  - Negative Tests (expected to FAIL):
    - A scalar primary path that still routes through risky pre-converted second-axis table sampling is rejected.
    - A fallback path that silently handles the golden endpoint without observability is rejected.

## Path Boundaries

Path boundaries define the acceptable range of implementation quality and choices.

### Upper Bound (Maximum Acceptable Scope)

The implementation introduces one local selector in `StaCharacterTiming` that matches exported scalar setup/hold arcs to preserved full-STA seq-check records using stable existing identities such as endpoint vertex, check arc, analysis mode, transition type, and one extra data-path discriminator only if truly needed. The exporter consumes preserved `constrain_value` as the primary scalar `check_margin`, keeps a clearly isolated fallback path, adds short-lived diagnostics to prove the golden endpoint uses the primary path, and updates the focused regression to make future regressions obvious.

### Lower Bound (Minimum Acceptable Scope)

The implementation changes only the scalar setup/hold export path needed for the golden regression so that the exported `check_margin` comes from preserved full-STA `constrain_value` for the matching endpoint/check case. The focused regression passes, and no generic iSTA behavior is modified.

### Allowed Choices

- Can use:
  - Existing preserved seq-check snapshot data in `StaCharacterTiming`
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
2. In `StaCharacterTiming`, find the point where exported scalar setup/hold values currently decide `check_margin`.
3. Insert a small selector that looks up the matching preserved seq-check snapshot for the current endpoint and timing arc.
4. Read `constrain_value` from that preserved record and use it as the scalar `check_margin`.
5. Keep the existing Liberty re-sample path behind a fallback branch only for unmatched cases.
6. Add minimal diagnostics to verify the golden endpoint is no longer taking fallback.
7. Rebuild `iSTATest`, rerun the focused regression, and inspect the generated Liberty once.

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

2. Primary Margin Routing
   - Phase A: Add a minimal preserved seq-check selector in `StaCharacterTiming`.
   - Phase B: Route scalar `check_margin` through preserved `constrain_value` first and leave Liberty re-sampling as fallback only.

3. Guardrails and Verification
   - Phase A: Add short-lived diagnostics around match selection and fallback activation.
   - Phase B: Rebuild `iSTATest`, rerun the focused regression, inspect generated Liberty output, and remove any excess debug noise.

The baseline regression must exist before the exporter change is trusted. The selector and primary routing must land before fallback diagnostics are meaningful. Verification is last and must use fresh build and test evidence.

## Task Breakdown

Each task must include exactly one routing tag:
- `coding`: implemented by Claude
- `analyze`: executed via Codex (`/humanize:ask-codex`)

| Task ID | Description | Target AC | Tag (`coding`/`analyze`) | Depends On |
|---------|-------------|-----------|----------------------------|------------|
| task1 | Confirm the focused scalar setup/hold regression is the active failing baseline and improve failure visibility only if needed | AC-2 | coding | - |
| task2 | Identify the exact scalar `check_margin` decision point in `StaCharacterTiming.cc` and map it to preserved seq-check snapshot fields | AC-1 | analyze | task1 |
| task3 | Implement the minimal selector that matches exported scalar setup/hold arcs to preserved full-STA seq-check records | AC-1, AC-3 | coding | task2 |
| task4 | Route scalar `check_margin` through preserved `constrain_value` first and keep Liberty re-sampling as fallback only | AC-1, AC-3, AC-4 | coding | task3 |
| task5 | Add or tighten short-lived diagnostics so fallback activation and preserved-margin selection are visible during the golden regression | AC-4 | coding | task4 |
| task6 | Rebuild `iSTATest`, rerun the focused regression, inspect the generated Liberty once, and remove excess debug noise if present | AC-2, AC-4 | coding | task5 |

## Claude-Codex Deliberation

### Agreements

- The scalar MVP should not solve full Liberty table pairing before fixing the current exporter mismatch.
- The preserved full-STA seq-check `constrain_value` is the right primary scalar source to try first.
- The implementation should stay local to `StaCharacterTiming` and avoid generic iSTA semantic changes.

### Resolved Disagreements

- Source of scalar truth: Earlier exploration kept drifting toward exporter-local Liberty re-sampling or direct reuse of full-STA arrival snapshots. The chosen resolution is to keep the current characterization-side data term and switch only the scalar `check_margin` source to preserved full-STA `constrain_value`.

### Convergence Status

- Final Status: `converged`

## Pending User Decisions

- None at this stage. The scope is intentionally constrained to scalar setup/hold MVP and already excludes full table construction.

## Implementation Notes

### Code Style Requirements

- Implementation code and comments must NOT contain plan-specific terminology such as `AC-`, `Milestone`, `Step`, `Phase`, or similar workflow markers.
- Use descriptive STA/export domain names instead of workflow names.
- Keep any temporary diagnostics easy to locate and remove after the focused regression stabilizes.
