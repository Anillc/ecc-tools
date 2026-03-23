# Cross-Layer Thinking Guide

> **Purpose**: Think through data flow across iCTS layers before implementing.

---

## The Problem

**Most bugs happen at layer boundaries**, not within layers.

Common cross-layer bugs in iCTS:

- Wrapper converts iDB coordinates to CTS `Point<int>`, but the module assumes floating-point units
- Module algorithm reads `CONFIG_INST` values that were never initialized because `CTSAPI::init()` was not called yet
- STAAdapter returns timing in nanoseconds, but the calling module computes with picoseconds
- Pointer obtained from `DESIGN_INST` is stored by the module, then Design resets and the pointer dangles

---

## iCTS Layer Architecture

```
External Tools (iDB, iSTA)
    |
    v
API Layer          CTSAPI singleton -- external entry point
    |
    v
Database Layer     Config, Design, Wrapper, STAAdapter singletons
    |
    v
Module Layer       topology, routing, characterization, timing algorithms
    |
    v
Utils Layer        logger, geometry helpers
```

**Dependency direction**: API -> Database <- Module. Module never calls API directly.

---

## Before Implementing Cross-Layer Features

### Step 1: Map the Data Flow

Draw out how data moves through the layers:

```
iDB -> Wrapper::read() -> Design (Clock/Net/Pin/Inst) -> Module algorithm -> Wrapper -> iDB output
```

For each arrow, ask:
- What type is the data in? (iDB types vs CTS types vs raw numeric)
- Who owns the memory? (`unique_ptr` owner vs raw-pointer borrower)
- What unit/coordinate system is used? (DBU, um, nm, ns, ps)

### Step 2: Identify Boundaries

| Boundary | Common Issues |
|----------|---------------|
| **API <-> Database** | Singleton initialization order; Config not parsed before Design reads it |
| **Database <-> Module** | Pointer ownership (Design owns, Module borrows); unit assumptions (DBU vs um) |
| **Wrapper <-> iDB** | Type conversion (`idb::IdbCoordinate` vs `icts::Point<int>`); null iDB objects |
| **STAAdapter <-> iSTA** | Timing unit mismatches (ns vs ps); Liberty cell name mismatches |
| **Module <-> Module** | Topology output consumed by routing; data structure compatibility |

### Step 3: Define Contracts at Each Boundary

For each boundary crossing:
- What is the exact input type and unit?
- What is the exact output type and unit?
- What happens when the input is null, empty, or out of range?
- Who logs the error -- the caller or the callee?

---

## Common Cross-Layer Mistakes

### Mistake 1: Unit and Coordinate Assumptions

**Bad**: Module assumes coordinates are in micrometers, but `Wrapper::read()` stores them in DBU (Design Base Units).

```cpp
// Bug: mixing DBU and um silently
double wire_length = point_a.get_x() - point_b.get_x();  // DBU, not um!
double resistance = STA_ADAPTER_INST.queryWireResistance(layer, wire_length);  // expects um
```

**Good**: Convert explicitly at the boundary.

```cpp
auto db_unit = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
double wire_length = static_cast<double>(distance_dbu) / db_unit;  // DBU -> um
double resistance = STA_ADAPTER_INST.queryWireResistance(layer, wire_length);
```

### Mistake 2: Pointer Ownership Across Layers

**Bad**: Module stores a raw pointer from Design, then Design resets and the pointer dangles.

```cpp
// Bug: caching a raw pointer without understanding lifetime
Pin* cached_pin = DESIGN_INST.get_clocks().front()->get_clock_source();
// ... later, after DESIGN_INST.reset() ...
cached_pin->get_name();  // dangling pointer!
```

**Good**: Either re-query after reset, or document and enforce that the module runs within Design's lifetime.

### Mistake 3: Singleton Initialization Order

**Bad**: Module code calls `CONFIG_INST.get_skew_bound()` before `CTSAPI::init()` has parsed the config file, getting the default value of `0.0`.

**Good**: `CTSAPI::init()` calls `CONFIG_INST.init()` then `WRAPPER_INST.init()` then `STA_ADAPTER_INST.init()` in strict order. Module code runs only after all singletons are initialized. Guard critical paths:

```cpp
CTS_LOG_FATAL_IF(CONFIG_INST.get_buffer_types().empty())
    << "Config not initialized: buffer_types is empty.";
```

### Mistake 4: Leaky Layer Abstractions

**Bad**: Module algorithm directly accesses `idb::IdbInstance*` by reaching through `WRAPPER_INST.get_idb()`.

**Good**: Module only works with CTS types (`icts::Inst*`, `icts::Net*`, `icts::Pin*`). All iDB access stays inside Wrapper.

### Mistake 5: Scattered Null Checks

**Bad**: Every module independently checks whether `WRAPPER_INST.get_idb()` is null.

**Good**: Validate once in the initialization path (`CTSAPI::init`), then trust the invariant inside modules. Use `CTS_LOG_FATAL_IF` at the earliest point where the null would cause damage.

---

## Data Flow Examples

### Example 1: Config -> Module

```
JSON file
  -> Config::parse() stores values in Config singleton members
    -> Module reads CONFIG_INST.get_skew_bound(), CONFIG_INST.get_buffer_types()
```

**Contract**: Config values are in user-facing units (ns for timing, um for distance). Module code must not assume DBU here.

### Example 2: iDB -> Design -> Module -> iDB

```
iDB database
  -> Wrapper::read() converts idb::IdbInstance* to icts::Inst*
    -> Design stores unique_ptr<Clock> with raw pointers to Inst/Net/Pin
      -> TopologyGen::build() reads Pin locations (in DBU)
        -> Routing produces SteinerTree
          -> Router::buildRCTree() queries STAAdapter for wire RC (converts DBU -> um)
```

**Contract**: Pin locations are always in DBU (integers). Conversion to um happens only when querying STA for electrical properties.

### Example 3: STAAdapter Round-Trip (Characterization)

```
CharBuilder reads CONFIG_INST.get_buffer_types()
  -> STAAdapter.createCharInstance() builds temporary STA instances
    -> STAAdapter.buildCharRcTree() constructs RC model
      -> STAAdapter.updateTiming() runs STA engine
        -> STAAdapter.queryCharClockAT() / queryCharSlew() return timing results (ns)
          -> CharBuilder stores results for later use by routing/buffering
            -> STAAdapter.destroyCharInstance() / destroyCharNet() cleans up
```

**Contract**: All temporary STA objects created by characterization must be destroyed in the same scope. Timing values are in nanoseconds.

---

## Checklist for Cross-Layer Features

Before implementation:
- [ ] Mapped the complete data flow across layers
- [ ] Identified all layer boundaries the feature crosses
- [ ] Documented the type and unit at each boundary
- [ ] Confirmed pointer ownership (who creates, who borrows, who destroys)
- [ ] Verified singleton initialization order is satisfied

After implementation:
- [ ] Tested with null/empty inputs at each boundary (empty loads, null pointers, zero config values)
- [ ] Verified error handling uses appropriate `CTS_LOG_*` level at each boundary
- [ ] Confirmed data survives a round-trip (e.g., CTS types written back to iDB correctly)
- [ ] No module code directly accesses iDB or iSTA types -- only through adapters

---

## When to Create Flow Documentation

Create detailed flow docs when:
- Feature spans 3+ layers (e.g., new routing algorithm that reads Config, queries STA, and writes back to iDB)
- Feature introduces a new adapter or singleton
- Data unit or coordinate system changes at a boundary
- Feature has caused pointer lifetime or initialization order bugs before
