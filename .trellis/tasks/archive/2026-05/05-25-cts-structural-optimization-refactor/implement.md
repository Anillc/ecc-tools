# CTS structural optimization refactor implementation

## Gate

`05-24-cts-contract-polish-convergence` is complete, validated, and archived. This task now implements the deeper structural cleanup from the
post-convergence codebase.

## Pre-Implementation Checklist

- [x] Confirm `05-24-cts-contract-polish-convergence` is complete.
- [x] Read the final short-term contract-polish state and active task docs.
- [x] Re-run a fresh audit of current code structure.
- [x] Update `design.md` with concrete post-cleanup findings.
- [x] Decide whether this task needs child tasks: not yet; implement the first structural phase in this task, then reassess.

## Expected Phases

### Phase 1: HTree Build/Diagnostics Boundary

- [x] Change production `HTreeBuild` to contain only `HTreeOutput` and `HTreeSummary`.
- [x] Add `HTreeDiagnosticBuild` and `HTree::buildWithDiagnostics`.
- [x] Move HTree implementation body behind a short-lived internal `HTreeBuilder`.
- [x] Update HTree internals that need diagnostics to use `HTreeDiagnosticBuild`.
- [x] Keep topology production callers on `HTree::build`.
- [x] Update HTree tests/experiments/visualization helpers to use `buildWithDiagnostics`.
- [x] Verify no production topology caller reads HTree diagnostics.

Checkpoint:

```bash
ninja -C build icts_source_flow icts_test_flow_synthesis_htree icts_test_flow_synthesis
ctest --test-dir build -R '^icts_test_.*htree|icts_test_flow_synthesis' --output-on-failure
```

### Phase 2: Synthesis Per-Clock Readability

- [x] Refactor the local `synthesizeClock(...)` plumbing in `Synthesis.cc` if Phase 1 leaves builds stable.
- [x] Keep public `SynthesisInput` unchanged.
- [x] Use CTS-domain names for clock validation, sink-domain preparation, topology formation, and layout merge.
- [x] Keep summary aggregation visible at `Synthesis::run`.

Checkpoint:

```bash
ninja -C build icts_source_flow icts_test_flow icts_test_flow_synthesis
```

### Phase 3: Topology Commit/Naming Review

- [x] Review `ClockTopologyFormation` after Phases 1-2.
- [x] Preserve build-before-commit behavior.
- [x] Keep `DesignConversion::commitInsertedObjects` as the explicit mutation boundary.
- [x] Apply only narrow renames/extractions if they materially improve CTS semantics.

### Phase 4: Test And Validate

- [x] Run targeted iCTS builds after each phase.
- [x] Run representative `icts_test_*` suites.
- [x] Run real `ics55_dev` flow.
- [x] Run final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.

Final validation evidence:

```bash
ninja -C build icts_source_flow icts_test_flow icts_test_flow_synthesis icts_test_flow_synthesis_htree icts_test_flow_synthesis_htree_analytical_solver
ctest --test-dir build -R '^icts_test_' --output-on-failure
ninja -C build iEDA
cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Results: targeted build passed, all 15 `icts_test_*` tests passed, `ics55_dev` reported `iCTS run successfully.`, and final
`ecc_dev_tools` reported 0 in-scope findings.

## Rollback Points

- Revert individual structural phases if build payload or commit semantics become ambiguous.
- Do not keep a partially introduced long-lived builder/service if it creates unclear lifetime ownership.
- If report output changes unexpectedly, stop and decide whether the report change is acceptable or should be compatibility-preserved.
