# 解绑 iCTS 的 iSTA 和 iPA 集成

## Goal

Complete the iCTS-side decoupling from iSTA and iPA.

After this task, iCTS production code must not initialize, call, include, or link the iSTA timing engine or iPA power engine. CTS-local RC, Liberty, timing, and power functionality should be served by iCTS-owned code paths:

- RC and Liberty access go through the existing `Wrapper` facade, whose semantics are expanded from "iDB wrapper" to "CTS database facade".
- CTS-local clock timing, skew, slew, cap, and power queries stay inside `FastSTA`; do not introduce a separate TimingProvider class.
- Full-design timing features currently supplied by iSTA must be explicitly classified and either replaced with CTS-local equivalents or removed/degraded from CTS reports.

## Requirements

- Remove iCTS runtime dependence on `STAAdapter`, `ista::TimingEngine`, `ista-engine`, `ipower::Power`, and the `power` target.
- Replace RC queries currently routed through `STAAdapter`/`TimingIDBAdapter` with `Wrapper` methods backed directly by iDB/LEF technology data.
- Replace Liberty/cell metadata queries currently routed through `STAAdapter` with `Wrapper` methods backed by Liberty data available outside iSTA engine initialization.
- Replace iPA-only helper usage with local CTS math:
  - clock toggle density: `2.0 / period_ns`
  - average rise/fall energy: `(rise + fall) / 2.0`
- Move CTS-local clock timing replacement work into `FastSTA`, without adding a separate TimingProvider abstraction.
- Remove or replace all full-design STA evaluation/report paths:
  - full timing context refresh/update/report
  - propagated-clock marking through iSTA SDC constraints
  - setup/hold TNS/WNS/suggested frequency
  - full STA latency/skew path metrics
  - pin clock-arrival/slew probes
  - iSTA RC-tree installation for final timing
- Delete all logs, tables, diagnostics, and report sections whose only purpose is to explain an unavailable full-design STA feature.
- Delete iEDA feature-facing CTS summary fields that depend on removed full-design STA metrics, including clock setup/hold timing summaries.
- Clean CMake so iCTS source targets and iCTS tests no longer link `ista-engine` or `power` for CTS functionality.
- Update tests and diagnostics to cover Wrapper-backed RC/Liberty and FastSTA-backed CTS timing.

## Acceptance Criteria

- [x] `src/operation/iCTS/source` has no production include of `api/TimingEngine.hh`, `api/TimingIDBAdapter.hh`, or `api/Power.hh`.
- [x] `src/operation/iCTS/source` has no production dependency on `STAAdapter` after the replacement is complete.
- [x] iCTS source CMake files do not link `ista-engine` or `power`.
- [x] `FastStaEnvironment`, `FastStaCharTopologySpec`, and `FastStaClockContext` no longer store `STAAdapter*`.
- [x] RC values used by CTS routing, characterization, fast STA parasitics, and QoR local RC metrics are queried through `Wrapper`.
- [x] Liberty-derived buffer ports, pin caps, slew/cap limits, timing/power tables, area, leakage, and cell classification are queried through `Wrapper` or fast STA conversion helpers fed by `Wrapper`.
- [x] Full-design setup/hold timing metrics are removed from CTS reports; they are not replaced by non-equivalent CTS-local skew values under the old names.
- [x] Removed full-design timing features do not emit degraded/unavailable warning logs, because the unsupported functionality is deleted rather than retained as a degraded mode.
- [x] `CTSAPI::outputSummary()` no longer maps setup/hold clock timing data into `ieda_feature::CTSSummary`.
- [x] CTS-local clock-tree arrival/skew/slew/cap/power reports use `FastSTA` or existing iCTS local `TimingEngine` where equivalent.
- [x] iPA calls and headers are removed from iCTS; root-driver and fast STA power calculations use local helpers only.
- [x] Tests/build targets covering iCTS pass without linking iSTA/iPA for CTS code.

## Notes

- User direction: create this task now, do not implement code in this turn.
- User direction: RC and Liberty should be implemented directly on `Wrapper`; do not introduce separate provider classes for them.
- User direction: TimingProvider functionality should be integrated directly into `FastSTA`; do not introduce a separate TimingProvider class.
- Full-design timing is not equivalent to current `FastSTA`; unsupported full-design timing behavior must be deleted, not kept as log-only degraded behavior.
- Planning decision resolved by user: any iSTA/iPA-backed feature that cannot be supported after decoupling is removed directly, including related report/log text and CTS API feature summary fields.
