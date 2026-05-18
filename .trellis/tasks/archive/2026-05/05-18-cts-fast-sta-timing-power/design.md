# CTS fast STA timing and power design

## Architecture

Add a new adapter-side module:

```text
src/operation/iCTS/source/database/adapter/fast_sta/
  CMakeLists.txt
  FastStaAdapter.hh/.cc
  FastStaTypes.hh
  FastStaBuilder.hh/.cc
  FastStaLiberty.hh/.cc
  FastStaClockTree.hh/.cc
  FastStaParasitics.hh/.cc
  FastStaDmpCeff.hh/.cc
  FastStaTiming.hh/.cc
  FastStaPower.hh/.cc
  FastStaIncremental.hh/.cc
  FastStaReport.hh/.cc
```

`database/adapter/fast_sta` is a peer of `database/adapter/sta` and `database/adapter/sdc`. The parent `database/adapter/CMakeLists.txt` adds `ICTS_DATABASE_ADAPTER_FAST_STA`, adds the subdirectory, and links the target into `icts_source_database_adapter`.

Target naming:

```text
icts_source_database_adapter_fast_sta
```

The module is a database adapter because its job is to bridge committed CTS design state plus external tech data into a CTS-owned timing/power model. It is not a flow stage and not an optimization algorithm.

## Boundaries

`FastStaAdapter`
: Public CTS-facing facade. Owns clock contexts and exposes build, query, update, and summary APIs. It should be the only fast STA class directly consumed by flow/module code.

`FastStaBuilder`
: Initialization bridge. Reads committed CTS `Design`, clock tree data, `STAAdapter`/iSTA-forwarded Liberty data, routing RC data, SDC clock period, and config limits. It builds fast STA-owned snapshots and then releases raw external object dependencies.

`FastStaLiberty`
: Fast STA-owned library snapshot. Stores buffer/sink pin caps, max cap, area, voltage, delay/slew tables, internal power tables, leakage values, and transition/pin metadata needed by CTS. Raw iSTA Liberty objects stay behind builder extraction helpers.

`FastStaClockTree`
: CTS clock-tree graph. Stores nodes, buffer pins, sink pins, parent/child topology, routed segment geometry, selected buffer master, and sink domain mapping.

`FastStaParasitics`
: OpenSTA-style RC calculations on the CTS tree. Stores detailed RC segments, node capacitance, downstream cap, driver-side Pi model, and per-load Elmore values.

`FastStaDmpCeff`
: DMP effective capacitance solver for reduced Pi load. This is the cell timing load model. It should not fallback to total cap during normal CTS usage.

`FastStaTiming`
: Levelized timing propagation. Computes buffer cell delay/slew from Liberty tables using DMP Ceff and computes net delay/load slew from per-load Elmore and the DMP waveform response. Produces sink arrivals, skew, per-node slew, per-net load, and cap legality.

`FastStaPower`
: CTS-only power and cost calculation. Computes clock net switching, buffer internal, leakage, and area using fast STA timing/load data and Liberty power data.

`FastStaIncremental`
: Dirty-range management for sizing updates. A master change invalidates upstream load/Pi data up to the clock root or nearest unaffected boundary and downstream timing/power from the changed buffer output. It coordinates recalculation order but does not decide optimization moves.

`FastStaReport`
: Focused diagnostics and comparison output for OpenSTA alignment, char-vs-fast-STA, and flow logs. It should avoid verbose per-node logs in normal runs.

## Calculation Contract

Timing target is OpenSTA `dmp_ceff_elmore` for the CTS subset.

For every driver net:

1. Build detailed RC tree from CTS routed segments.
2. Add sink pin cap or downstream buffer input pin cap at terminal nodes.
3. Compute downstream capacitance bottom-up.
4. Reduce the driver network to Pi values `c2`, `rpi`, and `c1` using OpenSTA-style admittance moments.
5. Store per-load Elmore delay using downstream cap.
6. Compute DMP Ceff from the reduced Pi and input slew.
7. Lookup Liberty cell delay/slew with `(input_slew, ceff)`.
8. Compute per-load net delay and load slew using per-load Elmore and driver waveform response.
9. Propagate arrival/slew to child buffer input pins and sink pins.

Power contract:

1. Clock activity density is `2 / period`.
2. Net switching power is `0.5 * load_cap * V^2 * activity_density` with one normalized unit system inside fast STA.
3. Buffer internal power uses Liberty internal power tables with `(input_slew, output_load)`, rise/fall handling explicit in the data model.
4. Leakage uses Liberty leakage data available through initialization. Conditional leakage can be represented, but the first CTS scope may use the deterministic CTS-safe default chosen from available Liberty data.
5. Area is summed from selected buffer masters.

## APIs

Initial facade shape:

```cpp
class FastStaAdapter {
 public:
  static auto buildClockContext(const Clock& clock) -> FastStaClockId;
  static auto rebuildClockContext(FastStaClockId clock_id) -> bool;
  static auto changeBufferMaster(FastStaClockId clock_id, FastStaNodeId node_id, std::string_view cell_master) -> bool;
  static auto updateTiming(FastStaClockId clock_id) -> bool;
  static auto updatePower(FastStaClockId clock_id) -> bool;

  static auto querySinkArrival(FastStaClockId clock_id, std::string_view sink_pin) -> std::optional<double>;
  static auto querySkew(FastStaClockId clock_id) -> FastStaSkewSummary;
  static auto queryNodeSlew(FastStaClockId clock_id, FastStaNodeId node_id) -> double;
  static auto queryNetLoad(FastStaClockId clock_id, FastStaNetId net_id) -> double;
  static auto queryCapStatus(FastStaClockId clock_id, FastStaNetId net_id) -> FastStaCapStatus;
  static auto queryPower(FastStaClockId clock_id) -> FastStaPowerSummary;
  static auto queryArea(FastStaClockId clock_id) -> double;
};
```

The exact signature can be adjusted to match local types, but the boundary must remain:

- build/update by clock context,
- query by fast STA IDs or stable CTS names,
- no raw iSTA/iDB/iPA objects exposed to CTS callers.

## Data Flow

Setup/initialization:

```text
committed CTS Design + Clock
  -> FastStaBuilder
  -> STAAdapter/iSTA/iDB forwarded tech queries
  -> FastStaLiberty + FastStaClockTree + FastStaParasitics
  -> FastStaTiming + FastStaPower
  -> FastStaAdapter clock context
```

Characterization migration:

```text
existing char caller
  -> FastStaAdapter build/query for controlled char topology
  -> FastStaTiming/FastStaPower result
  -> existing characterization result container
```

Optimization migration, owned by child task:

```text
buffer sizing algorithm
  -> FastStaAdapter query current skew/load/power/area
  -> FastStaAdapter changeBufferMaster + incremental update
  -> accept/reject move based on updated fast STA state
```

## OpenSTA Alignment Strategy

Use OpenSTA source as the reference implementation for:

- Pi reduction: `src/sta/parasitics/ReduceParasitics.cc`
- DMP Ceff: `src/sta/dcalc/DmpCeff.cc`
- Elmore load delay/slew entry: `src/sta/dcalc/DmpDelayCalc.cc`
- graph annotation semantics: `src/sta/dcalc/GraphDelayCalc.cc`
- power equations: `src/sta/power/Power.cc` and `src/sta/liberty/InternalPower.cc`

Validation should include focused CTS-like micro-cases:

- single buffer driving one sink with only capacitance,
- single buffer driving RC branch with two sinks,
- two-level buffer tree where child buffer input cap affects parent Ceff,
- sizing change that increases child input cap and dirties parent timing,
- clock switching/internal/leakage comparison on a small buffer tree.

## Compatibility

The first production integration keeps full iSTA available for final flow checks and QoR reporting. Fast STA becomes the CTS-local calculation source for char and optimization only after OpenSTA alignment cases pass.

Existing `STAAdapter` remains the source for:

- initial Liberty/iSTA/iDB data extraction,
- final full-design STA checks when flow already performs them,
- comparison reports during rollout.

## Tradeoffs

Do not implement a lumped-cap fallback as a normal mode. If required source data is missing in CTS-controlled scenarios, report the missing initialization contract and fail the focused operation.

Do not support full STA features that are irrelevant to CTS, including SI, current-source waveform models, multi-corner orchestration, arbitrary logic graph traversal, or sequential timing checks.

Do not put optimization policy into fast STA. Fast STA answers timing/power questions and applies requested master changes; optimization decides which changes to try.

## Rollback

Fast STA integration should be introduced behind CTS call-site changes. If validation finds a blocking mismatch, retain the module and keep char/optimization on the previous path until the mismatch is resolved. The optimization child task must not start replacing algorithms until fast STA exposes validated query/update APIs.
