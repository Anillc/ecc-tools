# CTS STA clock semantics and robustness cleanup

## Goal

Improve CTS robustness around setup, clock discovery, synthesis status, iDB writeback, and final evaluation, while adding a minimal STA clock-only SDC interface so CTS can use real SDC clock periods without requiring a full DB-backed STA timing context.

The central semantic fix is to stop treating an iDB clock net name as a clock definition. CTS clock identity and clock existence must come from SDC. iDB may be used to resolve an SDC clock source to the physical net CTS needs to modify, but it must not be used as a fallback clock discovery source.

## What I already know

- Current CTS default clock discovery uses iDB clock nets and creates pairs as `(net_name, net_name)`.
- Existing CTS code already has a TODO warning that clock net name may differ from clock name and should be queried from STA.
- `Config::net_list` can preserve `(clock_name, net_name)`, but it is manual, optional, and can drift from SDC/iDB. It should not define clock existence unless the clock is also present in SDC.
- Current synthesis success is `failed_clocks == 0`, so `0 clocks` or `all clocks skipped` can be reported as success.
- User decision: `0 clock` and `all skipped` must not be success; CTS must explicitly report no-op.
- User decision: STA should expose a minimal `readSdcClockPeriodsOnly()` interface.
- User decision: iDB writeback failure must rollback partial iDB changes.
- STA normal `readSdc()` is not enough for no-DB/no-netlist period extraction because clock/object commands resolve design objects and can fatal when objects are absent.

## Requirements

### 1. Config setup failure is explicit

Missing or unreadable CTS config must be a setup failure, not silent fallback to defaults.

Proposed behavior:
- Make `Config::init/parse` return a structured status or throw/abort through the established CTS setup error mechanism.
- Report the failing path and reason in setup/runtime logs.
- Do not proceed to readData/synthesis when config loading failed.

Acceptance:
- Missing config file causes CTS setup/API run to fail.
- Runtime report shows `status=failed` with config path.
- Existing valid default config still runs.

### 2. No-op CTS run is explicit and not success

`0 clocks` and `all clocks skipped` must be represented as no-op, not as successful CTS.

Proposed behavior:
- Add a synthesis outcome enum/status, for example `finished`, `failed`, `no_op`.
- Define success as `successful_clocks > 0 && failed_clocks == 0`.
- Define no-op as `total_clocks == 0 || (successful_clocks == 0 && skipped_clocks > 0 && failed_clocks == 0)`.
- Emit a clear reason such as `no_clocks_discovered` or `all_clocks_skipped`.
- Keep skipped per-domain rows, but propagate no-op to main CTS result and prevent iDB instantiation/evaluation from pretending final QoR is available.

Acceptance:
- `0 clocks` reports `no_op`, not `finished`.
- All skipped clocks report `no_op`, not `finished`.
- API summary and `CTS Key Results` are aligned.

### 3. iDB writeback is rollback-safe

iDB projection currently rewrites clocks incrementally. If a later net or inst conversion fails, earlier iDB mutations can remain.

Proposed behavior:
- Add a write transaction/snapshot around CTS-owned iDB changes.
- Preflight all required iDB insts/nets/pins before mutation where possible.
- On failure, restore all touched nets' driver/load pin lists and remove newly created CTS-owned iDB instances/nets.
- Return structured failure detail from `writeClock/writeClocks`, including failed clock/net and rollback status.

Acceptance:
- Simulated write failure leaves iDB identical to pre-write snapshot for touched clocks.
- Failed write marks instantiation failed and blocks final STA refresh.
- Logs state whether rollback succeeded.

### 4. Clock read pin indexing is transactional

During `Wrapper::readClock()`, pins are attached to CTS insts before `DESIGN_INST.indexPin(cts_pin)` succeeds. Duplicate/shared net situations can leave unindexed pins attached to instances.

Proposed behavior:
- Read/build pins for one clock in a local staging structure.
- Only attach pins to inst/net/clock and update indexes after all index prechecks pass.
- If a pin cannot be indexed, rollback staged attachments for that clock and mark the clock conversion failed.

Acceptance:
- Duplicate pin names or shared clock net reads do not leave orphan/unindexed pins in CTS insts.
- Failed clock conversion has a diagnostic row and does not create a partially valid `Clock`.

### 5. STA exposes `readSdcClockPeriodsOnly()`

H-tree root-driver compensation currently uses a hardcoded 10 ns period. CTS needs the SDC period without requiring full DB/graph timing.

Proposed behavior:
- Add minimal STA API `readSdcClockPeriodsOnly(path)` returning clock period records in ns.
- Use Tcl command parsing semantics, not regex-only SDC parsing.
- Register clock-only variants for `set_units`, `create_clock`, `create_generated_clock`, `get_clocks`, `all_clocks`, and safe source-object passthrough commands needed by common clock SDC.
- `create_clock` records `-name`, `-period`, waveform, and unresolved source expressions without `DesignObject*` resolution.
- `create_generated_clock` computes period when its master/source clock is known; otherwise records unresolved diagnostics.
- CTS root-driver compensation should use the matched SDC period, then fallback in order to configured period if added later, then conservative default with explicit provenance.

Acceptance:
- SDC with `set_units -time ps` returns periods converted to ns.
- SDC with `create_clock -name CLK -period 1000 [get_ports clk]` can return `CLK=1.0ns` without loaded DB/netlist.
- Unresolved generated clocks are reported as unresolved, not silently treated as 0 or 10 ns.
- H-tree report shows clock period source.

### 6. QoR availability is separated from estimated metrics

Failed/no-op runs can still expose seemingly valid final QoR fields initialized from CTS internal estimates or zeros.

Proposed behavior:
- Add explicit metric source/status fields: `unavailable`, `estimated_cts`, `final_sta`, `final_idb`.
- Only report final STA timing/QoR after successful synthesis, successful iDB writeback, and successful STA refresh.
- Keep CTS internal estimates visible, but label them as estimates.

Acceptance:
- No-op and failed instantiation do not expose final timing/QoR as valid.
- Report tables distinguish CTS estimate from final STA/iDB data.

### 7. Recoverable synthesis data inconsistencies do not fatal the process

Several H-tree/topology paths use `LOG_FATAL_IF` for conditions that can be reported as per-clock/per-domain failures.

Proposed behavior:
- Classify fatal vs recoverable invariants.
- Convert recoverable candidate/domain inconsistencies into structured `failure_reason`.
- Preserve true internal corruption as fatal only when continuing would corrupt memory or iDB.

Acceptance:
- Bad per-clock data fails that clock/domain and lets remaining clocks proceed where safe.
- CTS summary captures failed clock/domain reason.
- Unit tests cover at least one formerly fatal recoverable path.

### 8. Bool config invalid strings are handled safely

Current string bool parsing returns false for any unrecognized string, which can silently flip enabled features off.

Proposed behavior:
- Parse only known true/false tokens.
- For invalid tokens, either fail config validation or preserve the default and emit a warning; this task should prefer fail-fast for setup-critical options.
- Report offending key/value/path.

Acceptance:
- `"use_netlist": "maybe"` does not silently become false.
- Tests cover valid booleans, numeric booleans, valid string tokens, and invalid strings.

### 9. Clock judgement and parsing uses SDC as the only clock source

The current default path uses iDB clock net names as clock names. That loses logical clock identity and can mismatch STA clock names, especially when SDC `create_clock -name` differs from physical net name or multiple nets belong to one logical clock.

Proposed behavior:
- Do not add a new CTS clock discovery data structure for this task. Keep the existing `(clock_name, net_name)` flow into `Wrapper::readClocks()`, but change how the pairs are produced.
- Replace the default `Wrapper::collectClockNetPairs()` iDB-clock-net path with an SDC-driven path.
- Use STA SDC clocks as the only source of clock existence and clock name.
- Resolve each SDC clock source object to a physical iDB net only as a validation/materialization step:
  - full STA context available: resolve `SdcClock` source objects to net/pin/port and then to the iDB net;
  - clock-only SDC context: keep source expression text and resolve it against iDB only when it directly names a resolvable port/pin/net;
  - `Config::net_list`: allowed only as an explicit physical net mapping for SDC-declared clock names, not as an independent source of clocks.
- If an SDC clock cannot be mapped to exactly one physical iDB net, fail that clock/readData stage with `unresolved_sdc_clock_source` or `ambiguous_sdc_clock_source`.
- If SDC is missing or contains no clocks, report no-op/failure according to the no-op policy; do not fallback to iDB `is_clock()` or `dmInst->getClockNetList()`.
- iDB clock flags can be used only for diagnostics, such as warning that an SDC-mapped clock net is not marked clock in iDB, or that iDB has clock nets absent from SDC.

Acceptance:
- Clock name can differ from net name without losing period or report identity.
- ReadData reports `clock_source=sdc`; no clock is created from iDB clock-net fallback.
- Ambiguous or unmapped SDC-only clocks do not synthesize silently against a guessed net.
- Config `net_list` entries whose `clock_name` does not exist in SDC are rejected or ignored with a hard diagnostic, according to setup policy.

## Technical notes

- `src/operation/iCTS/source/database/io/Wrapper.cc:200` collects iDB clock net pairs.
- `src/operation/iCTS/source/database/io/Wrapper.cc:218` emits `(net_name, net_name)`.
- `src/operation/iCTS/source/database/io/Wrapper.cc:230` TODO states clock net name may differ from clock name.
- `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.cc:300` chooses config `net_list` vs iDB clock net discovery.
- `src/operation/iCTS/source/database/config/Config.cc:193` parses configured `(clock_name, net_name)`.
- `src/operation/iCTS/source/flow/synthesis/Synthesis.cc:212` currently treats `failed_clocks == 0` as success.
- `src/operation/iCTS/source/database/io/Wrapper.cc:633` writes one clock to iDB incrementally.
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:71` hardcodes root-driver compensation period to 10 ns.
- `src/operation/iSTA/source/module/sta/Sta.cc:180` normal SDC reader.
- `src/operation/iSTA/source/module/sdc-cmd/CmdCreateClock.cc:78` normal `create_clock` execution resolves source objects.
- `src/operation/iSTA/source/module/sdc/SdcClock.hh:40` current SDC clock data model.
- Research note: `.trellis/tasks/05-06-cts-sta-clock-semantics-robustness/research/sta-clock-sdc-no-db.md`.

## Out of scope

- Full CTS architecture rewrite.
- Replacing all iDB access in CTS. iDB is still needed to materialize SDC-declared clocks onto physical nets.
- Implementing complete PrimeTime SDC compatibility.
- Inferring physical clock net from arbitrary SDC text without a resolvable source object or explicit SDC-validated mapping.

## Definition of Done

- The nine requirements above are implemented or explicitly split into follow-up tasks with rationale.
- Unit/integration tests cover config failures, no-op synthesis, rollback, clock-only SDC period extraction, and clock name/net name mismatch.
- CTS reports expose status/provenance clearly.
- Existing valid CTS flows remain compatible.
