# CTS FastSTA and STAAdapter boundary refactor

## Goal

Clarify CTS FastSTA and STAAdapter semantics without pretending they are fully separable.

FastSTA should be the CTS-owned fast timing, power, and legality engine used by characterization and optimization. STAAdapter remains the iSTA/iPower-backed data and final timing bridge. FastSTA may depend on STAAdapter because the authoritative Liberty/STA data lives there, but the dependency must be intentional, visible, and limited to named query surfaces instead of accidental coupling through stale char-only APIs or ambiguous helper calls.

This task also standardizes legality semantics:

- Cap legality is net-cap legality for every driven net, including the source boundary net. Do not add a separate "source drive cap violation" category.
- Slew legality distinguishes buffer input transition and sink pin transition, because those are different receiving-pin roles and should be readable in reports/baselines.
- `module/timing` remains unchanged in this task.
- FastSTA remains in the current `database/adapter/fast_sta` location in this task.

## Confirmed Facts

- `database/adapter/fast_sta/FastStaAdapter.hh/.cc` expose a CTS-facing facade for fast timing/power contexts, char sampling, mutation, and cap/slew query APIs.
- Current characterization and optimization production call sites use `FastStaAdapter`, including `module/characterization/CharBuilderCircuit.cc`, `module/characterization/CharBuilderSlewSampling.cc`, and `flow/optimization/*`.
- FastSTA currently depends on STAAdapter for real technology data:
  - Liberty and table data through `STAAdapterInternal.hh` and iSTA Liberty objects;
  - pin capacitance through `STA_ADAPTER_INST.queryPinCapacitance(...)`;
  - wire RC through `STA_ADAPTER_INST.queryRequiredWireResistance/Capacitance(...)`;
  - cell area, buffer ports, cap/slew limits, and table-axis fallbacks through STAAdapter query APIs.
- The old STAAdapter char-only runtime APIs are still declared and implemented, but current production characterization uses FastSTA and no longer calls APIs such as `initCharOnly`, `createCharInstance`, `prepareCharTimingContext`, `setCharBufferInputSlew`, `updateCharTimingSample`, `prepareCharPower`, or `updateCharPower`.
- `FastStaClockTree::buildFromClock(...)` already appends `clock.get_clock_source_net()` into `context.nets`, so the source boundary net can and should be checked by normal net-cap traversal.
- `FastStaBuilder::snapshotClockData(...)` currently assigns `net.max_cap_pf` from runtime `max_cap` or from the driver buffer cell output limit. That may leave source nets with weak or missing max-cap semantics when the source driver is not represented as a normal buffer output cell.
- STAAdapter already has `queryClockSourceDriveCapLimit(const Pin* clock_source)`, which can be used to resolve the source boundary cap limit and store it on the existing source net as `FastStaNet::max_cap_pf`.
- FastSTA slew legality currently uses `FastStaNode::max_slew_ns`; buffer inputs are initialized from Liberty input slew limits. Sink pin transition coverage needs explicit initialization and status semantics.
- Fast char sampling currently mutates global config for root slew:
  - `FastStaChar::runSample(...)` writes `CONFIG_INST.root_input_slew`, runs `FastStaTiming::update(...)`, then restores the old value.
  - `FastStaTiming::update(...)` reads root slew from `CONFIG_INST`.
- `src/operation/iCTS/source/module/timing/TimingEngine` is a pure RCTree propagation/evaluation helper. It is used by cluster-constraint estimation and QoR measurement. It remains out of scope for this task.

## Requirements

- Rename the FastSTA facade:
  - `database/adapter/fast_sta/FastStaAdapter.hh` -> `FastSta.hh`;
  - `database/adapter/fast_sta/FastStaAdapter.cc` -> `FastSta.cc`;
  - `class FastStaAdapter` -> `class FastSTA`;
  - `FAST_STA_ADAPTER_INST` -> `FAST_STA_INST`.
- Keep FastSTA in `database/adapter/fast_sta` for this task.
- Keep FastSTA's STAAdapter dependency, but make it explicit:
  - group and name STA-backed FastSTA data queries clearly;
  - make call sites read as Liberty/tech/query dependencies, not hidden full-STA or stale char-runtime dependencies;
  - document why the dependency remains.
- Remove obsolete STAAdapter char-only runtime APIs after a full-repository search confirms no live users:
  - char instance/net/pin/clock creation and destruction;
  - char RC tree and char net graph setup;
  - char timing prepare/sample/update APIs;
  - char power prepare/update/query/destroy APIs;
  - char-only init/finish/reset state that only supports the removed runtime path.
- Keep STAAdapter technology query APIs that are still used by CTS:
  - wire RC;
  - cell output cap limits and table-axis fallbacks;
  - cell input slew limits and table-axis fallbacks;
  - clock source drive cap limit;
  - pin cap, pin slew, root driver cost, buffer ports, area/height as needed.
- Unify cap legality under net cap:
  - ensure every FastSTA net, including `clock_source_net`, has a meaningful `load_cap_pf` and `max_cap_pf` when a source or driver cap limit is available;
  - use `queryClockSourceDriveCapLimit(...)` to initialize the source boundary net cap limit when appropriate;
  - do not introduce a separate source-drive-cap violation type.
- Unify slew legality by receiving-pin role:
  - keep buffer input transition checks as buffer slew violations;
  - add sink pin transition checks as sink slew violations;
  - make `FastStaSlewStatus` or associated reporting/baseline data identify whether the checked node is a buffer input or sink.
- Make root input slew explicit:
  - stop `FastStaChar::runSample(...)` from mutating `CONFIG_INST.root_input_slew`;
  - carry root slew through FastSTA context or sample request data;
  - preserve existing default behavior for normal clock contexts.
- Preserve optimization legality policy:
  - no new cap/slew violations;
  - existing cap/slew violations must not worsen;
  - the same policy must apply to source net cap, ordinary net cap, buffer slew, and sink slew.
- Leave `src/operation/iCTS/source/module/timing` unchanged in this task.

## Acceptance Criteria

- [ ] `FastStaAdapter.hh/.cc`, `class FastStaAdapter`, and `FAST_STA_ADAPTER_INST` no longer exist in production source; the new names are `FastSta.hh/.cc`, `class FastSTA`, and `FAST_STA_INST`.
- [ ] FastSTA remains under `src/operation/iCTS/source/database/adapter/fast_sta`.
- [ ] FastSTA's remaining STAAdapter dependency is documented and visible through clearly named STA-backed query calls or a small local query helper; no fake provider layer is introduced solely to claim independence.
- [ ] Obsolete public STAAdapter char-only runtime APIs are removed when no full-repository users remain.
- [ ] STAAdapter technology query APIs still needed by CTS remain available.
- [ ] Source boundary cap is represented as normal `FastStaNet` cap status; optimization cap baseline/check loops include it through the normal net-cap path.
- [ ] No new source-drive-cap-specific violation type or report bucket is introduced.
- [ ] Slew status distinguishes buffer input transition and sink pin transition.
- [ ] Sink pin transition limits are initialized and included in slew baseline/check loops.
- [ ] Root input slew for char samples is passed explicitly and no longer mutates global config.
- [ ] `module/timing` files, class names, CMake targets, and users remain unchanged.
- [ ] Affected iCTS targets compile.
- [ ] A representative iCTS validation path confirms final ordinary STA evaluation still runs through STAAdapter.

## Out of Scope

- Moving FastSTA out of `database/adapter/fast_sta`.
- Fully decoupling FastSTA from STAAdapter or adding a provider layer whose only purpose is cosmetic separation.
- Renaming, moving, or deleting `src/operation/iCTS/source/module/timing`.
- Replacing final QoR/signoff timing with FastSTA.
- Rewriting FastSTA numerical algorithms beyond explicit root slew and legality coverage fixes.
- Broad optimization search-policy changes or performance redesign.

## Review Question

Please review whether the implementation scope below matches the intended first coding pass:

- rename FastSTA facade;
- keep location and STAAdapter dependency;
- make STA-backed query dependency explicit;
- remove stale STAAdapter char-only runtime APIs;
- fix cap legality through normal net cap, including source net;
- split slew semantics into buffer and sink receiving-pin checks;
- make char root slew explicit;
- leave `module/timing` untouched.
