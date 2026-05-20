# Implementation Plan: CTS Code Normalization Refactor

## Status

This parent task is `in_progress`.

The parent plan was reviewed and implementation proceeded through child tasks. All 9 child tasks are currently marked completed. Final
`ecc_dev_tools` validation is clean. The parent is not archived or committed.

## Progress Model

- The parent owns the architecture map, stage responsibility model, task tree, naming rules, and final integration criteria.
- Child tasks own concrete implementation slices and are checked independently before completion.
- Source cleanup remains the primary acceptance surface; test helper cleanup follows source boundaries.
- A runtime issue discovered during validation was split into a separate child task and completed before returning to this parent.

## Completed Child Task Checklist

- [x] `.trellis/tasks/05-19-cts-stage-responsibilities-flow-lifecycle`
  - Flow stage responsibilities were clarified before lifecycle conventions.
  - Root-flow helper names now match CTS stage behavior.
  - Validation recorded in child PRD: `icts_source_flow`, `icts_test_flow`, `./bin/icts_test_flow`, and whitespace check.
- [x] `.trellis/tasks/05-19-cts-clock-data-read-boundary`
  - Clock-data read ownership moved to `source/flow/setup/clock_data`.
  - Instantiation remains responsible for post-synthesis materialization/writeback.
  - Validation recorded in child PRD: setup/instantiation flow targets, clock/SDC flow tests, full flow test, and whitespace check.
- [x] `.trellis/tasks/05-19-cts-faststa-internal-split-target-boundaries`
  - FastSTA remained under `database/adapter/fast_sta`.
  - `FastSta.hh/.cc` remains the external facade.
  - Internals were split by CTS timing, Liberty, clock-tree, clock-net parasitic, power, edit, characterization, and report concepts.
- [x] `.trellis/tasks/05-19-cts-faststa-facade-narrowing`
  - Production callers were migrated away from broad mutable FastSTA clock-context layout access.
  - FastSTA facade APIs now expose CTS timing, route geometry, legality, power, and clock-sizing edit operations.
- [x] `.trellis/tasks/05-19-cts-module-singleton-boundary-cleanup`
  - Touched module APIs now express required CTS data/options at the boundary.
  - Runtime singleton and adapter reads were moved toward flow/database ownership for touched paths.
- [x] `.trellis/tasks/05-19-cts-semantic-source-naming-cleanup`
  - Source files/types were renamed by CTS object/action.
  - Child evidence records affected target builds, selected ctests, iCTS test enumeration, source name scans, and whitespace check.
- [x] `.trellis/tasks/05-19-cts-large-header-concept-split`
  - Broad source headers were split by stable CTS concepts.
  - Child evidence records H-tree/characterization/FastSTA/optimization builds, analytical solver ctest, source scans, and whitespace check.
- [x] `.trellis/tasks/05-19-cts-test-helper-semantic-cleanup`
  - Test helper names now use domain/role terms after source cleanup.
  - Child evidence records core/additional CTS test builds, full built iCTS test set `15/15`, generic path/name scans, and whitespace check.
  - Parent follow-up cleaned four residual FlowTest case names that still used rollback wording:
    `RootBufferInsertionFailureRestoresClockMembershipAndRecordsSinkDomainStatus`,
    `DownstreamNetCreationFailureRestoresClockMembershipAndRecordsSinkDomainStatus`,
    `TopologyResetRestoresPreparedSinkDomainAndKeepsPendingClockLayoutUnmerged`, and
    `SourceToRootFailureRestoresPreparedSinkDomainsAndRecordsStatus`.
- [x] `.trellis/tasks/05-20-cts-fanout4-optimization-runtime`
  - Exact full-power clock-sizing now stops after target skew is met.
  - Focused build passed, full built iCTS ctest passed `15/15`, and `ics55_dev` passed for fanout 4 and fanout 32.

## Current Read-Only Closure Scans

Commands run after returning to this parent:

```bash
rg -n "\b(snapshot|Snapshot|rollback|Rollback|fallback|Fallback|Internal|Support|Request|Response|Session)\b" \
  src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh' -g 'CMakeLists.txt'
rg -n "RollsBack|RollBack|rollback|Rollback|fallBack|fallback|Fallback" \
  src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh' -g 'CMakeLists.txt'
rg -n "Snapshot|snapshot" src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh' -g 'CMakeLists.txt'
find src/operation/iCTS/source src/operation/iCTS/test \
  \( -path '*Snapshot*' -o -path '*snapshot*' -o -path '*Internal*' -o -path '*Support*' -o -path '*Request*' \
  -o -path '*Response*' -o -path '*Types*' -o -path '*Session*' -o -path '*Fallback*' -o -path '*Rollback*' \
  -o -path '*RollBack*' -o -path '*RollsBack*' -o -path '*Network*' \) -print | sort
find src/operation/iCTS/source -type f \( -name '*.cc' -o -name '*.hh' \) -print0 | xargs -0 wc -l | sort -nr | sed -n '1,40p'
find src/operation/iCTS/test -type f \( -name '*.cc' -o -name '*.hh' \) -print0 | xargs -0 wc -l | sort -nr | sed -n '1,40p'
```

Results:

- No textual matches were found for the forbidden generic source/test terms listed above.
- No textual matches were found for rollback/fallback spelling variants after the parent follow-up test-name cleanup.
- No textual matches were found for copied-state naming variants.
- Generic path scan found only `source/database/design/ClockNetwork.hh/.cc`; this is allowed by user confirmation because it matches the database
  clock-network concept.
- `Network` also appears as a local LEMON min-cost-flow graph variable in `module/topology/mcf/MinCostFlow.hh`; it is not a CTS clock-net/domain
  name.
- Source line-count scan: 343 source `.cc/.hh` files, 59,328 total source lines, no source file above 600 lines.
- Test line-count scan: 108 test `.cc/.hh` files, 20,722 total test lines. `FastSTATest.cc` is 664 lines and remains a test-side follow-up risk,
  not a source-priority blocker for this parent.

## Final Integration Validation State

Completed:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization iEDA
ctest --test-dir build --output-on-failure -R '^icts_test_'
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Recorded outcomes:

- Focused optimization/FastSTA/iEDA build passed in the fanout runtime child.
- Full built iCTS test set passed: `15/15`.
- Parent follow-up test-name cleanup passed `ninja -C build icts_test_flow` and `./bin/icts_test_flow --gtest_color=no` with `26/26` tests.
- `ics55_dev` fanout 4 passed with optimized skew `0.0746 ns`, exact trials `3`, optimization elapsed `0.625 s`, command real time `38.70 s`.
- `ics55_dev` fanout 32 passed with optimized skew `0.0689 ns`, exact trials `170`, optimization elapsed `3.168 s`, command real time `42.26 s`.
- Initial final checker run found 26 in-scope issues: 22 formatting findings, 3 FastSTA CMake link-visibility findings, and 1 FastSTA IWYU
  finding.
- The final checker rerun passed with `0` in-scope findings. Out-of-scope diagnostics remained in external/non-iCTS headers and are not part of
  this task's acceptance surface.

Spec sync:

- `.trellis/spec/backend/quality-guidelines.md` now records the CTS semantic naming convention, including the copied-state ban, generic
  structural-name ban, the `ClockNetwork` exception, and the valid `internal_power` / root-input domain-term exceptions.

## Remaining Finish Gate

When the user wants to finish/archive this parent task:

1. Commit only if the user explicitly asks for a commit.
2. Archive the parent task only after the intended commit/branch workflow is confirmed.
