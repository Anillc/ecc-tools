# Implementation Plan: CTS optimization critical-branch char sizing

## Phase 0: Baseline Cleanup

- [x] Create local backup ref for old unpushed optimization work.
- [x] Reset active branch to `origin/cts_refactor`.
- [x] Confirm old LCB/full-tree optimization code is absent from active branch.
- [ ] Remove conflicting active old optimization task directories.

## Phase 1: Shared Char Lifetime

- [ ] Add `CharacterizationLibrary` member to `Flow`.
- [ ] Change `Synthesis::run` to accept a shared `CharacterizationLibrary&`.
- [ ] Remove per-clock local char-library ownership where it blocks reuse.
- [ ] Pass the shared char library to `Optimization::run`.

## Phase 2: Graph Utility

- [ ] Add `src/operation/iCTS/source/utils/graph`.
- [ ] Implement `RootedTreeLCA`.
- [ ] Add CMake wiring.
- [ ] Add tests for LCA and path extraction.

## Phase 3: Buffer Sizing Module

- [ ] Add `src/operation/iCTS/source/module/buffer_sizing`.
- [ ] Implement `CharTimingLookup`.
- [ ] Implement explicit tree problem data types.
- [ ] Implement assignment evaluation.
- [ ] Implement critical-pair root-to-sink solver.
- [ ] Add CMake wiring.
- [ ] Add module tests.

## Phase 4: Optimization Flow

- [ ] Add `src/operation/iCTS/source/flow/optimization`.
- [ ] Build per-clock tree problems from `ClockDAG`, design objects, STAAdapter data, and `ClockLayout`.
- [ ] Build/reuse char lookup from shared `CharacterizationLibrary`.
- [ ] Apply accepted sizing decisions.
- [ ] Emit concise summary and transition distribution logs.
- [ ] Add flow CMake wiring.

## Phase 5: Validation

- [ ] Build and run targeted graph tests.
- [ ] Build and run targeted buffer sizing tests.
- [ ] Build and run flow optimization tests.
- [ ] Build and run broader iCTS flow tests.
- [ ] Build `iEDA`.
- [ ] Run real design iCTS smoke.
- [ ] Run final full ecc dev check:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Rollback Points

- After Phase 1 shared char changes.
- After Phase 2 graph utility.
- After Phase 3 pure module tests.
- Before applying flow-side design mutation.

## Notes

- Do not run `ecc_dev_tools` before Phase 5 final check.
- Do not commit before user review.
