# Coding Constraints — iCTS Characterization Task

> **Scope**: This document applies ONLY to the `02-27-cts-characterization` task.
> **Status**: Active — must be followed by all agents working on this task.

---

## 1. No External Project References

**Constraint**: No source code (`.cc`, `.hh`, comments, strings) may contain branded names or project identifiers copied from an external reference implementation.

- Variable names, class names, comments, and log messages must not reference external project names
- Use generic terms: "reference implementation", "upstream CTS", or simply omit

**Rationale**: Licensing and branding separation.

---

## 2. No New Data Structures in CTSAPI

**Constraint**: Do NOT add new `struct`, `class`, `enum`, or type aliases inside `CTSAPI.hh` / `CTSAPI.cc`.

- Use existing iDB / iSTA / iPA data structures
- Use existing iCTS database objects (`CTSDesignInst`, `CTSConfigInst`, etc.)
- Helper types for the characterization module itself go in `source/module/characterization/`

**Rationale**: CTSAPI is a stable interface layer — it wraps calls to lower-level tools but should not accumulate domain-specific data types.

---

## 3. Real Nets with Clock-Based Timing via iSTA

**Constraint**: Build **real temporary nets** through iDB via `TimingIDBAdapter`, construct RC trees,
and use iSTA's **clock propagation** mechanism for automatic timing computation.

- Do NOT use iSTA's virtual RC tree API
- Do NOT manually sum individual arc/net delays — rely on `getClockAT` for total delay
- Temporary nets/instances should be cleaned up after characterization
- Use prefix `cts_char_` for all temporary object names to avoid collision with design

### Characterization Circuit Topology

The characterization circuit is a clock-path chain with source/sink buffers as endpoints:

```
[source_buf/out] --(clock source)--> net0(RC) --> [buf1/in → buf1/out] --> net1(RC) --> ... --> [sink_buf/in]
                                                                                                 ↑ measurement point
```

- **Source buffer**: Drives the chain. A clock is defined on its output pin → `getClockAT` starts from 0 here.
- **Sink buffer**: Terminates the chain. Its input pin is the measurement point for `getClockAT`.
- **Wire segments**: Variable length based on buffer positions (NOT equal division).

### STA Flow (through CTSAPI wrappers)

```
1. Create source/sink buffer instances + characterization buffers + nets
   → TimingIDBAdapter::createInstance() / createNet() / attach()

2. Build RC tree for each net (Pi-model)
   → STAInst->initRcTree(net)
   → STAInst->makeOrFindRCTreeNode()
   → STAInst->makeResistor() + incrCap()
   → STAInst->updateRCTreeInfo(net)  // computes Elmore delay automatically

3. Create a propagated clock on the source buffer's output pin
   → Sta::addClock(StaClock) + StaClock::setPropagateClock()
   → StaClock::addVertex(source_out_vertex)

4. Annotate input slew on the source buffer's output vertex
   → vertex->addData(new StaSlewData(...))

5. Full timing update (clock + slew + delay propagation)
   → STAInst->updateTiming()

6. Query results at sink buffer's input pin:
   → STAInst->getClockAT(sink_pin)  → total delay (ns)
   → STAInst->getSlew(sink_pin)     → output slew (ns)

7. Cleanup: destroy temp objects, remove clock, re-read SDC
   → adapter->deleteInstance() / deleteNet()
   → Sta::resetSdcConstrain() + readSdc()
```

### Wire Segment Division (Variable Length)

Wire segments are NOT equally divided. Their lengths depend on the number of consecutive
non-buffer positions between buffer/endpoint pairs, matching the reference implementation:

```
Example: total wire = 3 units, 3 nodes, topology bits = "010" (buf at position 1)
  Node 0: wire    → 1 position before buf
  Node 1: buffer  →
  Node 2: wire    → 1 position after buf
  Wire segments: [1*unit (net0), 1*unit (net1)]
  Extra positions: counted per net between buf/endpoint boundaries

Example: total wire = 3 units, 3 nodes, topology bits = "100" (buf at position 0)
  Node 0: buffer  →
  Node 1: wire    → 2 positions after buf
  Node 2: wire    →
  Wire segments: [0*unit (net0: source→buf), 2*unit (net1: buf→sink)]
```

### Load Compensation

The sink buffer has intrinsic input pin capacitance `C_sink`. When sweeping external load `L`:
- Set RC tree far-end cap to `L - C_sink` (so total effective load = L)
- If `L < C_sink`, skip this load value

**Rationale**: Using iSTA's clock propagation ensures correct multi-stage delay computation,
including liberty cell delays, RC Elmore delays, and slew degradation — all handled automatically by the engine.

---

## 4. Unit Protocol

**Constraint**: All internal characterization uses **physical units (um, ns, pF)** — NO DBU conversion.

| Context | Unit | Type | Description |
|---------|------|------|-------------|
| Char module internal | um | `double` | Physical wire length in microns |
| Char data structures | index | `unsigned` | Discretized bin indices `[1, *_steps]` |
| CTSAPI → STA calls | um/ns/pF | `double` | Physical units directly |

**Key Design**:
- CharBuilder works entirely in physical units (um for length)
- No DBU conversion except at iDB coordinate boundaries (handled by iDB/CTSAPI)
- Variable naming convention: `*_um` suffix when ambiguity possible (e.g., `wire_length_um`)

**API return unit reference**:
| API | Returns |
|-----|---------|
| `queryWireResistance(layer, length_um)` | Ohms |
| `queryWireCapacitance(layer, length_um)` | pF |
| `queryCharClockAT(pin, clock_name)` | ns |
| `queryCharSlew(pin)` | ns |
| `queryCharInputPinCap(cell_master)` | pF |
| `queryCellOutPinCapLimit(cell_master)` | pF |
| `queryCellInPinSlewLimit(cell_master)` | ps |

**Rules**:
- All CharBuilder internal computations use physical units (um, ns, pF)
- Discretization to integer indices happens ONLY when creating CharCore/SegmentChar
- Document units in variable names when ambiguous (e.g., `wire_length_um`)

---

## 5. Buffer Sorting and Pruning

**Constraint**: Buffer enumeration must apply driving-capability-based pruning.

**Sorting rule**:
- Sort buffers **ascending** by driving capability, using `max_cap` from liberty output port as the proxy metric
- Query via: `lib_cell->bufferPorts(in, out)` → `out->get_port_cap_limit(AnalysisMode::kMax)`
- Sorting is performed once during builder initialization

**Pruning rule (monotonic constraint)**:
- Buffer index sequence must be **non-decreasing** from input to output
- i.e., buffer[i] index <= buffer[i+1] index in the sorted list
- A smaller-drive-strength buffer must NOT appear after a larger one
- Valid combinations = C(n_buf_types + n_positions - 1, n_positions) — "stars and bars"
- Enumerate via mixed-radix counter, skip non-monotonic sequences

**Example** (4 buffer types, 2 positions):
- Valid: [0,0], [0,1], [0,2], [0,3], [1,1], [1,2], [1,3], [2,2], [2,3], [3,3] → 10 out of 16

**Rationale**: Reduces combinatorial explosion in pattern enumeration; mirrors standard CTS practice.

---

## 6. Near-Neighbor Redundancy Removal

**Constraint**: Implement configurable redundancy removal for the sorted buffer list, **default to disabled**.

**Scope**: This applies to the **buffer list** after sorting by max_cap — removing buffers whose max_cap values are too close to their neighbor.

**Parameters**:
- A percentage threshold parameter (e.g., `_char_buf_redundancy_pct`) configurable via `CTSConfigInst`
- Default value: `0.0` (disabled — all buffers kept)
- Reference uses 10% threshold when enabled

**Behavior when enabled** (threshold > 0):
- After sorting buffers by max_cap ascending
- Compare adjacent buffers: if `(buf[i+1].max_cap - buf[i].max_cap) / buf[i].max_cap < threshold`, remove `buf[i+1]`
- This reduces buffers with near-identical drive strength

**Behavior when disabled (default)**:
- All sorted buffers are kept — no filtering

---

## 7. Configuration via CTSConfigInst

**Constraint**: All algorithm parameters must be read from `CTSConfigInst` at initialization.

**Already existing parameters to use**:
| Parameter | Config Getter | Description |
|-----------|---------------|-------------|
| Buffer list | `get_buffer_types()` | Buffer cell master names |
| Slew steps | `get_slew_steps()` | Number of discretization bins for slew |
| Cap steps | `get_cap_steps()` | Number of discretization bins for cap |
| Length steps | `get_length_steps()` | Number of discretization bins for length |
| Max pattern nodes | `get_max_pattern_nodes()` | Max nodes per topology |
| Max buf tran | `get_max_buf_tran()` | Max buffer transition (defines slew range) |
| Max cap | `get_max_cap()` | Max capacitance (defines cap range) |
| Max length | `get_max_length()` | Max wire length (defines length range) |
| Routing layers | `get_routing_layers()` | Layers to consider |
| Wire width | `get_wire_width()` | Wire width for R/C queries |

**New parameters to add**:
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `_char_buf_redundancy_pct` | double | 0.0 | Buffer near-neighbor removal threshold |

- Do NOT hardcode algorithm parameters in the characterization module
- **Exception**: Structural constants (like bitset width for topology enumeration) may be hardcoded

### Data Types and Naming

**Integer types**: Use `unsigned` throughout (not `uint16_t`/`uint32_t`/`uint64_t`)
- Discretized indices: `unsigned` (slew_idx, cap_idx, length_idx)
- Pattern IDs: `unsigned` (local_id in PatternId)
- Topology levels: `unsigned`

**Naming convention for discretized fields**:
- Physical values (double): `slew`, `cap`, `length` (with `*_um`/`*_ns`/`*_pf` suffix when ambiguous)
- Discretized indices (unsigned): `*_idx` suffix — e.g., `input_slew_idx`, `load_cap_idx`, `length_idx`
- This distinction prevents mixing physical values with bin indices

---

## 8. Module Location and File Organization

**Constraint**: New code goes in `src/operation/iCTS/source/module/characterization/` (algorithm layer) and `src/operation/iCTS/source/database/characterization/` (data layer).

**Existing files (header-only, do not break)**:
- Database layer: `CharCore.hh`, `SegmentChar.hh`, `BufferingPattern.hh`, `PatternId.hh`, `HTreeTopologyChar.hh`, `HTreeTopologyPattern.hh`
- Module layer: `HashJoinEngine.hh`, `SegmentTraits.hh`, `HTreeTraits.hh`, `SegmentCharTable.hh`, `HTreeTopologyCharTable.hh`, `Pruner.hh`, `PatternCombiner.hh`

**Data type unification**: All characterization data structures use `unsigned` (not sized integers):
- `CharCore`: `input_slew_idx`, `output_slew_idx`, `driven_cap_idx`, `load_cap_idx` (all `unsigned`)
- `SegmentChar`: `length_idx` (`unsigned`)
- `PatternId`: `local_id` (`unsigned`)
- Hash keys: packed 32-bit via `(slew << 16) | cap` (slew/cap each `unsigned`)
- `PatternId::pack()`: `(domain << 30) | local_id` — shift 30 (not 32) to stay within 32-bit range

**New files to create** (in module/characterization/):
- `CharBuilder.hh` / `CharBuilder.cc` — Main builder class: enumerates topologies, drives CTSAPI, produces SegmentChar entries
- Possibly additional helpers as needed

**CMake changes**:
- Module characterization library is currently `INTERFACE` (header-only)
- Adding `.cc` files requires changing to regular `add_library` with source files
- Both libraries already have `add_subdirectory` enabled in parent CMakeLists

**New CTSAPI functions** (in `api/CTSAPI.hh` / `CTSAPI.cc`):
- Add wrapper functions for timing characterization queries
- No new data structures — only function declarations

**Naming**:
- Headers: `.hh`, PascalCase (e.g., `CharBuilder.hh`)
- Sources: `.cc`, PascalCase (e.g., `CharBuilder.cc`)
- Namespace: `icts`

---

## 9. Timing/Power Query Path

**Constraint**: Use iSTA's clock propagation for timing, all through CTSAPI wrapper functions.

| Metric | Tool | CTSAPI Function | Return Unit |
|--------|------|-----------------|-------------|
| Wire R/C | iSTA adapter | `queryWireResistance/Capacitance(layer, length_um)` | Ohms / pF |
| Cell cap limit | iSTA liberty | `queryCellOutPinCapLimit(cell_master)` | pF |
| Cell slew limit | iSTA liberty | `queryCellInPinSlewLimit(cell_master)` | ps |
| Buffer ports | iSTA liberty | `queryBufferPorts(cell_master)` | port names |
| Input pin cap | iSTA liberty | `queryCharInputPinCap(cell_master)` | pF |
| Build temp circuit | iDB adapter | `createCharInstance/Net`, `attachCharPin` | — |
| Build RC tree | iSTA | `buildCharRcTree(net, res, cap, load)` | — |
| Clock setup | iSTA | `createCharClock(pin, period, name)` | — |
| Input slew | iSTA | `setCharInputSlew(pin, slew_ns)` | — |
| Timing update | iSTA | `updateCharTiming()` | — |
| **Total delay** | **iSTA** | **`queryCharClockAT(pin, clock_name)`** | **ns** |
| **Output slew** | **iSTA** | **`queryCharSlew(pin)`** | **ns** |
| Cleanup | iSTA/iDB | `destroyCharInstance/Net`, `destroyCharClock` | — |
| DB unit | iDB | `queryDbUnit()` | DBU/um |

**Key design decisions**:
- **Delay via getClockAT**: Create a propagated clock on the source pin; after timing update,
  `getClockAT(sink_pin)` returns the total accumulated delay through the chain
- **No manual delay summation**: Do NOT call `getInstDelay()` / `getNetDelay()` individually
- **Power**: iSTA has no per-instance power API; `power_w = 0.0` for now (iPA integration deferred)
- The characterization module must NOT directly call `STAInst->` or `TimingIDBAdapter->` APIs
- All such calls must be wrapped in CTSAPI functions
- CTSAPI functions should be thin wrappers (no business logic)

---

## 10. General iCTS Coding Standards

These are inherited from project-level constraints (see `.trellis/spec/backend/`):

- **No `git commit`** — AI does not commit code
- **Copyright header**: Mulan PSL v2 on all new files
- **Logging**: Use `CTS_LOG_*` macros (not `LOG_*` or `std::cout`)
- **No exceptions**: Use logging-based error handling
- **Naming**: PascalCase classes, camelBack methods, `_underscore` members
- **Singleton access**: Use `CTSAPIInst`, `CTSConfigInst`, `CTSDesignInst`, `CTSWrapperInst`
- **Code style**: Must pass `clang-format` (Google-based config at project root)

---

## Summary Checklist

Before submitting code for this task, verify:

- [ ] No external project names anywhere in new code
- [ ] No new data structures in CTSAPI
- [ ] Real iDB nets used (not virtual RC trees)
- [ ] Physical units (um/ns/pF) throughout — NO DBU conversion in char module
- [ ] Buffers sorted by max_cap at initialization
- [ ] Pruning: monotonic buffer indices (non-decreasing)
- [ ] Redundancy removal configurable, default off
- [ ] Steps-based discretization (`*_steps` not `*_unit`)
- [ ] All parameters from CTSConfigInst
- [ ] Data types unified to `unsigned` (not `uint16_t`/`uint32_t`/`uint64_t`)
- [ ] `*_idx` naming for discretized indices, physical names for double values
- [ ] Module in `source/module/characterization/`
- [ ] Timing via iSTA clock propagation (`getClockAT`), all through CTSAPI
- [ ] iCTS naming conventions followed
- [ ] CTS_LOG_* macros used for logging
- [ ] Mulan PSL v2 copyright on new files
- [ ] clang-format clean
