# Design: iCTS iSTA/iPA Decoupling

## Current Coupling Summary

| Area | Current iSTA/iPA coupling | Main call sites | Replacement decision |
| --- | --- | --- | --- |
| Initialization/global state | `STAAdapter::init/resetStaTransientState` creates/destroys iSTA timing engine and destroys iPA power state. | `flow/setup/Setup.cc`, `database/adapter/sta/STAAdapter.cc` | Delete `STAAdapter` from CTS runtime. CTS setup should initialize `Wrapper`/FastSTA inputs only. |
| Wire RC | `STAAdapter` calls `TimingIDBAdapter::getResistance/getCapacitance`. | `FastStaParasitics`, `FastStaChar`, H-tree compensation, sink branch, characterization, QoR local RC metrics | Replace with `Wrapper` RC methods backed by iDB routing layer data. |
| Liberty/cell metadata | `STAAdapter` queries iSTA Liberty cells/ports/tables. | `FastStaLiberty`, optimization preparation, characterization library, instantiation conversion, embedding, sink clustering, QoR area/cap | Replace with `Wrapper` Liberty methods. `FastStaLiberty` remains the fast STA conversion layer but consumes `Wrapper`, not `STAAdapter`. |
| iPA helper usage | Uses `ipower::Power::destroyPower`, `c_default_clock_toggle`, and `ipower::CalcAveragePower`. | `STAAdapter.cc`, `STAAdapterTimingUpdate.cc`, `STAAdapterRootDriverQuery.cc` | Delete iPA calls. Use local constants/helpers in CTS root-driver/FastSTA power code. |
| SDC/clock tracing Liberty classification | Direct `ista::TimingEngine` Liberty lookup is used to classify sink/macro/clock pins. | `WrapperClockReader.cc`, `adapter/sdc/clock_trace/*` | Move classification to `Wrapper` Liberty query helpers. Keep SDC parser, but remove iSTA engine lookup. |
| FastSTA environment | `FastStaEnvironment`, char spec, and clock context store `STAAdapter*`. | `FastSta.hh`, `FastStaClockState.hh`, `FastStaBuilder.cc`, `FastStaChar.cc`, `FastStaParasitics.cc` | Store `Wrapper*` or copied immutable data. Timing remains inside `FastSTA`. |
| Full-design STA | iSTA rebuilds full timing netlist, reads SDC, updates graph, reports setup/hold and path skew. | `QorEvaluation.cc`, `QorEvaluationMetrics.cc`, `QorEvaluationRootProbe.cc`, `STAAdapterTimingUpdate.cc` | Do not preserve as full-design STA. Replace only CTS-local equivalents; delete non-equivalent full setup/hold features and their logs/API fields. |

## Wrapper Boundary

`Wrapper` becomes the CTS database facade. It may expose RC and Liberty query methods directly; no new RC/Liberty provider classes should be created.

### RC API Shape

Add methods conceptually equivalent to:

```cpp
auto queryWireResistanceOhm(int routing_layer, double length_um, std::optional<double> wire_width_um) const -> double;
auto queryWireCapacitancePf(int routing_layer, double length_um, std::optional<double> wire_width_um) const -> double;
auto queryRequiredWireResistanceOhm(int routing_layer, double length_um, std::optional<double> wire_width_um) const -> double;
auto queryRequiredWireCapacitancePf(int routing_layer, double length_um, std::optional<double> wire_width_um) const -> double;
auto queryConfiguredClockRouteSegmentRc(const Config& config) const -> ClockRouteSegmentRc;
```

Implementation should mirror the current `TimingIDBAdapter` behavior using `_idb_layout->get_layers()->get_routing_layers()`:

- resolve `routing_layer` as the current CTS 1-based routing-layer index;
- default `wire_width_um` to LEF routing-layer width divided by DBU-per-micron;
- resistance uses LEF resistance, segment length, and width;
- capacitance uses LEF area capacitance plus edge capacitance;
- normalize method names and units so callers no longer divide by `1000.0` unless the method explicitly returns milli-ohms.

The implementation phase must add a golden/unit check against the previous formula to avoid changing RC units accidentally.

### Liberty API Shape

Add `Wrapper` methods for the Liberty data CTS actually needs:

- buffer input/output port names;
- input pin capacitance;
- sink pin capacitance by CTS `Pin*`;
- input slew limit and table-axis fallback;
- output cap limit and table-axis fallback;
- cell height and area;
- buffer/inverter timing tables and output slew tables;
- internal power tables, leakage, nominal voltage, and threshold percentages;
- sequential/clock-pin classification used by SDC clock tracing.

Important constraint: current iDB `IdbCellMaster`, `IdbTerm`, and `IdbPin` do not hold timing/power LUTs or Liberty electrical constraints. The Wrapper implementation must therefore use a Liberty data source that is independent of iSTA engine initialization. It may use the existing database Liberty parser/data structures if they can be linked without `ista-engine`; the task acceptance is about removing the iSTA timing engine dependency, not about avoiding every historical `ista` namespace name in lower-level parser code.

`FastStaLiberty` should keep translating raw Liberty records into `FastStaLibertyCell`; only its source changes from `STAAdapter` to `Wrapper`.

## FastSTA Boundary

Do not introduce a standalone TimingProvider class.

`FastSTA` owns CTS-local timing behavior:

- clock context construction;
- route-tree parasitic injection;
- arrival/slew propagation;
- skew summary;
- cap/slew violation status;
- switching/internal/leakage/area power summary;
- char sampling.

Required structural changes:

- `FastStaEnvironment::sta_adapter` -> `Wrapper* wrapper`;
- `FastStaCharTopologySpec::sta_adapter` -> `Wrapper* wrapper`;
- `FastStaClockContext::sta_adapter` -> `Wrapper* wrapper` or copied RC/Liberty context;
- `FastStaBuilder` uses Wrapper-backed pin cap, sink slew, source cap-limit fallback, and Liberty extraction;
- `FastStaParasitics` and `FastStaChar` use Wrapper-backed RC methods;
- optimization/evaluation/reporting query FastSTA directly for CTS-local metrics.

## Full-Design Timing Disposition

| Current feature | Current source | Equivalence with FastSTA | Decision |
| --- | --- | --- | --- |
| `refreshFullDesignTimingContext` | Converts iDB to iSTA timing netlist, reads SDC, builds graph, initializes RC tree, updates timing. | No. FastSTA models CTS clock tree only. | Delete from CTS flow. No full iSTA refresh in iCTS and no "unavailable STA refresh" warning log. |
| `setPropagatedClocks` | Mutates iSTA SDC constraints to mark clocks propagated. | No. Only meaningful inside full STA. | Delete. No replacement. |
| `updateTiming`/`reportTiming` | Runs full iSTA update/report after CTS instantiation. | No. | Delete full timing report step from CTS; keep CTS-local timing reports. |
| setup/hold TNS/WNS/suggested frequency | `queryClockTiming(s)` from full STA path groups. | No. Clock-tree skew is not setup/hold closure. | Delete from CTS QoR, report tables, logs, and `CTSAPI::outputSummary()` feature mapping. Do not rename FastSTA skew into these fields. |
| full latency/skew path metrics | `queryClockLatencySkew` with launch/capture pins and path counts. | Partial only. FastSTA can compute min/max CTS sink arrival skew, not full launch/capture path skew. | Delete full launch/capture/path-count fields and related tables. Keep only CTS-local clock-tree skew if already supported under local metric names. |
| exact RC tree install into iSTA | `installClockNetRcTree` installs CTS routes into iSTA net RC graph. | No need without iSTA. | Delete. For local route timing, use FastSTA route-tree injection or existing iCTS `TimingEngine` on `RCTree`. |
| root-to-leaf pin arrival probe | `queryPinClockArrival` from full STA clock arrivals. | Partial. FastSTA can query arrivals for nodes present in the CTS clock context. | Delete this report unless it is rebuilt later as a pure FastSTA report. Do not retain unavailable log sections. |
| pin slew probe | `queryPinSlew` from iSTA vertex slew. | Partial. FastSTA can report slew for CTS nodes only. | Delete arbitrary full-design pin slew probes. Keep FastSTA cap/slew status only where existing CTS-local analysis consumes it. |
| clock source drive cap limit | `queryClockSourceDriveCapLimit` can refresh full timing and derive source driver cap. | Partial. | Prefer config `max_cap`, then Wrapper Liberty output cap limit for the source driver, then table-axis fallback. No full timing refresh. |
| root driver direct delay/power | iSTA Liberty direct lookup plus iPA helpers. | Yes, if Liberty data is available through Wrapper. | Move to Wrapper/FastStaLiberty/local helper path; keep behavior but remove iSTA/iPA API calls. |

## iEDA Feature Summary Changes

`CTSAPI::outputSummary()` currently maps `QorSummary::clocks_timing` into `ieda_feature::CTSSummary::clocks_timing`, including setup/hold TNS/WNS and suggested frequency. These fields are full-design STA outputs and must be removed from the iCTS mapping. If the shared `ieda_feature::CTSSummary` struct is used by other tools, iCTS should simply stop populating the removed fields; if the struct is CTS-owned enough to edit safely, the fields should be removed from the struct as well.

The remaining CTS summary fields are local physical/topology features and stay supported:

- buffer count and buffer area;
- clock path min/max buffer depth;
- max clock-tree level;
- max and total clock wirelength.

## Build Boundary

Targets to remove or rewire:

- `src/operation/iCTS/external_libs/icts_api_external_libs.cmake` currently contributes `ista-engine`.
- `database/adapter/sta/CMakeLists.txt` links `power` and iSTA libs.
- `database/adapter/sdc/CMakeLists.txt` links `ista-engine` for clock tracing classification.
- `database/adapter/fast_sta/CMakeLists.txt` indirectly links STA adapter because fast_sta Liberty/clock_state depend on `icts_source_database_adapter_sta`.
- Multiple flow/synthesis/optimization CMake files link `icts_source_database_adapter_sta`.

End state:

- no `icts_source_database_adapter_sta` production dependency;
- `database/adapter/sta` either removed from iCTS build or left as deleted/obsolete source not referenced by CMake;
- iCTS tests updated so CTS coverage does not require `ista-engine`/`power`.

## Compatibility Notes

- This is an intentional QoR behavior change: CTS will no longer publish full-design setup/hold metrics.
- Report labels must avoid implying equivalence between full setup/hold timing and CTS-local clock tree timing.
- Deleted full-design timing features should not leave "unavailable" warning logs or placeholder report tables.
- Wrapper-backed Liberty queries should fail loudly for missing required Liberty data in paths that cannot proceed safely, and should use explicit fallbacks only where current `STAAdapter` already had a fallback.
