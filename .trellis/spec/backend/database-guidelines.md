# Database Guidelines

Ownership, singleton boundaries, and data-model rules for iCTS.

## Scope

This document covers singleton roles, ownership, lifetime, database-layer placement, and new data classes.

Naming and generic accessor style live in `quality-guidelines.md`.

## Rules

### Singleton Roles

Use the existing singleton boundaries:

| Macro | Role |
|-------|------|
| `CTS_API_INST` | External API entry point |
| `DESIGN_INST` | Design database |
| `CONFIG_INST` | Configuration |
| `WRAPPER_INST` | iDB adapter |
| `STA_ADAPTER_INST` | iSTA adapter for internal source-layer use |
| `LOG_INST` | Logger |

Rules:
- External callers enter through `CTS_API_INST`.
- Module code should use narrowed singletons such as `CONFIG_INST`, `DESIGN_INST`, `WRAPPER_INST`, and `STA_ADAPTER_INST`.
- Do not introduce new singleton boundaries without a clear cross-module need.

### Ownership

- Use `std::unique_ptr` for ownership.
- Use raw pointers only for non-owning cross-references.
- `Design` owns `Clock` objects.
- `Wrapper` owns CTS-side `Pin`, `Inst`, and `Net` objects created from iDB.
- `Tree` owns `TreeNode` objects.
- Borrowed pointers must not outlive the owner.
- Do not cache borrowed pointers across owner reset boundaries.

### Placement

Put new types in the narrowest database subdirectory that matches their role:
- config types -> `source/database/config/`
- design objects -> `source/database/design/`
- iDB adapter code -> `source/database/io/`
- iSTA adapter code -> `source/database/adapter/sta/`
- spatial types -> `source/database/spatial/`
- routing DB types -> `source/database/routing/`
- timing DB types -> `source/database/timing/`

If a type is shared across modules and is part of the stable data model, prefer `source/database/` over `source/module/`.

### Access Boundaries

- Validate critical singleton state at initialization boundaries.
- Avoid scattering the same null-check pattern across modules.
- Keep iDB access inside `Wrapper`.
- Keep iSTA access inside `STAAdapter`.
- Module code should operate on CTS types, not external-tool types.

### Scenario: Multi-Design Real-Tech Placement Benchmarks

#### 1. Scope / Trigger

- Trigger: a test or benchmark repeatedly loads placement-stage DEF/Verilog designs and compares CTS module behavior across many cases.
- This is cross-layer because it touches `dmInst`, `CONFIG_INST`, `DESIGN_INST`, `WRAPPER_INST`, and optionally `STA_ADAPTER_INST`.

#### 2. Signatures

- Case input contract: `place_dreamplace/output/*_place.def` or `*_place.def.gz` plus matching `*_place.v`.
- CTS-side load contract: create a `Clock` in `DESIGN_INST`, call `WRAPPER_INST.read()`, then consume `Clock::get_loads()`.
- Slow benchmark targets should be gated by `ICTS_BUILD_SLOW_REALTECH_TESTS`.

#### 3. Contracts

- Benchmark design inputs must come only from placement DEF/Verilog paths selected by the test.
- Technology/config inputs may come from the allowed PDK/workspace paths or env overrides such as `ICTS_REALTECH_PDK_DIR`, `PDK_DIR`, and `ICTS_TEST_OUTPUT_DIR`.
- Do not read stage-local benchmark configs when the test is intended to isolate placement data.
- For heterogeneous top modules, derive clock/load net candidates from loaded iDB net metadata, net names, flip-flop clock pins, and fanout. Do not require a generic SDC clock name to match every design.

#### 4. Validation & Error Matrix

- Benchmark root missing -> write a skip reason and `GTEST_SKIP()`.
- DEF/Verilog top mismatch -> fail the case with the mismatched names.
- Required PDK/config file missing -> fail before running module comparisons.
- No candidate clock/load net -> fail the case with the selection reason.
- Algorithm output load count differs from input load count -> mark the result illegal.

#### 5. Good/Base/Bad Cases

- Good: load Verilog + placement DEF, verify DEF design equals Verilog top, add the selected clock net to `DESIGN_INST`, call `WRAPPER_INST.read()`, then compare algorithms on the same `std::vector<Pin*>`.
- Base: use `STA_ADAPTER_INST.init()` only for adapter setup when needed, without calling timing updates for clock discovery.
- Bad: call `STA_ADAPTER_INST.updateTiming()` with a generic SDC such as `get_ports clk` across unrelated designs; many tops do not expose that port name.

#### 6. Tests Required

- Assert the selected case count when the external root exists.
- Log per-case DBU, inst count, net count, selected clock net, load count, and span diameter before algorithm comparison.
- CSV output must include expected load count, actual load count, missing load count, legality, runtime, score, and violation counts.
- Aggregate acceptance must require both runtime and score wins when that is the benchmark's stated goal.

#### 7. Wrong vs Correct

Wrong:

```cpp
STA_ADAPTER_INST.updateTiming();
auto clocks = STA_ADAPTER_INST.collectClockNetPairs();
```

Correct:

```cpp
DESIGN_INST.add_clock(std::make_unique<Clock>(clock_name, idb_net_name));
WRAPPER_INST.read();
auto loads = DESIGN_INST.get_clocks().front()->get_loads();
```

### Adding New Data Classes

When adding a new database-layer type:
1. Place it under the correct `source/database/` subdirectory.
2. Use `enum class` for enums.
3. Initialize members with sensible defaults.
4. Use an `INTERFACE` target if the type is header-only.
5. Add a real library target only when `.cc` implementation is needed.
6. Document any non-trivial ownership rule.

### Singleton Implementation

When a singleton is justified:
- use the existing Meyers Singleton pattern
- delete copy and move operations
- expose access through the established macro alias
- keep initialization order controlled by the existing API/setup flow

Do not introduce ad-hoc global state outside this pattern.

## Checklist

Before handoff, verify:

- [ ] Ownership is explicit and minimal
- [ ] Borrowed pointers do not outlive their owners
- [ ] New data types live in the correct database subdirectory
- [ ] External-tool access stays inside adapter layers
- [ ] Header-only database types use `INTERFACE` targets when appropriate
- [ ] New singleton usage is truly cross-module and justified

## Related Docs

- `directory-structure.md`
- `quality-guidelines.md`
- `../guides/cross-layer-thinking-guide.md`
