# Design: CTS FastSTA and STAAdapter boundary refactor

## Objective

Refactor names and legality semantics so CTS has one readable fast timing surface:

- `FastSTA` is the fast CTS timing/power/legality facade.
- `STAAdapter` remains the source of STA/Liberty/iPower-backed data and final full-design timing.
- Cap legality is uniformly net-cap legality.
- Slew legality is uniformly receiving-pin transition legality, with buffer and sink roles reported separately.

The task intentionally does not move FastSTA, does not claim full STAAdapter independence, and does not touch `module/timing`.

## Boundary Decision

### FastSTA

FastSTA stays in:

```text
src/operation/iCTS/source/database/adapter/fast_sta
```

for this task.

Although FastSTA is conceptually a CTS fast timing engine, it still lives at the boundary where committed CTS design data, STA-backed Liberty data, wire RC data, and optimization queries meet. Moving it now would add churn without solving the real dependency issue.

### STAAdapter Dependency

FastSTA may depend on STAAdapter because the real data lives behind iSTA and the existing STA adapter:

- Liberty cell, port, arc, table, unit conversion, and table-axis data;
- pin capacitance and source pin limits;
- wire resistance/capacitance;
- full-design STA queries when needed for source or pin limits.

The change is not to hide this dependency behind an artificial provider. The change is to make the dependency explicit and readable:

- keep STA-backed query calls in named helper functions or narrowly named STAAdapter APIs;
- avoid using stale char-runtime APIs from FastSTA;
- avoid broad "adapter" naming for FastSTA itself;
- document the remaining coupling as data ownership, not accidental architecture leakage.

### STAAdapter

Keep STAAdapter responsibilities:

- iSTA/iPower setup and teardown;
- full committed-design timing update/report;
- exact STA RC tree installation;
- propagated clocks;
- STA-backed technology and pin queries used by CTS.

Remove stale public char-only runtime APIs after confirming no users remain. Retain STAAdapter query APIs that provide real data to CTS.

## Legality Semantics

### Cap Legality

There should be one cap legality concept:

```text
FastStaNet load cap <= FastStaNet max cap
```

This applies to:

- source boundary net;
- buffer output nets;
- any committed CTS clock net represented in FastSTA.

Do not add a separate "source drive cap" violation category. From a STA perspective, the source driver's cap limit is a max-cap limit on the driven net boundary. Reporting it as another violation type makes the optimizer and report harder to read.

#### Source Net Plan

Current evidence shows the source clock net is already present in `context.nets` because `FastStaClockTree::buildFromClock(...)` appends `clock.get_clock_source_net()`.

The implementation should ensure that this net receives a `max_cap_pf` through normal net initialization:

1. Identify the source boundary net from `clock.get_clock_source_net()` or by matching the net driven by `context.source_node_id`.
2. Resolve its cap limit with `STA_ADAPTER_INST.queryClockSourceDriveCapLimit(clock.get_clock_source())`.
3. Store that value in the same `FastStaNet::max_cap_pf` field used by every other net.
4. Keep `FastSTA::queryCapStatus(...)`, cap baseline capture, and optimization cap checking generic over all nets.

Fallback policy:

- If runtime `max_cap` is configured and intended as a hard cap, keep current behavior unless source-specific STA/Liberty cap limit should override it by policy.
- If no source-specific cap limit is available, use the same fallback rules already used for net cap, not a separate status.

Recommended policy for review: runtime `max_cap` remains the global hard override when present; otherwise use source-specific cap limit for source net and driver output cap limit for buffer nets.

### Slew Legality

Slew legality should be one concept with two receiving-pin roles:

```text
FastStaNode timing slew <= FastStaNode max slew
```

Roles:

- `buffer_input`: transition at inserted buffer input pins;
- `sink`: transition at final sink pins.

Buffer input limits already come from Liberty input slew limits during cell snapshot. Sink nodes need explicit max-slew initialization. For sink pins that have owning inst/cell data, the implementation should query a receiving-pin transition limit through STAAdapter or a clearly named STA-backed helper. If no sink-specific limit is available, use a documented fallback such as runtime max transition only if that is already the expected CTS policy.

`FastStaSlewStatus` should expose enough information for reports/baseline checks to distinguish buffer versus sink violations. It does not need separate algorithms; it needs clear role metadata.

## Root Slew Contract

Char sampling should stop passing sample root slew through global config mutation.

Recommended shape:

- Add root slew data to `FastStaClockContext`, for example `root_input_slew_ns`.
- Normal clock context builds initialize the field from `CONFIG_INST.get_root_input_slew()`.
- Char sampling sets or passes the sample root slew explicitly before calling timing update.
- `FastStaTiming::update(...)` reads the context/request value instead of reading and mutating global config per sample.

During migration, normal clock updates may still default to runtime config when the context field is unset, but char sample paths must not modify `CONFIG_INST`.

## Rename Scope

Mechanical rename:

- `FastStaAdapter.hh` -> `FastSta.hh`;
- `FastStaAdapter.cc` -> `FastSta.cc`;
- `FastStaAdapter` -> `FastSTA`;
- `FAST_STA_ADAPTER_INST` -> `FAST_STA_INST`;
- includes in characterization and optimization;
- CMake source list;
- log text.

The directory and CMake target name can remain `fast_sta` / `icts_source_database_adapter_fast_sta` in this task.

## STAAdapter Char Runtime Cleanup

Remove stale char-only runtime surface if full-repository search confirms no users:

- `initCharOnly`;
- char instance/net/pin/clock creation and destruction;
- char net graph/RC tree setup;
- char timing prepare/sample/update APIs;
- char power prepare/update/query/destroy APIs;
- private state that only supports those APIs.

Do not remove technology query APIs still used by CTS. In particular, keep or improve:

- wire RC queries;
- cap/slew limit queries;
- source drive cap query;
- pin cap/slew queries;
- root driver cost;
- buffer ports and cell area/height queries.

## `module/timing`

Leave this unchanged in this task.

The existing `module/timing/TimingEngine` is a generic RCTree estimator used by cluster constraints and QoR measurement. It is not part of this FastSTA/STAAdapter boundary refactor.

## Validation Strategy

Build direct users after the rename and legality changes:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization
```

Build broader touched modules:

```bash
ninja -C build icts_source_module_characterization icts_source_flow_synthesis icts_source_flow_evaluation
```

Run the final iCTS checker before handoff:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Run or request a representative iCTS smoke that exercises synthesis, characterization, optimization, and final QoR/STA reporting.
