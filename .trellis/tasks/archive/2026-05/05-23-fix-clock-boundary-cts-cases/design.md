# Design: Clock Boundary Support For Remaining CTS Benchmark Cases

## Objective

Fix `cia` and `ip1_SimpleEdgeAiSoC` by correcting iCTS clock object
classification and `ClockDAG` topology validation. The solution must make the
cases run as real CTS workloads, not by suppressing failed status or changing
SDC to internal pin overrides.

The classification should be broader than a narrow buffer/non-buffer fix:
latches and logic cells that touch clock nets need concrete CTS roles so the
clock graph can be built deliberately.

## Current Failure Path

1. SDC clock trace resolves clock target nets and `ClockDataRead` materializes
   CTS `Clock` objects.
2. `WrapperClockReader` creates CTS `Inst`/`Pin`/`Net` projections for pins on
   the selected clock nets.
3. `inferCtsInstTypeFromIdbInst()` maps:
   - macro block -> `kMacroBlock`
   - iDB flip-flop -> `kFlipFlop`
   - any iDB clock instance -> `kBuffer`
   - otherwise -> `kUnknown`
4. Non-buffer cells connected to clock nets can therefore become `kBuffer`.
5. `ClockDAG::buildBufferCellArcs()` expects every `kBuffer` to have a complete
   input/output pin model. For read-in boundary loads, only the clock-net pin is
   often modeled.
6. DAG rebuild fails with `buffer_input_pin_is_null`, and CTS is marked failed
   after synthesis.

## Architecture

### 1. Add A CTS Clock Role Classification Helper

Introduce a helper in the Wrapper/iDB adapter layer, close to
`WrapperClockReader`, because iDB pointers must not escape Wrapper.

Suggested shape:

```cpp
struct CtsInstClassification
{
  InstType type = InstType::kUnknown;
  std::string reason;
  std::string role;
  std::string input_pin_name;
  std::string output_pin_name;
};
```

The helper should inspect:

- iDB cell master block/logic status;
- iDB pin terms and connected clock nets;
- Liberty cell by cell master name through the timing engine, where available;
- Liberty buffer/inverter ports via `LibCell::bufferPorts(...)`;
- Liberty sequential and ICG status;
- Liberty pin attributes such as `isClock()` and clock-gate pin flags;
- Liberty timing arcs and output function expressions;
- number of clock-net-connected input-like pins.

The return value drives `Inst::set_type(...)` and diagnostic fields. It should
not add broad new global state.

### 2. Role Taxonomy

The code can continue to store coarse `InstType` initially, but the helper
should reason in a richer role taxonomy. The role string/reason can be emitted
to diagnostics even before a new enum is introduced.

| Role | CTS type for MVP | Meaning |
| --- | --- | --- |
| `sequential_sink` | `kFlipFlop` | Edge-triggered or otherwise Liberty sequential clock sink. |
| `latch_sink` | `kFlipFlop` initially, later `kLatch` if enum is extended | Level-sensitive sequential sink / enable-clocked latch. |
| `clock_buffer` | `kBuffer` | True one-input/one-output buffer by Liberty function. |
| `clock_inverter` | `kInverter` | True one-input/one-output inverter by Liberty function. |
| `integrated_clock_gate` | `kClockGate` | Liberty ICG or clock-gate attributes. |
| `clock_mux` | `kMux` | Multiple clock-like inputs or mux-like clock ownership ambiguity. |
| `clock_logic_boundary` | `kUnknown` initially | Combinational logic using clock as input and producing a clock-like output or control boundary. |
| `clock_load_boundary` | `kUnknown` | Combinational clock-net load that does not propagate a clock. |
| `macro_clock_sink` | `kMacroBlock` | Hard macro/block clock pin. |
| `unknown_boundary` | `kUnknown` | Insufficient Liberty/iDB/SDC evidence. |

Longer-term, adding `InstType::kLatch`, `InstType::kClockLogic`, and
`InstType::kBoundaryLoad` would make the data model cleaner. MVP can avoid enum
churn by preserving `InstType` and emitting role/reason diagnostics.

### 3. Concrete Classification Standards

Apply rules in this priority order:

1. Null or missing cell master: fail existing required-materialization checks.
2. Cell master block: `kMacroBlock`, reason `idb_macro_block`.
3. Liberty ICG: `kClockGate`, role `integrated_clock_gate`, reason
   `liberty_clock_gate`.
4. Liberty sequential non-ICG: classify as a sequential sink. If latch-specific
   evidence is available, role `latch_sink`; otherwise role `sequential_sink`,
   reason `liberty_sequential_sink`. The current local Liberty API exposes
   `isSequentialCell()` and clock/check arcs, but no direct `isLatch()` helper;
   therefore latch detection must be best-effort from available arcs/pins until
   the Liberty parser exposes explicit latch groups.
5. Liberty buffer with one resolvable input and one output:
   `kBuffer`, reason `liberty_buffer`.
6. Liberty inverter with one resolvable input and one output:
   `kInverter`, reason `liberty_inverter`.
7. Multiple clock-net-connected input-like pins: `kMux`, role `clock_mux`, reason
   `multi_clock_input_boundary`.
8. Combinational logic with exactly one clock-like input and one or more output
   pins whose Liberty function depends on that input:
   - if an output net is owned by `create_generated_clock` or accepted by clock
     trace as a downstream clock target, role `clock_logic_boundary`;
   - if all non-clock inputs are case-constrained, role
     `case_constrained_clock_logic`;
   - if the output net directly feeds sequential/macro clock sinks, role
     `clock_derived_logic_boundary`;
   - CTS type remains `kUnknown` for MVP unless the SDC trace has chosen it as a
     preclustered buffer anchor; it is not a buffer.
9. Combinational logic with a clock-net input but no clock-like output evidence:
   role `clock_load_boundary`, type `kUnknown`. This covers the `cia`
   `NOR2BX1H7L/AN` pattern.
10. iDB flip-flop fallback: `kFlipFlop`, role `sequential_sink`, reason
    `idb_flip_flop`.
11. Any remaining iDB clock instance: `kUnknown`, role `unknown_boundary`,
    reason `clock_net_boundary_load`.
12. Otherwise: `kUnknown`, role `non_clock_unknown`.

Important detail: `is_clock_instance()` may remain useful as a broad membership
signal, but it must not imply `kBuffer`.

### 4. Latch Standard

The desired standard is:

- If Liberty exposes explicit latch metadata in the future, use it directly and
  map to role `latch_sink`.
- With current APIs, classify a cell as latch/sequential sink when:
  - `LibCell::isSequentialCell()` is true and `LibCell::isICG()` is false;
  - the connected input pin is a Liberty clock pin (`LibPort::isClock()` or
    `get_is_clock_pin()`), or participates in sequential check arcs as the
    related/clock side;
  - iDB/LEF `USE CLOCK` is optional evidence, not required.
- If the cell is sequential but the specific connected pin cannot be proven to
  be the clock/latch enable pin, keep the inst sequential but emit a degraded
  diagnostic so the benchmark does not silently treat arbitrary sequential data
  pins as clock sinks.

This standard handles `LATLX0P5H7R/GN`: it should be a sequential/latch sink
when Liberty says the cell/pin is sequential-clock-like, even if LEF says
`USE SIGNAL`.

### 5. Logic Standard

Logic connected to a clock net should be split into propagation logic vs load
boundary:

- Input evidence:
  - pin direction input/inout;
  - connected net is the traced clock net or a DEF/Sdc clock net;
  - Liberty port is not a sequential clock pin and not ICG clock pin.
- Propagation evidence:
  - Liberty output function expression uses the clock input port;
  - output pin drives a net accepted by SDC trace, declared as generated clock,
    or directly feeding sequential/macro clock sinks;
  - other inputs are case-constrained or clearly non-clock controls.
- Mux evidence:
  - more than one input-like pin connected to clock-like nets;
  - output function depends on multiple clock-like inputs;
  - multiple SDC clocks can reach the same output without case analysis.
- Load-boundary evidence:
  - clock input influences normal data output, or no output clock-target
    evidence exists;
  - treat as sink/load boundary for CTS topology, not as propagation.

For MVP, logic roles should not synthesize through arbitrary logic. They should
make topology construction explicit and diagnostics clear. Full generated-clock
balancing through logic remains a follow-up unless SDC trace already owns the
downstream clock net.

### 6. Materialized Pin Policy

For a true Liberty buffer/inverter read from existing clock topology, the
projection must include the clock input and output pins when possible:

- Direct clock-net materialization already includes only pins on the selected
  net, so a pre-existing buffer whose output drives the selected net may need
  its input pin materialized to build a cell arc.
- Preclustered sink reuse already materializes anchor input/output pins.

MVP options:

- Preferred: when classification returns true buffer/inverter, use Liberty port
  names to build or find both pins if the pins exist in iDB, even if one is not
  connected to the selected target net.
- Minimum: keep strict validation for true buffers/inverters and improve
  diagnostics. The two target failures are expected to avoid this path after
  non-buffers stop being misclassified.

The preferred option reduces the chance of a follow-up failure in a case where
an existing real clock buffer is partially modeled.

### 7. ClockDAG Construction Policy

Update `ClockDAG::buildBufferCellArcs()` to operate on clock propagation cells,
not only `inst->is_buffer()`:

- Include `InstType::kBuffer`.
- Include `InstType::kInverter` if path-depth reporting should count clock
  tree inverters as clock-cell stages. If this changes report semantics, add a
  field or reason in diagnostics.
- Exclude `kFlipFlop`, `kClockGate`, `kMux`, `kMacroBlock`, and `kUnknown` from
  buffer cell arc validation.

For included propagation cells:

- Missing output pin remains invalid.
- Missing input pin remains invalid.
- Diagnostic reason should include classification, inst name, cell master, and
  modeled pin names.

For excluded boundary cells:

- Do not add synthetic cell arcs.
- They remain graph pins/loads through net arcs.
- Their presence should be reportable as boundary stats or diagnostics, not
  topology invalidation.

Future extension:

- Add explicit boundary arcs only after the role has well-defined propagation
  semantics:
  - clock buffer/inverter: normal timing propagation arc;
  - ICG: clock input to clock output under enable semantics;
  - clock mux: selected input to output only with SDC case analysis or
    generated-clock ownership;
  - arbitrary logic: generated-clock boundary, not ordinary buffer-depth arc.

### 8. Diagnostics

Add structured diagnostics through schema helpers when useful:

- During read/materialization summary:
  - count by classification type/reason;
  - count unknown clock-boundary loads;
  - optional top N examples for unknown/gate/mux boundaries.
- During DAG invalidation:
  - offending inst name;
  - cell master;
  - inst type;
  - pins currently attached to the CTS inst;
  - missing role: input or output.

This is critical because a later genuine generated-clock failure should point to
the boundary object, not just `invalid_clock_dag`.

### 9. Tests

Add focused tests instead of relying only on the full benchmark:

- `ClockDAGTest`: unknown one-pin boundary load does not invalidate topology.
- `ClockDAGTest`: malformed true buffer still invalidates topology.
- `FlowSdcTraceTest` or a new Wrapper clock-reader test:
  - direct combinational input on clock net, similar to `NOR2BX1H7L/AN`, is
    classified unknown/boundary and does not become buffer;
  - latch/sequential Liberty clock pin on a clock net is classified as
    sequential sink even if LEF pin use is signal;
  - logic cell whose output function uses the clock input but whose output does
    not feed a clock target is a load boundary;
  - logic cell whose output function uses the clock input and whose output feeds
    clock sinks is a clock-derived logic boundary, not a buffer;
  - optional ICG/discrete clock-gate pattern is classified as clock gate or
    boundary and does not become buffer.

Benchmark validation remains the integration proof.

## Compatibility Notes

- Existing `InstType::kFlipFlop` name is historical. For MVP, use it as
  "sequential clock sink" to avoid invasive enum/data-model changes.
- Existing `STAAdapterClockLookup` already uses Liberty-backed classification.
  Reuse its logic direction, but keep iDB access in Wrapper and avoid creating
  a new cross-layer dependency from Wrapper to STAAdapter unless it follows
  existing adapter boundaries cleanly.
- Existing SDC trace behavior for generated clocks, case analysis, and mux
  ambiguity should remain unchanged unless tests show it contributes directly
  to the two cases.

## Rollout Strategy

1. Add classification helper and tests around the known failure class.
2. Add `ClockDAG` robustness/diagnostics.
3. Run targeted unit/flow tests.
4. Build release and run `cia`/`ip1_SimpleEdgeAiSoC`.
5. Run the benchmark set and collect CSV/logs.
6. If new failures appear, triage by new typed reason instead of expanding this
   task into unrelated CTS algorithm work.

## Rollback Strategy

- Revert the classification helper and `ClockDAG` changes as a small unit if
  common cases regress.
- Keep tests that encode the failure root cause if the implementation shape
  changes, because they describe the desired behavior independently from the
  chosen helper location.

## Key Trade-Off

MVP should treat unsupported generated/gated clock structures as explicit
boundaries unless SDC declares and trace accepts them. This is less ambitious
than full generated-clock CTS balancing, but it is the correct first step:
classification must be fixed before CTS can safely synthesize or balance those
domains.

The user asked whether CTS type判定 can become more comprehensive directly. The
answer is yes: implement role classification now, but keep propagation through
latch/logic conservative until SDC and ClockDAG semantics are precise enough to
avoid false clock trees.
