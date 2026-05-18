# CTS optimization fast STA migration implementation plan

## Preconditions

- [x] Parent fast STA task has validated query/update APIs.
- [x] Parent fast STA task can build a clock context for committed CTS design state.
- [x] Parent fast STA task can update timing after a buffer master change without full iSTA/iPA recomputation.

## Phase 1: Integration

- [x] Add `database/adapter/fast_sta` dependency to optimization/module targets as needed.
- [x] Replace char lookup timing inputs in `flow/optimization` and `module/buffer_sizing` with fast STA context IDs and query/update calls.
- [x] Remove or isolate `CharTimingLookup` usage from optimization move evaluation.
- [x] Keep characterization migration owned by the parent task separate from optimization policy.

## Phase 2: Candidate Evaluation

- [x] Generate legal buffer master candidates for existing buffer nodes.
- [x] Apply each candidate through fast STA buffer-master update APIs.
- [x] Query fast STA skew, cap legality, area, and runtime-relevant summaries after each trial or batch.
- [x] Reject candidate states that introduce new cap violations.
- [x] Restore rejected states through fast STA-supported rollback or by applying the previous master and updating incrementally.

## Phase 3: Objective Handling

- [x] Implement target-skew semantics:
  - minimum area among legal states that satisfy target,
  - otherwise minimum skew spread.
- [x] Preserve fixed topology.
- [x] Keep area as the primary cost metric.
- [x] Avoid any self-computed timing formulas in optimization code.

## Phase 4: Reporting

- [x] Log compact run summary:
  - runtime,
  - target skew,
  - initial skew,
  - optimized skew,
  - area delta,
  - cap legality,
  - changed master distribution.
- [x] Remove temporary debug path traces before final handoff unless still needed for an active investigation.

## Phase 5: Binary Validation

- [x] Run:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- [x] Repeat with target skew set to 80ps.
- [x] Repeat with target skew set to 40ps.
- [x] Repeat with target skew set to 0ps.
- [x] Record results in a task report artifact.

## Final Gate

- [x] Do not run `ecc_dev_tools` during normal development.
- [ ] Parent task owns final full `src/operation/iCTS` ecc dev check after this task converges.
- [ ] Commit this task together with the parent fast STA task only after final check passes.
