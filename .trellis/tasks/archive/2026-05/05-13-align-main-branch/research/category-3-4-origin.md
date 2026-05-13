# Category 3/4 Origin Analysis

## Scope

This note tracks the source of the category 3 and 4 diffs that should be aligned back to `main` before preparing a selective CTS refactor sync.

Category 3:

- iMP, py_imp, py_idb formatting, python CMake whitespace, iRT congestion Tcl, sky130 `rt.lyp`, congestion/evaluation formatting, iIR, iPL include path, operation-level CMake, global CMake/build/rust CMake, parser Rust CMake, and iSTA test/debug/report-only changes.

Category 4:

- `src/database/data/design/db_design/IdbInstance.*`
- non-CTS `TimingIDBAdapter::substituteCell` / DB sync changes.

## Key Findings

- Current `main` is the merge base/ancestor of `cts_refactor`, so remaining diffs are branch-local deviations that survived the previous main-to-CTS merges.
- Most category 3 and 4 deviations became current `main`-relative diffs during `e18c8ce8f` on 2026-05-13 (`merge: bring main into cts_refactor`). Comparing the merge result to its `main` parent shows 65 category 3/4 files still differed from main.
- `364215d55` on 2026-05-13 (`merge: bring latest main into cts_refactor`) did not materially introduce category 3/4 scope; in this area it only added/kept iSTA test files. The earlier `e18c8ce8f` merge is the point where these differences remained unresolved against main.

## Root Causes by Group

### Global build and Rust CMake

- `bbd9f73cf` on 2025-07-11 introduced `BUILD_AIEDA` handling in the top-level CMake (`add definition for AiEDA in top CMakelist`).
- `d97347792` on 2025-10-22 introduced the `build.sh -p` AIEDA build option (`build: add AIEDA build.sh option`).
- `cde093059` on 2026-01-23 renamed the Python target from `ieda_py` to `ecc_py`.
- Parser Rust CMake behavior traces back to older Rust integration commits such as `46697299f` on 2024-03-29 and related per-parser CMake code; these were later preserved instead of aligning to current `main`.

Reason: these are historical AI-EDA / iPD build-system choices, not CTS refactor requirements.

### py_imp / iSTA tests / category 4 DB sync

- `235085052` on 2026-04-12 introduced the main chunk:
  `fix: iSTA, iDB, and parser (copied from the feature/admm-gate-sizing branch of iPD)`.
- That commit added py_imp OpenROAD/export helpers and tests, added iSTA tests, and changed iDB/STA sync logic:
  - `IdbInstance::swap_cell_master`
  - `TimingIDBAdapter::substituteCell` syncing DB master, STA pins, and timing arcs.

Reason: this was copied from an ADMM/gate-sizing branch to support gate-sizing / OpenROAD-style export and related iSTA/iDB/parser behavior. It is not directly required by the CTS refactor runtime.

### iMP, iIR, iRT/eval/iPL, and operation-level CMake

- These files have long historical origins from iMP/iIR/eval/iPL development, but the current main-relative differences were preserved by the 2026-05-13 merge result (`e18c8ce8f`) rather than introduced as CTS work.
- The category 3 restore should therefore treat these as unrelated module drift and align them to `main`.

Reason: they are non-CTS module/build/test changes and no direct iCTS dependency was found in the current refactor flow.

### iSTA report/debug/test-only subset

- A large part of the iSTA test/debug/report-only diff was also preserved by `e18c8ce8f`.
- Some tests and DB-to-netlist checks came from `235085052`.
- The latest merge `364215d55` added/kept a small set of iSTA test additions, but not production CTS-required semantics.

Reason: these are verification/debug/report local changes or branch drift. Required iCTS-facing iSTA semantics, such as char timing and SDC clock-only reading, are outside this restore scope and should be preserved.

## Implementation Implication

- Restore category 3 and 4 paths to `main` in a scoped way.
- Preserve iCTS-required iSTA/liberty/iPA semantics:
  - `TimingEngine::prepareCharTiming`
  - `TimingEngine::updateCharTiming`
  - `TimingEngine::readSdcClockPeriodsOnly`
  - liberty unit conversion / internal power semantics
  - iPA power unit alignment
- After cleanup, compare `main..cts_refactor` and run the iCTS dev script before/after to verify no behavior drift.
