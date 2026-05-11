# Implementation Checklist

## Planning

- [x] Create Trellis task for HTree demand-driven frontier refactor.
- [x] Inspect current HTree, segment pruning, topology pruning, source trunk, and focused tests.
- [x] Document semantic frontier types, consumer matrix, and refactor direction.
- [x] Wait for or create a clean commit boundary for the predecessor runtime optimization task before editing production code for this task.
- [x] Confirm user-approved MVP: first refactor request/catalog API and keep downstream HTree full request; do not enable default HTree demand narrowing in the first slice.
- [x] Confirm validation rule: do not run `ecc_dev_tools` during the normal implementation loop.

## Refactor Slice 1: API Decoupling

- [x] Add `SegmentFrontierKind`, kind-set helpers, `SegmentFrontierRequest`, and `SegmentFrontierCatalog` in the HTree segment-pruning module.
- [x] Implement `SynthesizeSegmentFrontiers(...)` as the only general public synthesis API.
- [x] Keep a full request profile that preserves old full all/branch/leaf behavior.
- [x] Migrate `SourceTrunkSegment::build` to request `all` through the catalog.
- [x] Migrate downstream `HTree::build` and `TopologyPruning` to consume frontiers through catalog accessors.
- [x] Remove the public source-trunk-specific `SynthesizeSegmentAllFrontierEntrySets(...)` wrapper.
- [x] Move segment frontier entry counting into catalog helpers.

## Refactor Slice 2: HTree Demand Narrowing

- [x] Add a demand planner that derives HTree required semantic kinds from boundary constraints.
- [x] Add or update tests proving unrestricted HTree can request `all` and branch-forced HTree requests `all | terminal_branch_buffered`.
- [x] Enable narrowed HTree requests only after equivalence checks are in place.
- [x] Decide whether internal pattern ID drift is acceptable for narrowed HTree requests; if accepted, update comparisons to use materialized-pattern equivalence instead of raw IDs.

## Readability Cleanup

- [x] Extract selected pattern decoration from `HTree::build`.
- [ ] Extract HTree build orchestration helpers without changing the flow architecture.
- [x] Fix stale file comments around segment frontier/library files.
- [ ] Consider extracting shared delay/power Pareto helpers if the local change remains small.
- [ ] Defer BuildResult ownership restructuring unless it is necessary for the request API.

## Validation

- [x] `git diff --check`
- [x] `cmake --build build --target icts_test_flow_synthesis_htree -j $(nproc)`
- [x] `./bin/icts_test_flow_synthesis_htree`
- [x] `cmake --build build --target icts_test_flow_synthesis_htree_realtech -j $(nproc)` if realtech assets are available.
- [x] `./bin/icts_test_flow_synthesis_htree_realtech` if realtech assets are available.
- [x] `cmake --build build --target iEDA -j $(nproc)`
- [x] If narrowed default HTree demand is enabled, rerun the `ics55_dev` target command and compare runtime/QoR against the predecessor task benchmark.
- [x] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` only during finish-work/final quality gate if requested; do not run during the process.

## Review Gates

- Do not mix this refactor with uncommitted predecessor runtime-optimization code unless the user explicitly asks to fold the tasks together.
- Do not change default HTree semantic demand in the same patch that only introduces the catalog API unless tests prove exact selected physical equivalence.
- Do not remove `terminal_leaf_unbuffered` support; remove only default construction when no request needs it.
- Stop if branch-forced HTree no longer materializes terminal branch buffers on every selected level.
