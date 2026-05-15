# CTS SDC Clock Tracing Design

## Technical Design

### Boundary

The SDC-facing implementation belongs under:

```text
src/operation/iCTS/source/database/adapter/sdc
```

This keeps the feature visible as a CTS SDC adapter instead of an STA timing feature. The adapter should produce clock-tracing declarations and provenance records for CTS read-data, not STA timing constraints.

The adapter must not:

- source the SDC into iSTA's real `ScriptEngine`
- call `STAAdapter::refreshFullDesignTimingContext()`
- build a full-design STA netlist or graph
- mutate iSTA `SdcConstrain` timing state
- disable timing arcs as a side effect of `set_case_analysis`

### Parsing Stance

Pure text syntax parsing is not enough for practical SDC because SDC is Tcl-shaped and common clock files use variables, command substitution, and expressions. For example:

```tcl
set clk_port [get_ports $clk_port_name]
create_clock -name $clk_name -period $clk_period $clk_port
```

The CTS adapter should therefore implement a side-effect-free SDC front-end:

```text
SDC file
-> lexical/command parser for Tcl-shaped SDC commands
-> minimal evaluator for variables, command substitution, list expansion, and expr
-> CTS SDC record model
-> clock trace resolver
```

This is not "execute real SDC" in the STA sense. It is a restricted, side-effect-free evaluator whose commands return typed records instead of mutating timing state.

### Components

Suggested files:

```text
database/adapter/sdc/SdcToken.hh/.cc
database/adapter/sdc/SdcParser.hh/.cc
database/adapter/sdc/SdcEvalContext.hh/.cc
database/adapter/sdc/SdcClockModel.hh
database/adapter/sdc/SdcClockReader.hh/.cc
database/adapter/sdc/SdcDiagnostics.hh/.cc
```

The existing CTS `SdcClockReader` under `database/adapter/sta` should be moved or replaced by the new adapter boundary during implementation.

### Record Model

The adapter should return CTS-owned records:

```cpp
enum class SdcObjectKind {
  kPort,
  kPin,
  kNet,
  kClock,
  kUnknown,
};

struct SdcObjectRef {
  SdcObjectKind kind;
  std::string pattern;
  bool from_collection_cmd = false;
};

struct SdcClockDecl {
  enum class Kind {
    kPrimary,
    kGenerated,
  };

  Kind kind;
  std::string clock_name;
  std::vector<SdcObjectRef> targets;
  std::vector<SdcObjectRef> generated_sources;
  std::string master_clock_name;
  double period_ns = 0.0;
  bool period_resolved = false;
  int divide_by = 1;
  int multiply_by = 1;
  bool invert = false;
  bool is_virtual = false;
};

struct SdcCaseAnalysis {
  int value = 0;
  std::vector<SdcObjectRef> objects;
};
```

### Supported Subset

MVP supported commands:

- `set`
- `expr`, limited to arithmetic needed for period expressions
- `set_units -time`
- `get_ports`
- `get_pins`
- `get_nets`
- `get_clocks`
- `all_clocks`
- `create_clock`
- `create_generated_clock`
- `set_case_analysis`

Unsupported commands should be skipped with diagnostics unless they are known non-clock constraints that do not affect CTS clock tracing.

### Clock Trace Data Flow

```text
SdcClockReader
-> vector<SdcClockDecl>, vector<SdcCaseAnalysis>
-> ClockTraceResolver over iDB + Liberty
-> vector<(sdc_clock_name, target_net_name)>
-> Wrapper::readClocks
-> one CTS Clock object per accepted target net
```

`ClockTraceResolver` may live under `database/io` or `database/adapter/sdc`; the SDC parser should not depend on iDB or Liberty. If tracing is placed in `adapter/sdc`, keep iDB/Liberty access behind a resolver class so parsing remains testable without a design database.

### Trace Target Semantics

The resolver must distinguish:

- trace-through nets: legal intermediate clock-provenance nets
- synthesis target nets: nets that should become CTS `Clock(clock_name, net_name)`

Synthesis targets are accepted only when they belong to an SDC clock and satisfy CTS clock-target features such as direct sequential CK sinks, macro clock sinks, generated-clock roots, or legal clock-gate/mux boundaries.

Multiple accepted target nets under one SDC clock are all materialized. Sink count is report evidence, not a selection score.

### Generated Clocks and Muxes

Generated clocks are ownership boundaries. Master-clock tracing stops at the generated-clock declaration boundary, and downstream targets belong to the generated clock name.

Mux traversal requires either:

- usable `set_case_analysis`
- or exactly one clock-provenance input

Otherwise the mux is reported as ambiguous and is not traversed.

## Rollout / Rollback

Rollout should preserve `use_netlist` compatibility while adding auto tracing behind a mode or default path.

Rollback is straightforward if `DesignConversion::readClockData()` keeps a fallback path that passes configured `(clock_name, net_name)` pairs directly to `Wrapper::readClocks`.

