# CTS fast STA OpenSTA and iEDA timing/power research

## OpenSTA Timing

OpenSTA default delay calculator is `dmp_ceff_elmore`.

Reference files:

- `/home/liweiguo/project/OpenROAD/src/sta/search/Sta.cc`
- `/home/liweiguo/project/OpenROAD/src/sta/dcalc/DelayCalc.cc`
- `/home/liweiguo/project/OpenROAD/src/sta/parasitics/ReduceParasitics.cc`
- `/home/liweiguo/project/OpenROAD/src/sta/dcalc/DmpCeff.cc`
- `/home/liweiguo/project/OpenROAD/src/sta/dcalc/DmpDelayCalc.cc`
- `/home/liweiguo/project/OpenROAD/src/sta/dcalc/GraphDelayCalc.cc`

Relevant behavior:

- Detailed parasitic network is reduced to a Pi model with `c2`, `rpi`, and `c1`.
- Reduced `PiElmore` also stores per-load Elmore delays.
- Cell delay/slew uses DMP effective capacitance and Liberty table lookup with `(input_slew, ceff)`.
- Per-load net delay/slew is based on load Elmore and DMP waveform response.
- Gate delay/driver slew and wire delay/load slew are annotated separately.

## OpenSTA Power

Reference files:

- `/home/liweiguo/project/OpenROAD/src/sta/power/Power.cc`
- `/home/liweiguo/project/OpenROAD/src/sta/liberty/InternalPower.cc`
- `/home/liweiguo/project/OpenROAD/src/sta/liberty/LeakagePower.cc`

Relevant behavior:

- Clock activity density is `2 / clock period`.
- Switching power is `0.5 * load_cap * V^2 * activity_density`.
- Internal power uses Liberty power tables with slew/load axes and activity weighting.
- Leakage supports conditional leakage weighting and fallback to cell leakage.

## iEDA/iSTA Timing

Reference files:

- `src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc`
- `src/operation/iSTA/source/module/sta/StaDelayPropagation.cc`
- `src/operation/iSTA/source/module/sta/StaSlewPropagation.cc`
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc`
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc`
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc`

Relevant behavior:

- iCTS can install exact CTS RC tree into iSTA.
- iSTA ordinary Elmore path computes downstream cap, Elmore delay, load delay, and impulse.
- iSTA ordinary cell delay/slew lookup uses total root load cap, not DMP Ceff.
- iSTA ordinary net delay uses per-load Elmore.
- iSTA ordinary net slew uses an impulse formula.

## iPA Power

Reference files:

- `src/operation/iPA/source/module/ops/propagate_toggle_sp/PwrPropagateClock.cc`
- `src/operation/iPA/source/module/ops/calc_power/PwrCalcSwitchPower.cc`
- `src/operation/iPA/source/module/ops/calc_power/PwrCalcInternalPower.cc`
- `src/operation/iPA/source/module/ops/calc_power/PwrCalcLeakagePower.cc`

Relevant behavior:

- Clock network toggle uses `2 / period_ns` style activity.
- Switching power uses `0.5 * toggle * cap * V^2`.
- Internal power uses STA slew/load and Liberty internal power tables.
- Leakage sums Liberty leakage groups and applies SP to conditional leakage.

## Implication

The fast STA timing core should align with OpenSTA `dmp_ceff_elmore` rather than iSTA ordinary total-cap Elmore lookup. This is the main accuracy boundary for CTS buffer sizing because buffer sizing changes upstream input cap, downstream Ceff, driver slew, and per-load net timing.

Detailed OpenSTA/iEDA comparison refreshed on 2026-05-18 after pulling OpenROAD:

- OpenROAD commit: `5894d0639dfab272652582b4e15185b68ea8a868`
- OpenSTA submodule commit: `76c4d6df3537ccce331b5caa812196c3330ba7c4`
- OpenSTA default timing is `dmp_ceff_elmore`: detailed parasitic -> Pi/Elmore reduction -> DMP Ceff -> Liberty delay/slew lookup with `(input_slew, ceff)` -> per-load Elmore/DMP load delay and slew.
- iEDA current Elmore timing is: CTS exact RC tree -> downstream cap/Elmore/impulse -> Liberty delay/slew lookup with `(input_slew, root_total_load)` -> per-load Elmore delay -> impulse-based net slew.
- OpenSTA and iPA power semantics are conceptually close for CTS: clock activity `2 / period`, switching power `0.5 * C * V^2 * activity`, Liberty internal power weighted by activity/toggle, and Liberty leakage with condition weighting. The larger correctness gap for CTS optimization is timing, not power.

### Source-Grounded OpenSTA vs iEDA Delta

OpenSTA timing chain:

- `Sta::makeArcDelayCalc()` selects `dmp_ceff_elmore` by default (`OpenROAD/src/sta/search/Sta.cc:395`).
- `DelayCalc.cc` registers `dmp_ceff_elmore`, `dmp_ceff_two_pole`, `ccs_ceff`, `lumped_cap`, and other calculators (`OpenROAD/src/sta/dcalc/DelayCalc.cc:45`).
- Detailed parasitics are reduced through `Parasitics::reduceToPiElmore()` (`OpenROAD/src/sta/parasitics/Parasitics.cc:172`) into a Pi model plus per-load Elmore data. `ReduceToPi` computes `c2`, `rpi`, `c1` from the first three admittance moments (`OpenROAD/src/sta/parasitics/ReduceParasitics.cc:103`), and `ReduceToPiElmore` stores load Elmore values from a second DFS (`OpenROAD/src/sta/parasitics/ReduceParasitics.cc:323`).
- `GraphDelayCalc::findDriverArcDelays()` calls the arc delay calculator with input slew, load cap, parasitic, and load pin map (`OpenROAD/src/sta/dcalc/GraphDelayCalc.cc:1045`), then annotates gate delay/driver slew and per-load wire delay/load slew (`OpenROAD/src/sta/dcalc/GraphDelayCalc.cc:1174`).
- `DmpCeffDelayCalc::gateDelay()` reads the reduced Pi model, selects a DMP algorithm, computes effective capacitance, uses Liberty lookup with `(input_slew, ceff)`, then derives per-load wire delay/slew (`OpenROAD/src/sta/dcalc/DmpCeff.cc:1017`).
- `DmpCeffElmoreDelayCalc::loadDelaySlew()` uses stored load Elmore and DMP load waveform calculation, then applies threshold adjustment (`OpenROAD/src/sta/dcalc/DmpDelayCalc.cc:112`).

iEDA timing chain:

- `RcTree::updateRcTiming()` runs exact-tree downstream load, Elmore delay, load-delay moment, and impulse updates (`src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc:512`).
- `RcTree::updateLoad()` accumulates downstream cap per mode/transition (`src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc:301`).
- `RcTree::updateDelay()` sets per-node Elmore delay as parent delay plus `R * downstream_load` (`src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc:335`).
- `RcTree::updateResponse()` computes impulse as `2 * beta - delay^2` (`src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc:389`), and `RctNode::slew()` returns `sqrt(input_slew^2 + impulse)` (`src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc:129`).
- `StaDelayPropagation` looks up instance arc delay with `(input_slew, rc_net/root load)` (`src/operation/iSTA/source/module/sta/StaDelayPropagation.cc:210`) and uses `RcNet::delay()` for net arc Elmore delay (`src/operation/iSTA/source/module/sta/StaDelayPropagation.cc:273`).
- `StaSlewPropagation` looks up instance output slew with `(input_slew, rc_net/root load)` (`src/operation/iSTA/source/module/sta/StaSlewPropagation.cc:152`) and uses `RcNet::slew()` for net arc slew (`src/operation/iSTA/source/module/sta/StaSlewPropagation.cc:235`).

OpenSTA power chain:

- `Power::power()` first ensures activity and instance power, then classifies clock-network instances separately (`OpenROAD/src/sta/power/Power.cc:418`).
- Instance power is `internal + switching + leakage` (`OpenROAD/src/sta/power/Power.cc:1076`).
- Internal power uses Liberty internal power tables with STA slew and output load, then weights by activity/duty (`OpenROAD/src/sta/power/Power.cc:1106`, `OpenROAD/src/sta/power/Power.cc:1272`).
- Switching power is `0.5 * load_cap * voltage^2 * activity_density` on output pins (`OpenROAD/src/sta/power/Power.cc:1417`).
- Clock activity defaults to `2.0 / clk->period()` with clock duty from waveform (`OpenROAD/src/sta/power/Power.cc:1559`, `OpenROAD/src/sta/power/Power.cc:1582`).
- Leakage handles conditional leakage, condition duty weighting, unconditional leakage, and default cell leakage fallback (`OpenROAD/src/sta/power/Power.cc:1466`).

iPA power chain:

- Clock propagation writes clock toggle as `2 / period_ns` and SP `0.5` (`src/operation/iPA/source/module/ops/propagate_toggle_sp/PwrPropagateClock.cc:38`).
- Switching power uses driver STA net load, propagated/default toggle, driver voltage, and `c_switch_power_K = 0.5` (`src/operation/iPA/source/module/ops/calc_power/PwrCalcSwitchPower.cc:87`; `src/operation/iPA/source/module/include/PwrConfig.hh:38`).
- Internal power uses STA slew and output load to query Liberty internal power tables, converts table units to `mW * ns`, averages rise/fall energy, and multiplies by toggle (`src/operation/iPA/source/module/ops/calc_power/PwrCalcInternalPower.cc:75`, `src/operation/iPA/source/module/ops/calc_power/PwrCalcInternalPower.cc:174`, `src/operation/iPA/source/module/ops/calc_power/PwrCalcInternalPower.cc:323`).
- Leakage sums Liberty leakage groups and applies SP weighting for `when` conditions (`src/operation/iPA/source/module/ops/calc_power/PwrCalcLeakagePower.cc:38`, `src/operation/iPA/source/module/ops/calc_power/PwrCalcLeakagePower.cc:83`).

CTS fast STA implication:

- For timing, OpenSTA and iEDA are not numerically equivalent on routed CTS nets. OpenSTA moves cell delay/slew to an effective capacitance derived from the RC Pi, then computes load arrival/slew from DMP/Elmore. iEDA ordinary timing looks up cell delay/slew against root total load and then appends Elmore/impulse net timing. Buffer sizing changes exactly the quantities where these models diverge.
- For power, the CTS-relevant formulas are close enough to use one fast-STA-owned implementation, as long as the timing-derived slew/load and activity units match the selected timing model.

## Implementation Notes

Route-source boundary:

- `Net` does not own committed routed topology.
- `Router::buildClockNetTree(const Net&)` can regenerate a `ClockSteinerTree` from committed driver/load locations, but `Router` lives under `source/module/routing`.
- `database/adapter/fast_sta` should not link to routing-module algorithm targets. It may consume stable database routing types and database-owned `ClockLayout` routed segments at the adapter boundary.
- Flow/module code that needs rebuilt route trees should do that before entering fast STA, then pass database-owned topology/segments into the adapter.

Focused validation run on 2026-05-18:

- `cmake --build build --target icts_test_database_adapter_fast_sta -j 8`
- `./bin/icts_test_database_adapter_fast_sta`
- `cmake --build build --target lib/libicts_source_database_adapter_fast_sta.a -j 8`

The focused unit test covers Liberty table interpolation, bounded slew-sensitive Ceff approximation, Pi/Elmore reduction, levelized timing/power, and buffer master update propagation on synthetic fast STA-owned data. Full OpenSTA numeric alignment remains a required follow-up before using fast STA as CTS timing/power source of truth.

## Incremental Update Progress

Focused validation rerun on 2026-05-18 after fixing incremental master-change invalidation:

- `clang-format -i src/operation/iCTS/source/database/adapter/fast_sta/FastStaIncremental.cc src/operation/iCTS/source/database/adapter/fast_sta/FastStaTiming.cc src/operation/iCTS/source/database/adapter/fast_sta/FastStaPower.cc src/operation/iCTS/source/database/adapter/fast_sta/FastStaAdapter.cc src/operation/iCTS/test/database/adapter/fast_sta/FastStaAdapterTest.cc`
- `cmake --build build --target icts_test_database_adapter_fast_sta -j 8`
- `./bin/icts_test_database_adapter_fast_sta`
- `rg -n "try\s*\{|catch\s*\(|throw\b|std::cout|printf\(" src/operation/iCTS/source/database/adapter/fast_sta src/operation/iCTS/test/database/adapter/fast_sta`

Result:

- `5 tests from FastStaAdapterTest`
- `5 passed`
- Forbidden-pattern scan returned no matches.

Focused validation run on 2026-05-18 03:06 +0800:

- `cmake --build build --target icts_test_database_adapter_fast_sta -j 8 && ./bin/icts_test_database_adapter_fast_sta`

Result:

- `5 tests from FastStaAdapterTest`
- `5 passed`

Covered by the added synthetic two-level CTS tree case:

- `FastStaIncremental::changeBufferMasterIncremental` applies the requested cell master change and returns a `FastStaDirtyRegion`.
- The dirty region starts from the upstream driver boundary needed to recompute the changed buffer input cap effect, then includes the downstream subtree.
- `FastStaTiming::updateRegion` refreshes dirty net loads/Pi/Elmore data and repropagates timing from the preserved boundary timing.
- `FastStaPower::updateRegion` refreshes dirty net switching power and dirty buffer internal/leakage/area terms, then resums the context total.
- The incremental result is compared against a full recomputation after the same master change for input cap, net load, sink arrival, sink slew, switching power, internal power, leakage, and area.

This is a focused proof of the incremental API boundary. It does not replace the remaining OpenSTA numeric alignment work, real committed-CTS context validation, char migration, or optimization migration.
