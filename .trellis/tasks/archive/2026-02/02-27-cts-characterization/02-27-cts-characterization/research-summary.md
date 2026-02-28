# Research Summary — iCTS Characterization

> Consolidated findings from 3 research agents. Reference for implementation phase.

---

## 1. Reference Characterization Flow (External CTS Implementation)

### Overall Flow
```text
initialize characterization context
  -> query wire RC parameters
  -> sort buffers by drive capability
  -> optionally remove near-neighbor buffers
  -> derive wirelength, slew, and load sweep ranges

for each wirelength:
  enumerate candidate topologies
  build an isolated or temporary timing context
  apply Pi-model parasitics and update timing

  for each topology:
    for each monotonic buffer combination:
      for each load:
        update far-end load
        for each input slew:
          annotate source slew
          run timing update
          collect delay, slew, power, and input capacitance

post-process results
  -> filter out-of-range entries
  -> normalize data for lookup-table storage
```

### Key Design Decisions
- **Separate STA instance** for characterization block (not main design STA)
- **Pi-model parasitics**: symmetric C/2-R-C/2 with Elmore delay = R*C
- **Load applied** by adding to far-end cap of last net's Pi-model
- **Input slew** is annotated on the source-side timing vertex
- **Delay** = arrival at output pin (fall transition)
- **Output slew** = average of rise and fall slew
- **Power** = sum of all buffer instance powers
- **Input cap** = first_buf_input_cap + first_seg_wire_cap (buffered) or load + total_wire_cap (pure wire)
- **Normalization**: continuous → discrete uint8_t by dividing by step size and ceil

### Buffer Enumeration
- Buffers sorted ascending by `max_capacitance` from liberty output port
- Near-neighbor dedup: remove if within 10% of neighbor's max_cap
- **Monotonic constraint**: buffer indices must be non-decreasing left→right
  - e.g., with 4 buf types, 2 positions: 10 valid out of 16 combinations = C(n+k-1, k)
- Enumeration via mixed-radix counter with skip for non-monotonic

### WireSegment (LUT Entry) Fields
| Field | Type | Description |
|-------|------|-------------|
| length | uint8_t | Normalized wire length |
| load | uint8_t | Normalized output load cap |
| outputSlew | uint8_t | Normalized output slew |
| inputCap | uint8_t | Normalized input capacitance |
| inputSlew | uint8_t | Normalized input slew |
| delay | unsigned | Normalized delay |
| power | double | Total buffer power |
| bufferLocations | vector<double> | Fractional positions (0-1) |
| bufferMasters | vector<string> | Cell master names |

### LUT Key
30-bit composite: `length | (load << 10) | (outputSlew << 20)`

---

## 2. iCTS Existing Infrastructure

### Already Exists (header-only)

**Database layer** (`source/database/characterization/`):
- `CharCore.hh` — Core data: input_slew(u16), output_slew(u16), driven_cap(u16), load_cap(u16), delay(double), power(double), pattern_id
- `SegmentChar.hh` — CharCore + length_dbu(uint64_t)
- `BufferingPattern.hh` — Buffer positions and cell masters
- `PatternId.hh` — Domain-tagged pattern ID (segment vs topology)

**Module layer** (`source/module/characterization/`):
- `HashJoinEngine.hh` — Generic hash-join for table concatenation
- `SegmentTraits.hh` / `HTreeTraits.hh` — Join traits
- `SegmentCharTable.hh` / `HTreeTopologyCharTable.hh` — Table wrappers
- `Pruner.hh` — Pareto pruner, InputBoundaryPruner
- `PatternCombiner.hh` — Pattern combiners

### Config Parameters (already present)
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `_slew_unit` | double | 0.05 | Slew discretization unit |
| `_cap_unit` | double | 0.05 | Cap discretization unit |
| `_length_unit` | double | 25 | Length discretization unit |
| `_max_pattern_nodes` | unsigned | 8 | Max pattern nodes |
| `_buffer_types` | vector<string> | empty | Buffer cell master names |
| `_routing_layers` | vector<unsigned> | empty | Routing layers |
| `_max_buf_tran` | double | 0.0 | Max buffer transition |
| `_max_sink_tran` | double | 0.0 | Max sink transition |
| `_max_cap` | double | 0.0 | Max capacitance |
| `_max_length` | double | 0.0 | Max length |
| `_wire_width` | double | 0.0 | Wire width |

### CTSAPI Existing STA Functions
| Function | Purpose |
|----------|---------|
| `initSTA()` | Create TimingIDBAdapter, load liberty, build graph |
| `queryWireResistance(layer, length, width)` | Wire R via adapter (length in um) |
| `queryWireCapacitance(layer, length, width)` | Wire C via adapter (length in um) |
| `queryCellOutPinCapLimit(cell_master)` | Output pin max cap (pF) |
| `queryCellInPinSlewLimit(cell_master)` | Input pin max slew (ps) |
| `queryInstType(inst_name)` | Buffer/FF/Inverter/ICG/Mux |

### Missing in CTSAPI (Need to Add)
- Create temporary net + instances for characterization
- Build RC tree on temporary net
- Set annotated slew on input
- Query delay/slew at output after timing update
- Query power via iPA
- Clean up temporary nets/instances

---

## 3. Tool APIs for New CTSAPI Functions

### 3.1 Creating Temporary Nets/Instances (via TimingIDBAdapter)

```cpp
auto* adapter = STAInst->getIDBAdapter();

// Create instance
Instance* inst = adapter->createInstance(lib_cell, "cts_char_buf_0");

// Create net
Net* net = adapter->createNet("cts_char_net_0", nullptr, IdbConnectType::kClock);

// Connect pin to net
Pin* pin = adapter->attach(inst, "Z", net);  // output pin
Pin* pin2 = adapter->attach(inst2, "A", net); // input pin of next stage

// Cleanup
adapter->deleteInstance("cts_char_buf_0");
adapter->deleteNet(net);
```

### 3.2 Building Real RC Tree on Net

```cpp
// After creating net and connecting pins via adapter:
auto* net_obj = STAInst->get_netlist()->findNet("cts_char_net_0");

STAInst->initRcTree(net_obj);
auto* node1 = STAInst->makeOrFindRCTreeNode(driver_pin);
auto* node2 = STAInst->makeOrFindRCTreeNode(load_pin);
STAInst->makeResistor(net_obj, node1, node2, resistance);
STAInst->incrCap(node2, capacitance);
STAInst->updateRCTreeInfo(net_obj);
```

Or use Pi-model approach (simpler):
```cpp
STAInst->buildRcTreeAndUpdateRcTreeInfo("cts_char_net_0", loadname2wl_map);
```

### 3.3 Timing Queries

```cpp
// Update timing after RC tree changes
STAInst->updateTiming();  // or incrUpdateTiming()

// Slew at pin (returns ns)
double slew = STAInst->getSlew("cts_char_buf_0:Z", AnalysisMode::kMax, TransType::kRise);

// Instance delay (returns ns)
double delay = STAInst->getInstDelay("cts_char_buf_0", "A", "Z", AnalysisMode::kMax, TransType::kRise);

// Net delay (returns ns)
double net_delay = STAInst->getNetDelay("cts_char_net_0", "cts_char_buf_1:A", AnalysisMode::kMax, TransType::kRise);

// Pin capacitance
double cap = STAInst->getInstPinCapacitance("cts_char_buf_0:A");
```

### 3.4 Liberty Table Direct Lookup (Alternative)

```cpp
// Direct NLDM table lookup (no net construction needed)
auto* table = STAInst->getCellLibertyTable("CLKBUF_X4", LibTable::TableType::kCellRise);
double delay = table->findValue(input_slew, output_load);

auto* slew_table = STAInst->getCellLibertyTable("CLKBUF_X4", LibTable::TableType::kRiseTransition);
double out_slew = slew_table->findValue(input_slew, output_load);
```

### 3.5 Liberty Cell Properties

```cpp
auto* lib_cell = STAInst->findLibertyCell("CLKBUF_X4");
LibPort* in_port = nullptr;
LibPort* out_port = nullptr;
lib_cell->bufferPorts(in_port, out_port);

// Max cap (drive strength proxy)
auto max_cap = out_port->get_port_cap_limit(AnalysisMode::kMax);

// Input pin capacitance
double in_cap = in_port->get_port_cap();

// Max slew limit
auto max_slew = in_port->get_port_slew_limit(AnalysisMode::kMax);
```

### 3.6 iPA Power Queries

```cpp
auto* pe = ipower::PowerEngine::getOrCreatePowerEngine();
pe->runCompleteFlow();  // Full power analysis

auto* power = pe->get_power();
// Per-instance power
auto* data = power->getObjData(design_obj);
double total = data->get_total_power();  // Watts
```

**Warning**: PowerEngine creates its own TimingEngine internally — may conflict with iCTS's STAInst singleton. Need to verify or use a shared instance.

### 3.7 Unit Conversion Reference

| API | Input Unit | Output Unit |
|-----|-----------|-------------|
| `queryWireResistance(layer, length)` | length: um (double) | Ohms |
| `queryWireCapacitance(layer, length)` | length: um (double) | pF |
| `getSlew()` | — | ns |
| `getInstDelay()` | — | ns |
| `getNetDelay()` | — | ns |
| `getInstPinCapacitance()` | — | pF (after unit conversion) |
| `queryCellOutPinCapLimit()` | — | pF |
| `queryCellInPinSlewLimit()` | — | ps |
| `CTSWrapperInst.getDbUnit()` | — | DBU per micron |

---

## 4. Implementation Strategy Notes

### Approach: Real Net with RC Tree (as required)

1. Create temporary buffer instances + wire nets in iDB via `TimingIDBAdapter`
2. Build Pi-model RC tree on each wire net segment
3. Apply load cap on last net, set annotated input slew
4. Run `updateTiming()`
5. Read delay, slew, power from STA/PA queries
6. Clean up temporary objects

### Naming Convention for Temp Objects
Use prefix like `cts_char_` to avoid collision with real design:
- Instances: `cts_char_buf_{topology_id}_{position}`
- Nets: `cts_char_net_{topology_id}_{segment_id}`

### Considerations
- The reference flow used an isolated characterization context, while iCTS planned to create temporary objects in the active design with distinct names
- The reference flow used a dedicated timing setup for characterization, while iCTS may need to reuse the shared `STAInst` with incremental updates
- Power via iPA may need special handling due to TimingEngine singleton conflict
- All temp objects MUST be cleaned up after characterization
