# design.md

## Technical Design

### Scope

The investigation covers the iCTS stage entered by `Flow::readData()`, especially `DesignConversion::readClockData()` and `Wrapper::readClocks()`, which materialize SDC/configured clock nets from iDB into CTS `Clock`, `Net`, `Inst`, and `Pin` objects.

The local huge-case runner is `scripts/design/ics55_huge_dev/script/iCTS_script/run_iCTS_dev.tcl`. It will be temporarily changed from `$WORKSPACE/design.def` to `$WORKSPACE/design_clock.def` to make the default user command exercise the intended large clock net.

### Measurement Strategy

Use existing CTS runtime logging first. If the existing `read_data` stage is too coarse, add temporary or permanent low-risk substage timing around:

- SDC clock declaration reading and clock/net pair construction.
- `WRAPPER_INST.readClocks()` / iDB-to-CTS materialization.
- Clock period assignment and distribution summary emission.
- Inside wrapper materialization: net pin collection, inst creation, pin creation/indexing, and load insertion.

Prefer permanent schema/runtime or concise log timing only when it improves future diagnostics without noisy output.

### Likely Risk Area

The large clock net has O(100k) sinks. Any uniqueness helper that linearly scans an accumulating vector once per sink can produce O(N^2) read-time behavior. Candidate APIs include `Clock::add_load()` and `Net::add_load()`, both of which preserve uniqueness through `std::ranges::find`.

If profiling confirms this shape, the optimization should avoid per-sink linear uniqueness checks when the reader already has a unique, ordered list of iDB pins. The change must preserve uniqueness at the boundary where duplicates are actually possible.

### Compatibility

The optimized path must keep:

- The same driver/load classification.
- The same CTS pin full names and index behavior.
- The same clock source net association.
- The same clock distribution counts.
- The same failure behavior for duplicate CTS pin names and unresolved driver pins.

### Rollout / Rollback

The code change should be small enough to revert independently. The script input change is temporary and must be reverted before task close.
