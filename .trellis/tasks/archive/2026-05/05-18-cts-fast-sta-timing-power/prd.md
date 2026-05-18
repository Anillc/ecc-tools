# CTS fast STA timing and power

## Goal

Build a CTS-specific fast STA timing/power module under `src/operation/iCTS/source/database/adapter/fast_sta`. The module must provide OpenSTA-style CTS timing and power calculation with incremental update support, expose stable CTS-facing adapter APIs, and become the timing/power basis for CTS characterization and later optimization.

## Requirements

- Add a new independent iCTS database adapter module at `src/operation/iCTS/source/database/adapter/fast_sta`.
- Keep the module CTS-specific. It should model the clock tree, CTS buffers, CTS routed RC segments, sink pin caps, and CTS-relevant power only.
- Define fast STA-owned data structures. Do not store raw iSTA/iDB/iPA types in fast STA data structures except in initialization/build adapters.
- Use iSTA/Liberty/iDB only as initialization-time sources for real technology/design data, including library timing/power tables, pin caps, max cap, area, voltage, routing RC, CTS clock period, and committed CTS topology.
- Follow the OpenSTA `dmp_ceff_elmore` calculation style for CTS timing:
  - detailed CTS RC tree storage,
  - downstream capacitance calculation,
  - driver-side Pi/Elmore reduction,
  - per-load Elmore delay,
  - DMP effective capacitance for cell delay/slew table lookup,
  - per-load net delay/slew propagation.
- Implement CTS power calculation without full iPA graph construction:
  - clock activity density from clock period,
  - net switching power,
  - buffer internal power from Liberty power tables,
  - buffer leakage power from Liberty leakage data,
  - area from Liberty cell area.
- Support incremental timing/power update after CTS buffer sizing changes. Recompute only affected upstream load/Pi data and downstream timing/power data needed for correct sink arrivals and power totals.
- Expose a CTS-facing adapter interface analogous to `STAAdapter`, but scoped to fast STA construction, querying, update, and report/diagnostic needs.
- Validate fast STA timing and power against OpenSTA on focused CTS-like cases before using it as the source of truth for CTS calls.
- Compare current CTS characterization results against fast STA and record the observed differences from iSTA/char timing口径.
- Replace CTS characterization timing/power calls with fast STA口径 after OpenSTA validation.
- Keep `src/operation/iCTS/source/module/buffer_sizing` algorithm work in a separate optimization task. This task may add fast STA query/update APIs needed by optimization but should not complete the optimization algorithm migration itself.
- Do not run `ecc_dev_tools` during development. Run the final full `src/operation/iCTS` check only after fast STA and optimization task nodes converge.

## Acceptance Criteria

- [ ] `src/operation/iCTS/source/database/adapter/fast_sta` exists with its own headers, sources, CMake target, and integration through `source/database/adapter/CMakeLists.txt`.
- [ ] Fast STA data structures are independent of raw iSTA/iDB/iPA runtime objects after initialization.
- [ ] The timing implementation contains OpenSTA-style RC reduction, DMP Ceff cell timing, per-load Elmore delay, and propagated load slew.
- [ ] The power implementation contains clock switching, buffer internal, leakage, and area accounting needed by CTS.
- [ ] Incremental update APIs exist for buffer master changes and affected timing/power recomputation.
- [ ] CTS-facing adapter APIs can build a fast STA clock context from committed CTS design state and query sink arrival/skew, slew/load/cap legality, area, and power.
- [ ] Focused OpenSTA alignment cases are documented with numeric comparison results for timing and power.
- [ ] CTS characterization is migrated to fast STA口径, and char-vs-iSTA/previous-char differences are summarized.
- [ ] No full iSTA or iPA graph recomputation is required for normal fast STA timing/power queries after initialization.
- [ ] The binary command `cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` runs successfully after fast STA migration checkpoints that affect CTS flow.
- [ ] Final completion waits for the child optimization task and then runs one full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.
- [ ] The fast STA task and the optimization task are committed only after both tasks pass their required checks.

## Confirmed Facts

- OpenSTA default delay calculator is `dmp_ceff_elmore`, registered in `src/sta/dcalc/DelayCalc.cc` and created in `src/sta/search/Sta.cc`.
- OpenSTA reduces detailed parasitics to `PiElmore` while retaining per-load Elmore values.
- OpenSTA `dmp_ceff_elmore` uses DMP effective capacitance for cell delay/slew lookup instead of raw total load capacitance.
- iSTA ordinary Elmore propagation currently uses total root load capacitance for Liberty cell delay/slew lookup, per-load Elmore for net delay, and an impulse-based slew formula.
- iPA clock power uses clock toggle `2 / period` style activity, switching power `0.5 * toggle * cap * V^2`, internal power from Liberty power tables, and leakage from Liberty leakage groups.
- Existing CTS optimization is char-backed and lives under `source/flow/optimization` plus `source/module/buffer_sizing`; migrating it to fast STA is tracked as child task `05-18-cts-optimization-fast-sta-migration`.

## Out of Scope

- Full-chip STA replacement.
- Current-source, CCS, Arnoldi, PRIMA, SI, OCV/AOCV/POCV, multi-corner, or non-clock data-path analysis.
- iPA graph replacement outside CTS clock-tree power needs.
- Topology-changing optimization such as adding or deleting buffers.
- Broad iSTA/iPA refactors beyond narrow data extraction helpers needed during fast STA initialization.

## Open Questions

- None blocking planning. The initial implementation should use OpenSTA `dmp_ceff_elmore` semantics as the required timing target and keep unsupported advanced models out of scope.
