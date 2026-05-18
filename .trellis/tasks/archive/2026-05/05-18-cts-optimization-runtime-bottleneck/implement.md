# Implementation Plan: CTS optimization runtime bottleneck

## Phase 1: Task Setup and Evidence

- [x] Create Trellis task as a child of the fast STA batch-sizing task.
- [x] Record huge-case reproduction evidence from the interrupted 80ps run.
- [x] Inspect optimization and fast STA code paths reached after `CTS Optimization Setup`.
- [x] Record initial bottleneck hypotheses without changing code.

## Phase 2: Profiling Instrumentation

- [x] Add optimization-level scoped timing around each per-clock stage in `Optimization.cc`.
- [x] Add graph/search counters to the optimization summary:
  - node count,
  - net count,
  - sink count,
  - buffer count,
  - candidate count,
  - batch trial count,
  - accepted mutation count.
- [x] Add fast STA context-build timing around:
  - clock tree build,
  - sink pin cap snapshot,
  - liberty snapshot,
  - initial timing update,
  - initial power update.
- [x] Add low-frequency solver trial progress so huge runs do not stay silent inside `solveClock(...)`.
- [x] Keep logs compact and schema-based; avoid per-trial verbose output.
- [ ] Add deeper counters for buffer input lookup scans and update pass sizes if the next task targets full timing/power update internals.

## Phase 3: Bottleneck Run

- [x] Build affected targets:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization
```

- [x] Refresh `scripts/design/ics55_dev/iEDA` or `scripts/design/ics55_huge_dev/iEDA` with the built binary as needed.
- [x] Run `ics55_dev` 80ps once to verify instrumentation and log shape.
- [x] Run `ics55_huge_dev` 80ps with separate result/log paths.
- [x] Stop early only after the dominant stage has been measured, or if the run again exceeds a practical time limit without new stage output.
- [x] Summarize measured bottleneck in `results.md`.

## Phase 4: Low-Risk Fix If Proven

- [x] Fix proven route-tree injection bottleneck by changing route-tree parasitic injection to update only the injected net load.
- [x] Preserve existing full-context timing/power update behavior at normal full update boundaries.
- [x] Rebuild affected targets.
- [x] Rerun `ics55_dev` 80ps/40ps/0ps to check QoR/runtime regression.
- [x] Rerun `ics55_huge_dev` 80ps to measure route-tree injection improvement and expose the next bottleneck.
- [x] Try and revert sequential incremental batch evaluation after measurement proved it slower on high-level dirty regions.
- [ ] Defer buffer pairing indexes or full-update redesign to the follow-up solver runtime task.

## Phase 5: Final Validation

- [x] Update `results.md` with:
  - confirmed bottleneck,
  - before/after stage runtime,
  - memory observations,
  - `ics55_dev` QoR/runtime regression matrix,
  - `ics55_huge_dev` progress result.
- [x] Run final full check:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Notes

- Do not run `ecc_dev_tools` during profiling iterations.
- Do not commit until measurement and any selected fix have converged.
- Do not continue huge 40ps/0ps pressure runs until 80ps reaches a useful optimization summary or a measured early-stop bottleneck report.

## Phase 6: Scalable Batch Solver Probe

- [x] Add a timing-only batch master-change API in `FastStaAdapter`.
- [x] Keep the exact full timing/power batch solver for small graphs.
- [x] Add a scalable solver path for large fast STA contexts:
  - score late/early frontier batch candidates cheaply,
  - exact-verify only a bounded set of candidates per iteration,
  - use timing-only fast STA updates during trial/apply/restore,
  - preserve no-new-cap and no-new-slew checks,
  - run one final power update after scalable solving.
- [x] Build affected targets plus `iEDA`.
- [x] Run `ics55_dev` 80ps sanity after the first implementation.
- [x] Run `ics55_huge_dev` 80ps with the scalable solver.
- [x] Run final full `ecc_dev_tools` check before commit.
