# CTS fast STA timing and power implementation plan

## Phase 1: Structure

- [x] Add `src/operation/iCTS/source/database/adapter/fast_sta`.
- [x] Add `icts_source_database_adapter_fast_sta` CMake target and link it through `icts_source_database_adapter`.
- [x] Add public facade/header skeletons with required file headers and no raw iSTA/iDB/iPA exposure in public fast STA types.
- [x] Build the touched target after CMake wiring before algorithm implementation.

## Phase 2: Initialization Data

- [x] Implement `FastStaLiberty` snapshots for CTS buffer masters and sink pins:
  - pin caps,
  - max cap,
  - delay/slew tables,
  - internal power tables,
  - leakage,
  - area,
  - voltage.
- [x] Implement builder extraction from current `STAAdapter`/iSTA/iDB APIs with narrow helper functions.
- [x] Implement clock context construction from committed CTS `Design` and `Clock`.
- [x] Represent CTS clock tree nodes, nets, terminal pins, routed segments, and selected buffer masters in fast STA-owned IDs.

## Phase 3: Timing Core

- [x] Implement detailed CTS RC storage and per-driver RC view.
- [x] Implement downstream capacitance calculation.
- [x] Implement OpenSTA-style Pi/Elmore reduction.
- [x] Implement per-load Elmore delay calculation.
- [x] Implement DMP Ceff solver for reduced Pi load.
- [x] Implement Liberty delay/slew lookup using `(input_slew, ceff)`.
- [x] Implement per-load net delay/load slew propagation.
- [x] Implement levelized full-clock timing propagation and skew summaries.

## Phase 4: Power Core

- [x] Implement clock activity density from period.
- [x] Implement net switching power.
- [x] Implement buffer internal power from Liberty power tables.
- [x] Implement leakage and area accounting.
- [x] Add power summary query APIs.

## Phase 5: Incremental Update

- [x] Implement buffer master change API.
- [x] Update affected terminal input cap and area/power metadata.
- [x] Dirty upstream net load/Pi/Ceff data through affected ancestors.
- [x] Dirty downstream timing and power through affected subtree.
- [x] Validate incremental recomputation against full recomputation on focused cases.

## Phase 6: OpenSTA Alignment

- [x] Build focused CTS-like validation cases against OpenSTA source behavior.
- [x] Compare Pi values, Elmore values, cell delay/slew, load delay/slew, sink arrival/skew, and power terms.
- [x] Record numeric comparison results in a task report artifact.
- [x] Fix mismatches before using fast STA at CTS call sites.

## Phase 7: CTS Adapter and Char Migration

- [x] Expose stable `FastStaAdapter` query/update APIs for CTS flow/module callers.
- [x] Replace CTS characterization timing/power calls with fast STA口径.
- [x] Produce char-vs-fast-STA and char-vs-iSTA comparison notes.
- [x] Run the binary command after migration:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Phase 8: Handoff to Optimization Child Task

- [x] Confirm fast STA APIs support optimization needs:
  - current sink arrivals and skew,
  - candidate buffer master changes,
  - cap legality,
  - incremental accept/reject updates,
  - area and power summaries.
- [ ] Start child task `05-18-cts-optimization-fast-sta-migration` only after fast STA validation is sufficient.

## Final Validation Gate

- [x] Do not run `ecc_dev_tools` during normal development.
- [ ] After fast STA and optimization child task converge, run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

- [ ] Fix all in-scope findings and rerun the same full check until clean.
- [ ] Commit fast STA and optimization task changes only after the final check passes.

## Risk Points

- OpenSTA DMP Ceff behavior is the critical alignment risk. Implement it as a dedicated component and validate before integrating with flow.
- Unit handling must be explicit. Keep internal units documented in `FastStaTypes.hh` and convert only at adapter boundaries.
- Incremental invalidation must be proven against full recomputation before optimization relies on it.
- Avoid moving iSTA/iPA raw types across the fast STA facade; doing so would make later optimization dependent on external runtime graph state.
