# Implementation Plan

## Preconditions

- Do not start implementation until the user reviews and approves this plan.
- Keep benchmark SDC port-based. Do not add `get_pins` overrides.
- Follow iCTS backend specs:
  - no exceptions;
  - use `LOG_*` and schema diagnostics;
  - keep iDB access inside Wrapper;
  - do not use `ecc_dev_tools` as the normal edit loop;
  - run one full `src/operation/iCTS` check before final handoff.

## Files To Inspect Or Modify

- `src/operation/iCTS/source/database/io/WrapperClockReader.cc`
- `src/operation/iCTS/source/database/design/ClockDAG.cc`
- `src/operation/iCTS/source/database/design/Inst.hh`
- `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTracePins.cc`
- `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTraceResolve.cc`
- `src/operation/iCTS/source/database/adapter/sta/clock_lookup/STAAdapterClockLookup.cc`
- `src/operation/iCTS/test/database/design/ClockDAGTest.cc`
- `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc`
- test fixtures under `src/operation/iCTS/test/flow/`

## Ordered Work

1. Create CTS role classification helper in `WrapperClockReader.cc`.
   - Resolve Liberty cell from cell master name.
   - Resolve Liberty ports to iDB pins by term name.
   - Return `InstType`, role, reason string, and optional propagation pin names.
   - Replace `is_clock_instance() -> kBuffer` with the ordered policy from
     `design.md`.

2. Implement concrete latch/sequential detection.
   - Use `LibCell::isSequentialCell()` and exclude `LibCell::isICG()`.
   - Prefer connected Liberty clock pins (`LibPort::isClock()`,
     `get_is_clock_pin()`) when deciding whether a specific pin is a clock/latch
     sink.
   - Record role `latch_sink` only when latch-specific evidence is available;
     otherwise use `sequential_sink` and keep a diagnostic reason.
   - Do not require LEF `USE CLOCK`.

3. Implement concrete logic classification.
   - Count clock-like input pins.
   - Inspect output pins and Liberty output function dependency on the clock
     input when function expressions are available.
   - Use SDC trace/generation ownership, direct downstream clock sinks, and
     case-analysis constraints to distinguish clock-derived logic boundary from
     ordinary clock load boundary.
   - Classify mux-like multi-clock-input logic as `kMux`.
   - Keep logic out of `kBuffer`.

4. Materialize missing true buffer/inverter pins where needed.
   - For Liberty buffer/inverter insts, attempt to build/find both buffer ports.
   - Preserve current duplicate pin/index safeguards.
   - Do not materialize arbitrary data pins for unknown/gate/mux boundaries.

5. Update `ClockDAG`.
   - Treat only true clock propagation cells as requiring input/output arcs.
   - Keep malformed true buffer/inverter strict.
   - Exclude unknown, mux, clock gate, macro, and sequential sink insts from
     buffer arc validation.
   - Add richer invalid-topology diagnostics.

6. Add classification/boundary diagnostics.
   - Emit summary counts by type/reason.
   - Emit examples for unknown clock-boundary loads at warning or report level.
   - Include latch/logic roles in the report so `cia` and
     `ip1_SimpleEdgeAiSoC` can be audited after benchmark rerun.
   - Ensure logs remain concise on large designs such as `ip1_SimpleEdgeAiSoC`.

7. Add tests.
   - Unknown one-pin boundary load does not invalidate `ClockDAG`.
   - Malformed true buffer does invalidate `ClockDAG`.
   - Direct combinational clock-net load is not classified as buffer.
   - Liberty sequential/latch-like clock sink is not classified as buffer.
   - Logic with clock-dependent output but no downstream clock sinks is a
     load boundary.
   - Logic with clock-dependent output and downstream clock sinks is a
     clock-derived logic boundary.
   - Optional: ICG/discrete gate-like boundary is classified as clock gate or
     unknown boundary and does not break DAG rebuild.

8. Run targeted validation.
   - Build only the needed test target if available.
   - Run `ClockDAGTest`.
   - Run SDC trace/flow clock tests.

9. Build release.
   - Use the repository's current release build path from the parent benchmark
     task.
   - Confirm the binary used by the benchmark is the new release binary.

10. Run targeted benchmark cases.
   - `cia`
   - `ip1_SimpleEdgeAiSoC`
   - Force rerun to avoid stale artifacts.

11. Run full benchmark if targeted cases pass.
   - Confirm CSV output omits power columns as configured in the parent task.
   - Confirm `icts.def` and `icts.v` exist for all finished cases.

12. Final validation and handoff.
   - Run final full `src/operation/iCTS` quality check.
   - Summarize changed semantics and benchmark result.

## Validation Commands

Use exact commands after implementation, adjusted only if build paths differ:

```bash
python3 -m py_compile scripts/design/ics55_cts_bench/tools/*.py
```

```bash
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py \
  --case cia \
  --case ip1_SimpleEdgeAiSoC \
  --force \
  --timeout 3600
```

```bash
python3 scripts/design/ics55_cts_bench/tools/collect_cts_metrics.py
```

```bash
rg -n "buffer_input_pin_is_null|invalid_clock_dag|CTS Key Results|\\| status" \
  /nfs/share/home/liweiguo/run_benchmark/run_pl2icts/cia/cts/cts.log \
  /nfs/share/home/liweiguo/run_benchmark/run_pl2icts/ip1_SimpleEdgeAiSoC/cts/cts.log
```

Final handoff check:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Expected Result

- `cia` and `ip1_SimpleEdgeAiSoC` stop failing with
  `buffer_input_pin_is_null`.
- Both cases finish CTS with valid `icts.def` and `icts.v`.
- Full deep benchmark target is 91/91 finished.
- If a later failure appears, it has a new typed diagnostic that identifies the
  unsupported clock boundary or generated-clock structure.

## Review Gate

Before `task.py start`, confirm the MVP boundary policy:

- Recommended: implement comprehensive role classification for latch and logic
  now, but let unsupported discrete generated-clock propagation become explicit
  CTS boundaries unless SDC declares a generated clock and trace accepts it.
- Alternative: require full generated/gated clock balancing in this task. That
  is higher risk and should be split into a second child task after the two
  benchmark cases are unblocked.
