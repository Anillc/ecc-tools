# 修复 clock-boundary CTS benchmark cases

## Goal

Make the remaining deep benchmark CTS failures (`cia` and
`ip1_SimpleEdgeAiSoC`) complete as real CTS runs, with meaningful `icts.def`
and `icts.v` outputs, by adding correct support for clock-boundary cells and
clock-net-connected non-buffer objects.

The goal is not to hide the failure status or patch SDC with pin overrides. The
tool must stop treating every iDB "clock instance" as a CTS buffer and must
model clock sinks, clock gates, generated/gated clock boundaries, and unknown
clock-net loads in a way that does not corrupt the clock topology.

## User Value

- The deep CTS benchmark should reach 91/91 finished cases without manual SDC
  pin overrides.
- The benchmark output should remain useful for CTS algorithm evaluation:
  inserted clock tree DEF/Verilog should be generated from valid topology, not
  from suppressed diagnostics.
- Future large designs with latch-based or combinational clock-gating structures
  should fail with actionable diagnostics only when the constraints/library data
  are genuinely insufficient.

## Confirmed Facts

- Parent task: `.trellis/tasks/05-22-deep-benchmark-test`.
- Current benchmark state after SDC cleanup: 89 cases finished, 2 cases fail in
  CTS status.
- Remaining cases:
  - `cia`
  - `ip1_SimpleEdgeAiSoC`
- Both cases pass ReadData with port-based SDC and produce `icts.def` and
  `icts.v`, but CTS is marked failed after synthesis.
- Direct failure reason in both logs: `buffer_input_pin_is_null` during
  post-synthesis `Design::rebuildClockDAG()`.
- `ClockDAG::buildBufferCellArcs()` assumes every `InstType::kBuffer` has a
  driver pin and at least one input pin.
- `WrapperClockReader::inferCtsInstTypeFromIdbInst()` currently maps
  `idb_inst->is_clock_instance()` to `InstType::kBuffer` after the flip-flop
  check.
- `IdbInstance::is_clock_instance()` is broad: it returns true when any inst pin
  is connected to a clock net.
- `cia` has an ordinary combinational `NOR2BX1H7L/AN` input connected to the
  root clock net `E_CLK`.
- `ip1_SimpleEdgeAiSoC` has latch and discrete clock-gating style loads on the
  root clock net, including `LATLX0P5H7R/GN` and `AND2*/B`.
- Existing SDC trace code already recognizes some safe transitions through
  Liberty buffer/inverter cells, ICG cells, case-constrained gates, generated
  clock boundaries, and ambiguous mux-like clock paths. The later clock
  materialization/classification layer is less precise than the trace layer.
- Existing benchmark policy from the parent task remains in force: benchmark
  SDC must not use `get_pins` pin override to paper over these failures.

## Requirements

- R1: Preserve the no-pin-override policy. The fix must not require changing
  benchmark SDC from clock ports to internal pins.
- R2: Classify CTS inst types from explicit role standards rather than a single
  clock-net-membership heuristic. The role set must cover at least:
  flip-flop/sequential sinks, latch/sequential-level-sensitive sinks when
  detectable, buffer, inverter, ICG/clock gate, macro block, mux-like
  multi-clock-input cells, clock-derived logic/boundary logic, and unknown
  boundary/load.
- R3: Do not classify arbitrary clock-net-connected logic as `InstType::kBuffer`
  solely because iDB marks it as a clock instance.
- R4: Only true buffer/inverter cells with resolvable input and output pins may
  participate in buffer-cell arc construction and path buffer-depth accounting.
- R5: Sequential clock sinks must include Liberty sequential/latch clock pins
  even when LEF does not mark the pin as `USE CLOCK`. If the local Liberty API
  cannot distinguish latch from flip-flop, classify both as a sequential sink
  and record the detection basis.
- R6: ICG and discrete clock-gating / clock-derived logic structures must be
  represented as clock gates, generated-clock boundaries, mux-like boundaries,
  logic boundaries, or unknown boundary loads rather than simple buffers.
- R6a: Logic classification must use concrete criteria: input/output direction,
  Liberty output function dependency on the clock input, number of clock-like
  inputs, whether output nets feed clock sinks, ICG attributes, and SDC
  generated-clock / case-analysis ownership. It must not be based on cell-name
  substring matching except as a last-resort diagnostic hint.
- R7: `ClockDAG` must remain strict for malformed real buffers/inverters, but
  a one-pin unknown boundary/load must not invalidate the whole DAG with
  `buffer_input_pin_is_null`.
- R8: Add structured diagnostics/reporting for skipped or boundary clock objects:
  inst name, cell master, pin, net, classification, and reason.
- R9: Existing passing benchmark cases and existing SDC trace behavior must not
  regress.
- R10: The implementation must follow iCTS backend constraints: no exceptions,
  LOG/schema diagnostics for runtime issues, iDB access inside Wrapper, and
  final full `src/operation/iCTS` quality validation before handoff.

## Acceptance Criteria

- [ ] Unit coverage proves that non-buffer clock-boundary loads do not invalidate
      `ClockDAG` as `buffer_input_pin_is_null`.
- [ ] Unit coverage proves that a malformed true buffer/inverter still produces
      a clear invalid-buffer diagnostic rather than being silently ignored.
- [ ] Flow coverage exercises a direct combinational clock-net load similar to
      `cia` and confirms CTS clock read plus DAG rebuild succeed.
- [ ] Flow coverage exercises a latch/discrete-gate clock-boundary pattern
      similar to `ip1_SimpleEdgeAiSoC` and confirms CTS clock read plus DAG
      rebuild succeed or emits a typed, actionable unsupported-boundary error.
- [ ] `cia` and `ip1_SimpleEdgeAiSoC` no longer fail with
      `buffer_input_pin_is_null`.
- [ ] `cia` and `ip1_SimpleEdgeAiSoC` produce `icts.def` and `icts.v` under
      `/nfs/share/home/liweiguo/run_benchmark/run_pl2icts/${case}/`.
- [ ] Target benchmark result is 91/91 finished. If another genuine failure
      appears after this fix, the failure must have a new, typed reason and
      artifacts sufficient for root-cause analysis.
- [ ] Existing iCTS tests covering SDC clock trace, generated boundaries, mux
      ambiguity, and ClockDAG path buffer stats still pass.
- [ ] Final full check for `src/operation/iCTS` is run before handoff.

## Out Of Scope

- Reintroducing power evaluation into the fast benchmark path.
- Changing benchmark case staging paths or restoring local
  `scripts/design/ics55_cts_bench/cases`.
- Adding SDC pin overrides to individual benchmark cases.
- Implementing full signoff-quality generated-clock balancing for every possible
  gated/divided/muxed clock topology in one step.
- Changing iSTA or external modules except for minimal adapter calls that are
  required for classification.

## Open Scope Decision

- Recommended MVP: first make classification and `ClockDAG` robust enough that
  `cia` and `ip1_SimpleEdgeAiSoC` finish with valid topology, while treating
  unsupported discrete generated-clock branches as explicit boundaries. Full
  balancing across generated/gated clock domains should be a follow-up after
  the benchmark is green.
- User preference update: make the CTS type/role判定 more complete directly,
  especially for latch and logic cells, using concrete classification standards
  that can later drive ClockDAG construction rather than collapsing everything
  into unknown.

## Planning Artifacts

- `research/eda_cts_clock_boundaries.md`
- `design.md`
- `implement.md`
