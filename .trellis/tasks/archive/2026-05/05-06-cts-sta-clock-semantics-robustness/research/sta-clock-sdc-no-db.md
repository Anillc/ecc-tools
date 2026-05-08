# STA clock-only SDC feasibility

## Question

Can STA read SDC and get clock periods without reading DB, and can it build enough clock information for CTS clock identity without a full DB-backed timing context?

## Current STA behavior

- `Sta::readSdc()` registers normal SDC Tcl commands, resets constraints, and sources the SDC file. It does not directly require iDB, but the registered SDC commands do resolve design objects through the STA netlist.
- `create_clock` requires `source_objects`. During execution it calls `FindObjOfSdc()` for each source object. Empty object lists are fatal, so a completely empty netlist cannot reliably read ordinary clock SDC with the current command implementation.
- `get_ports` resolves ports from `design_nl->findPort()` and fatals if a requested port is absent.
- `get_clocks` and `all_clocks` resolve from `SdcConstrain`, not the netlist. They are usable after clocks already exist.
- `SdcClock` stores clock name, period in ns, waveform, source design objects, propagated flag, and generated-clock metadata.
- `TimingEngine::getClockList()` returns applied `StaClock` objects from `Sta::_clocks`; those exist after SDC constraints are applied to the timing graph. It is not suitable for no-DB/no-graph period extraction.
- `StaApplySdc::setupClocks()` converts `SdcClock` to `StaClock` and requires graph vertices for each source object. That path is full-design dependent.

## Conclusion

Existing `readSdc()` can work without iDB only if STA has a usable netlist containing the referenced ports/pins. It cannot be treated as a no-DB/no-netlist period extractor because `create_clock` and object collection can fatal on unresolved source objects.

A minimal `readSdcClockPeriodsOnly()` is justified. It should execute SDC through Tcl semantics but use clock-only SDC command implementations that collect clock declarations without resolving source objects into `DesignObject*`. This avoids brittle regex parsing while avoiding full STA graph or iDB dependency.

## Recommended interface split

- `readSdcClockPeriodsOnly(path)` returns logical clock period records: clock name, period ns, waveform if available, generated-clock derivation status, source expression text, and diagnostics.
- A broader `readClockOnly(path)` can wrap the same clock-only reader and return logical clock records, not physical CTS net mappings.

## Important limitation

Clock-only SDC parsing can recover logical clock identity and periods. It cannot prove the physical iDB net that CTS should synthesize unless source expressions are resolved against a design model or directly resolvable iDB object. Therefore CTS should treat SDC clock metadata as authoritative for logical clock existence, name, and period. iDB clock-net flags should not be used as a fallback clock discovery source.

Physical net materialization should be strict:

- best: full design STA object-to-net mapping when available;
- acceptable: SDC-declared clock name plus config-provided net mapping, only if the clock exists in SDC;
- acceptable: clock-only source expression that directly resolves to one iDB port/pin/net after iDB is available;
- not acceptable: fallback discovery from iDB `is_clock()` or iDB clock net lists.

## Evidence

- `src/operation/iSTA/source/module/sta/Sta.cc:180` normal `readSdc()`.
- `src/operation/iSTA/source/module/sta/Sta.cc:1217` normal SDC command registration.
- `src/operation/iSTA/source/module/sdc-cmd/CmdCreateClock.cc:78` `create_clock` execution.
- `src/operation/iSTA/source/module/sdc-cmd/CmdCreateClock.cc:115` `FindObjOfSdc()` result is required.
- `src/operation/iSTA/source/module/sdc-cmd/CmdGetPorts.cc:53` `get_ports` resolves netlist ports and fatals if missing.
- `src/operation/iSTA/source/module/sdc-cmd/CmdGetClocks.cc:44` `get_clocks` reads SDC clocks from `SdcConstrain`.
- `src/operation/iSTA/source/module/sdc/SdcClock.hh:40` `SdcClock` data model.
- `src/operation/iSTA/source/module/sta/StaApplySdc.cc:54` apply-SDC clock setup requires graph vertices.
- `src/operation/iSTA/api/TimingEngine.cc:738` `getClockList()` returns applied `StaClock` objects.
