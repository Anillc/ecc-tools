# EDA / CTS Research: Clock Boundary Cases

## Scope

This research supports the task to make the `cia` and
`ip1_SimpleEdgeAiSoC` CTS benchmark cases run through iCTS without SDC pin
overrides. The focus is clock tree semantics around non-buffer cells that touch
clock nets: sequential sinks, latches, integrated clock gates, discrete
clock-gating structures, generated-clock boundaries, mux-like logic, and
ordinary combinational clock loads.

## Case Evidence From This Repository

The parent task recorded the direct failure:

- `Flow::runSynthesis()` calls `Synthesis::run()` successfully, then marks CTS
  failed if `DESIGN_INST.rebuildClockDAG()` fails.
- `ClockDAG::buildBufferCellArcs()` marks the graph invalid with
  `buffer_input_pin_is_null` when an `InstType::kBuffer` has an output pin but
  no modeled input pin.
- `WrapperClockReader::inferCtsInstTypeFromIdbInst()` maps any
  `idb_inst->is_clock_instance()` to `InstType::kBuffer` after the flip-flop
  check.
- iDB `is_clock_instance()` means "any pin is connected to a clock net", not
  "this cell is a clock buffer".

Observed failing patterns:

- `cia`: `NOR2BX1H7L/AN` is an ordinary combinational input connected to
  `E_CLK`. It is a clock-net load or clock-control boundary, not a buffer.
- `ip1_SimpleEdgeAiSoC`: `LATLX0P5H7R/GN` and `AND2*/B` pins are connected to
  `clock`. The latch pin is sequential/clock-like by Liberty semantics, while
  `AND2` cells are discrete clock-gating style logic. None should become a
  simple buffer because one input touches a DEF clock net.

Relevant current code:

- `src/operation/iCTS/source/database/io/WrapperClockReader.cc`
- `src/operation/iCTS/source/database/design/ClockDAG.cc`
- `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTraceResolve.cc`
- `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTracePins.cc`
- `src/operation/iCTS/source/database/adapter/sta/clock_lookup/STAAdapterClockLookup.cc`
- `src/database/manager/parser/liberty/Lib.cc`
- `src/operation/iSTA/source/module/sta/StaBuildGraph.cc`

## Public EDA References

### CTS Buffers And Sinks

OpenROAD's CTS implementation is based on TritonCTS 2.0 and exposes explicit
configuration for clock buffer selection (`-buf_list`, `-root_buffer`,
clock-buffer footprint/string) and reports roots, inserted buffers, subnets, and
sinks after a successful run.

Implication for iCTS:

- A clock tree buffer is a selected library cell / inserted clock-tree object,
  not every inst connected to a clock net.
- Reportable CTS metrics distinguish buffers from sinks and subnets, which
  matches the need to keep `InstType::kBuffer` narrowly defined.

Source: OpenROAD CTS documentation:
https://openroad.readthedocs.io/en/latest/main/src/cts/README.html

OpenROAD Resizer documents `repair_clock_nets` as inserting buffers between the
clock input pin and the clock root buffer, and `repair_clock_inverters` as
handling clock-tree inverters with multiple fanouts.

Implication for iCTS:

- Existing physical clock nets can contain special cases before or inside the
  CTS tree. Repair flows still reason about true clock buffers/inverters, not
  arbitrary clock-net loads.
- Inverters need explicit clock-tree treatment rather than being folded into a
  generic buffer category.

Source: OpenROAD Resizer documentation:
https://openroad.readthedocs.io/en/latest/main/src/rsz/README.html

### SDC / STA Clock Semantics

OpenSTA supports SDC constraints for generated clocks, propagated/ideal clocks,
gated clock checks, multiple-frequency clocks, exception points with
`-through`, and case analysis. Its documentation describes OpenSTA as a
gate-level STA engine that consumes Verilog, Liberty, SDC, and parasitics.

Implication for iCTS:

- A robust CTS clock reader should not rely on one physical DEF net flag alone.
  SDC ownership, generated-clock declarations, and case analysis determine which
  clock paths should be traversed or stopped.
- The existing iCTS SDC trace layer already has the right direction: it parses
  `create_generated_clock` and `set_case_analysis`, traces safe transitions,
  and stops at generated-clock boundaries.

Sources:

- OpenROAD/OpenSTA documentation:
  https://openroad.readthedocs.io/en/latest/main/src/sta/README.html
- OpenSTA project documentation:
  https://github.com/The-OpenROAD-Project/OpenSTA

OpenSTA command documentation also defines clock-gating checks for data signals
that gate clocks and states that library cell function normally determines the
active state for AND/NAND or OR/NOR-style gates, with options for other cells
such as muxes.

Implication for iCTS:

- Discrete gate structures such as `clock & enable` are recognized EDA clock
  phenomena. They are clock-gating boundaries/check points, not buffers.
- Mux-like clock selection requires explicit constraints or conservative stop
  behavior when ambiguous.

Source: OpenSTA technical documentation mirror:
https://studylib.net/doc/27134936/opensta

### Liberty Clock, Sequential, And Clock-Gating Metadata

Liberty includes pin and cell metadata for clock semantics:

- pin `clock : true` marks clock pins on sequential cells;
- integrated clock-gating cells require attributes such as
  `clock_gate_clock_pin` and `clock_gate_enable_pin`;
- cell-level clock-gating metadata identifies cells intended for clock gating;
- nonintegrated clock-gating on AND/NAND/OR/NOR structures may designate one
  input as enable, with the other acting as the clock.

Implication for iCTS:

- iCTS should prefer Liberty classification over LEF `USE CLOCK` when deciding
  whether a pin is a sequential sink, ICG clock pin, gate enable, or clock-tree
  buffer port.
- The `LATLX0P5H7R/GN` pattern in `ip1_SimpleEdgeAiSoC` should be handled from
  Liberty sequential/latch semantics even if LEF marks the physical pin as
  `USE SIGNAL`.
- `AND2*/B` clock-gating style loads should be recognized as gate/boundary
  candidates, not inferred as buffer cells.

Sources:

- Liberty Reference Manual 2020.09 mirror:
  https://zao111222333.github.io/liberty-db/2020.09/reference_manual.pdf
- Liberty User Guides and Reference Manual 2013.03 mirror:
  https://studylib.net/doc/27620849/liberty13-03

### LEF/DEF `USE CLOCK`

LEF/DEF defines pin use categories including `CLOCK`; the reference describes
`USE CLOCK` as physical/netlist connectivity metadata for clock net
connectivity. DEF can also mark nets as clock nets.

Implication for iCTS:

- `USE CLOCK` is useful physical metadata but is not sufficient to infer the
  functional type of every connected inst.
- A DEF clock net can legally connect to non-buffer cells. Those connections
  may be sinks, gates, boundaries, generated-clock logic, or ordinary loads.
- Therefore `IdbInstance::is_clock_instance()` is a broad membership test and
  should not drive CTS buffer classification.

Source: LEF/DEF 5.8 Language Reference mirror:
https://www.kaixinspace.com/wp-content/uploads/2025/02/lefdefref.pdf

## iCTS Gap Analysis

### What Already Works

- `ClockTraceResolve.cc` traces through Liberty buffer/inverter cells when
  buffer ports are resolvable.
- It traces through Liberty ICG cells from clock input to outputs.
- It supports generated-clock boundary ownership with
  `create_generated_clock`.
- It supports `set_case_analysis` in safe-transition decisions.
- It rejects ambiguous mux-like clock paths without enough ownership.
- `STAAdapterClockLookup.cc` already has more precise classification than
  `WrapperClockReader`: sequential, buffer, inverter, ICG, mux, unknown.
- The local Liberty parser exposes `LibCell::isBuffer()`, `isInverter()`,
  `isSequentialCell()`, `isICG()`, `bufferPorts(...)`, and pin-level
  clock-gate attributes.

### What Is Missing

- `WrapperClockReader` does not use Liberty when projecting iDB insts into CTS
  `InstType`.
- `InstType::kInverter` exists but `ClockDAG::buildBufferCellArcs()` currently
  only handles `is_buffer()`.
- The DAG builder has no distinction between a malformed true buffer and a
  legal one-pin boundary/load.
- A read-in non-buffer cell on a clock net can be modeled with only the clock
  pin present. That is normal for a CTS sink/boundary projection, but invalid
  for a buffer arc.
- Diagnostics identify `buffer_input_pin_is_null` but not the offending inst,
  cell master, pin list, or classification reason.
- The local Liberty parser exposes sequential and clock-gating signals, but not
  a direct `isLatch()` helper. Latch support therefore needs either a
  best-effort role inferred from sequential clock/check arcs or a small Liberty
  parser extension later.

## Recommended Technical Policy

### Classification Policy

Use a single clock-boundary classification helper for iDB-to-CTS materialization:

1. Macro block: `InstType::kMacroBlock`.
2. Liberty sequential non-ICG cell: sequential sink role for CTS purposes,
   including latch-like cells. If latch-specific evidence is unavailable in the
   current API, emit `sequential_sink` rather than inventing a false FF/latch
   distinction.
3. Liberty buffer with resolvable buffer input/output ports:
   `InstType::kBuffer`.
4. Liberty inverter with resolvable input/output ports: `InstType::kInverter`.
5. Liberty ICG or pins with clock-gate attributes:
   `InstType::kClockGate`.
6. Multiple clock-net input pins / mux-like patterns: `InstType::kMux`.
7. Combinational logic whose output function depends on the clock input and
   whose output is SDC/generated-clock owned or feeds direct clock sinks:
   role `clock_derived_logic_boundary`, MVP type `kUnknown`.
8. Other clock-net-connected insts: role `clock_load_boundary` or
   `unknown_boundary`, type `kUnknown`.

The helper should also return a reason string for diagnostics, for example
`liberty_buffer`, `liberty_inverter`, `liberty_sequential`,
`liberty_clock_gate`, `multi_clock_input_mux`, `clock_derived_logic_boundary`,
`clock_load_boundary`.

Concrete logic criteria should use pin directions, Liberty function dependency,
downstream clock-sink evidence, generated-clock ownership, and case-analysis
constraints. Cell-name substring rules should not be the normal mechanism.

### DAG Policy

- Build cell arcs only for real buffer/inverter cells with complete
  input/output pins.
- Continue to mark malformed real buffer/inverter cells invalid.
- Do not require unknown, mux, clock-gate, macro, or sequential sink insts to
  have buffer input arcs.
- Count path buffer depth only across true buffer/inverter arcs. Clock gates and
  generated-clock boundaries should be reported separately until the algorithm
  intentionally models their latency/balancing semantics.

### SDC / Generated-Clock Policy

For MVP:

- Keep current generated-clock boundary behavior.
- Keep case-analysis based safe transitions.
- For cases with no explicit generated clock, allow the root CTS tree to
  legally terminate at non-buffer boundary loads and sequential sinks. Do not
  infer a new generated clock domain unless trace already accepted a downstream
  clock target.

For later follow-up:

- Add richer generated-clock modeling for discrete latch-plus-gate structures,
  including explicit ownership of generated roots and balancing/reporting by
  clock domain.
- Add user-facing diagnostics that suggest `create_generated_clock` or
  `set_case_analysis` when the tool sees ambiguous clock mux/gating paths.

## Risks

- If `InstType::kFlipFlop` is used for all Liberty sequential cells, naming may
  be semantically imprecise for latches. This is acceptable for MVP if the
  code treats it as "sequential clock sink" rather than edge-triggered-only
  behavior. A later cleanup could rename or add `kSequentialSink`.
- If inverter arcs are included in path buffer stats, existing reports may see
  a count change. The design should explicitly decide whether "buffer depth"
  includes inverters or exposes a combined clock-cell depth.
- Overly permissive unknown-boundary handling could hide unsupported generated
  clock structures. This is mitigated by structured diagnostics and by keeping
  true buffer validation strict.

## Conclusion

The two benchmark cases expose a real modeling bug, not a bad SDC workaround
problem. Production EDA semantics distinguish physical clock-net membership,
clock-tree buffers, sequential clock sinks, clock gates, generated-clock
boundaries, and mux/unknown loads. iCTS already has much of the trace-time
semantic machinery, but the final iDB-to-CTS materialization layer regresses to
a broad `is_clock_instance() -> kBuffer` heuristic. The task should replace that
heuristic with Liberty-backed classification and make `ClockDAG` robust to
legal non-buffer clock boundaries while remaining strict for malformed real
clock buffers.
