# Characterization Repair And Real-Tech Validation

## Goal

Repair and harden `src/operation/iCTS/source/module/characterization` for real-tech usage, with the characterization flow centered on `STAAdapter` and verified by real-tech tests.

## Accepted Architecture

- `CharBuilder::init()` is the only place that resolves characterization inputs from `CONFIG_INST`.
- Characterization wire-length semantics are:
  - `wire_length_unit_um`: base lattice unit
  - `wire_length_iterations`: number of enumerated lattice lengths
  - `max_length`: compatibility placeholder only, not the active slicing control
- Real-tech test bootstrap keeps the flow script path and CTS config path as explicit setup-state inputs; characterization tests must not re-derive CTS config by walking parent directories.
- Max slew/cap resolution priority is:
  - Config value
  - Liberty port limits across configured buffers, take the minimum available
  - Liberty timing-table axis upper bounds across configured buffers, take the minimum available
- Characterization timing stays on the shared iSTA engine, but uses a char-only lightweight update entry from `TimingEngine`.
- The temporary clock stays stable for one topology sweep and is recreated only at topology boundaries.
- Timing semantics for the char source are supplemented in `STAAdapter`; global iSTA RC fallback changes must be removed.
- Power flow reuses iPA graph/toggle propagation, but final power analysis is restricted to the selected characterization insts/nets inside `STAAdapter`; global iPA fallback changes must be removed.
- External-module edits must stay minimal and must not perturb the original design flow.

## Functional Scope

- Fix characterization max slew/cap resolution.
- Keep buffer selection driven by config buffer list.
- Align wire-length enumeration to the configured lattice semantics.
- Keep pattern feasibility pruning and group-local join pruning active.
- Fix H-tree half-cap matching to use the discretized half-cap key, not index halving.
- Keep real-tech validation focused on:
  - base segment char from iSTA
  - segment composition
  - simple manual H-tree composition and reporting

## Implementation Tasks

- [x] Clean `STAAdapter` timing path:
  - inject source-buffer electrical semantics at the source output pin
  - remove global `StaDelayPropagation` and `StaSlewPropagation` fallback behavior
- [x] Clean `STAAdapter` power path:
  - store selected char inst/net scope in `STAAdapter`
  - rebuild iPA context per sample
  - run leakage/internal only on selected insts
  - compute switching only on selected nets
  - remove global `PwrCalcSPData` fallback behavior
- [x] Keep only minimal external diffs:
  - `iSTA/api/TimingEngine.cc/.hh`
  - `iSTA/api/TimingIDBAdapter.cc`
  - `iSTA/source/module/sta/StaClockSlewDelayPropagation.cc` if still required by the source-output-root topology
  - `iIR/api/iIR.cc`
- [x] Re-check touched comments in characterization/config/test files:
  - no debug-process comments
  - no conversational comments
  - add short comments only where algorithmic intent is non-obvious
- [x] Keep real-tech tests aligned with the accepted flow and config semantics.
- [x] Make real-tech asset/bootstrap config explicit:
  - `RealTechAssets` carries `cts_config_path` directly
  - `RealTechSetupState` exposes explicit `flow_script_path` and `cts_config_path`
  - characterization helpers stop re-deriving CTS config paths from flow-script parents

## Acceptance Criteria

- [x] `CharBuilder` resolves max slew/cap with the required priority and does not fall back to output port capacitance for cap limits.
- [x] Characterization timing works without the global iSTA RC fallback edits.
- [x] Characterization power is non-zero on active real-tech samples and is derived from selected char insts/nets, not from global fallback behavior.
- [x] External diffs in `iSTA`, `iPA`, and `iIR` are minimal and do not add unrelated formatting or semantic drift.
- [x] Real-tech characterization tests pass and emit usable segment/H-tree reports.
- [x] Real-tech asset/config plumbing uses explicit CTS-config state instead of test-side path back-derivation.
- [x] `ecc_dev_tools` checks pass on touched paths and on full `src/operation/iCTS`.

## Notes

- Real-tech validation remains focused on usability of the characterization module itself, not the full production `TopologyGen -> H-tree selection` orchestration.
- If characterization limits cannot be resolved or no usable buffers remain, the build should warn and return early instead of entering a meaningless sweep.
