# Database Guidelines

> Data model, singleton patterns, and memory management in iCTS.

---

## Overview

iCTS uses a singleton-based data model where core design objects (Clock, Inst, Net, Pin) are managed through the `Design` singleton. Configuration is handled by the `Config` singleton, iDB integration through the `Wrapper` singleton, and external timing-system access through the database-level `STAAdapter` singleton. External callers still enter through `CTS_API_INST`.

---

## Singleton Pattern

All singletons use the **Meyers Singleton** pattern with a macro alias.

### Singleton Template

```cpp
// In header file
#define CONFIG_INST (icts::Config::getInst())

class Config
{
 public:
  static Config& getInst()
  {
    static Config instance;
    return instance;
  }

  // Delete copy and move
  Config(const Config&) = delete;
  Config(Config&&) = delete;
  Config& operator=(const Config&) = delete;
  Config& operator=(Config&&) = delete;

 private:
  Config() = default;
  ~Config() = default;
};
```

### Singleton Registry

| Macro | Class | Defined In | Purpose |
|-------|-------|------------|---------|
| `CTS_API_INST` | `CTSAPI` | `api/CTSAPI.hh` | Main external API entry point |
| `DESIGN_INST` | `Design` | `database/design/Design.hh` | Design database |
| `CONFIG_INST` | `Config` | `database/config/Config.hh` | Configuration |
| `WRAPPER_INST` | `Wrapper` | `database/io/Wrapper.hh` | iDB adapter |
| `STA_ADAPTER_INST` | `STAAdapter` | `database/adapter/sta/STAAdapter.hh` | iSTA adapter for routing / timing / characterization internals |
| `LOG_INST` | `Logger` | `utils/logger/Logger.hh` | Logging |

### Usage Example

```cpp
// External entry uses CTS_API_INST
CTS_API_INST.init(config_file, InitOptions{WorkDir{work_dir}});
CTS_API_INST.runCTS();

// Internal source-layer code uses narrowed singletons
if (CONFIG_INST.is_use_netlist()) {
  auto net_list = CONFIG_INST.get_net_list();
  for (auto& [clock_name, net_name] : net_list) {
    auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
    DESIGN_INST.add_clock(std::move(clock));
  }
}

WRAPPER_INST.read();
LOG_INST.close();
```

---

## Data Model Hierarchy

```
Design (singleton, DESIGN_INST)
 └─ vector<unique_ptr<Clock>>  _clocks
       │
       Clock
        ├─ string     _clock_name        (SDC clock name)
        ├─ string     _clock_net_name    (physical net name)
        ├─ Pin*       _clock_source      (driver pin)
        ├─ vector<Pin*>  _loads          (sink pins)
        ├─ vector<Inst*> _inserted_insts (CTS result: buffers)
        └─ vector<Net*>  _inserted_nets  (CTS result: nets)

Net
 ├─ string      _name
 ├─ Pin*        _driver     (one output pin)
 └─ vector<Pin*> _loads

Pin
 ├─ string      _name
 ├─ PinType     _type       (kClock, kIn, kOut, kInOut, kOther)
 ├─ Point<int>  _location   (DBU integer coordinates)
 ├─ Inst*       _inst       (owning instance, nullptr for IO ports)
 ├─ Net*        _net        (connected net)
 └─ bool        _b_io       (true for top-level IO ports)

Inst
 ├─ string      _name
 ├─ string      _cell_master (Liberty cell name)
 ├─ InstType    _type        (kBuffer, kFlipFlop, kInverter, kClockGate, kMux, kUnknown)
 ├─ Point<int>  _location
 └─ vector<Pin*> _pins       (first pin = driver pin by convention)
```

### Spatial Types

```
Point<T>  — Template 2D point with arithmetic operators (used as Point<int>)

Tree      — Topology tree (non-copyable, movable)
 ├─ vector<unique_ptr<TreeNode>>  _nodes
 └─ size_t  _root

TreeNode
 ├─ size_t           _id, _parent
 ├─ vector<size_t>   _children
 ├─ Point<int>       _position
 └─ vector<Pin*>     _loads
```

### Characterization Types

```
PatternId — Domain-tagged pattern ID
 ├─ PatternDomain domain  (kSegmentPattern, kTopologyPattern)
 └─ unsigned local_id
 Static factories: PatternId::segment(id), PatternId::topology(id)
 Hashable via pack() method

CharCore — Base electrical boundary + cost
 ├─ unsigned  _input_slew_idx, _output_slew_idx, _driven_cap_idx, _load_cap_idx
 ├─ double    _delay, _power
 └─ PatternId _pattern_id

SegmentChar     (CharCore + unsigned _length_idx)
 - compose() static: merges upstream + downstream segments

HTreeTopologyChar (CharCore + unsigned _levels)
 - compose() static: merges with binary fan-out (power *= 2 for downstream)

BufferingPattern
 ├─ unsigned           _length_idx
 ├─ PatternId          _pattern_id
 ├─ vector<double>     _buffer_positions  (normalized 0..1)
 └─ vector<string>     _cell_masters
 - concat() static: merges patterns with position renormalization

HTreeTopologyPattern
 ├─ PatternId              _pattern_id
 ├─ unsigned               _levels
 └─ vector<PatternId>      _level_segment_pattern_ids
 - concat() static: merges level segment references
```

---

## Memory Management

### Design Objects: Smart Pointers

Design layer objects (`Clock`, `Inst`, `Net`, `Pin`) use `std::unique_ptr` ownership with raw-pointer access for cross-references.

**Object creation** (in `Wrapper.cc`):
```cpp
auto cts_pin  = std::make_unique<Pin>(name, type, location, nullptr, nullptr, is_io);
auto cts_inst = std::make_unique<Inst>(name, cell_master, type, location);
auto cts_net  = std::make_unique<Net>(name);
```

Objects are owned by `Wrapper` via `_owned_pins`, `_owned_insts`, and `_owned_nets` vectors. Raw pointers are obtained via `.get()` for cross-reference maps and downstream use.

**Object ownership**: `Design` owns `unique_ptr<Clock>` objects; RAII handles cleanup:
```cpp
// Design::reset() -- vector clear destroys unique_ptr's
void reset() { _clocks.clear(); }
```

`Wrapper::reset()` similarly clears `_owned_pins`, `_owned_insts`, and `_owned_nets`, which destroys the owned objects via RAII.

### Topology Objects: Smart Pointers

`Tree` uses `std::unique_ptr` for `TreeNode` ownership:
```cpp
std::vector<std::unique_ptr<TreeNode>> _nodes;
```

`Tree` is non-copyable but movable (returned by value from `TopologyGen::build()`).

### Cross-Reference Maps (Wrapper)

`Wrapper` maintains bidirectional maps between iDB and CTS objects:
```cpp
std::unordered_map<Inst*, idb::IdbInstance*> _cts2idb_inst_map;
std::unordered_map<idb::IdbInstance*, Inst*> _idb2cts_inst_map;
// Same pattern for Net and Pin
```

### Memory Management Summary

| Object Type | Storage | Owner | Freed By |
|-------------|---------|-------|----------|
| `Clock` | `unique_ptr<Clock>` in `Design._clocks` | `Design` | RAII (`_clocks.clear()`) |
| `Pin`, `Inst`, `Net` | `unique_ptr` in `Wrapper._owned_*` vectors | `Wrapper` | RAII (`_owned_*.clear()`) |
| `TreeNode` | `unique_ptr<TreeNode>` in `Tree` | `Tree` | RAII (destructor) |
| `Tree` | Value / moved | Caller | RAII (destructor) |

---

## Getter/Setter Conventions

All data classes follow the same pattern:

```cpp
class Inst
{
 public:
  // Getter: const reference for strings/vectors, value for primitives
  const std::string& get_name() const { return _name; }
  InstType get_type() const { return _type; }
  const std::vector<Pin*>& get_pins() const { return _pins; }

  // Setter: const reference for strings, value for primitives
  void set_name(const std::string& name) { _name = name; }
  void set_type(InstType type) { _type = type; }

  // Mutable getter for modifying collections
  std::vector<Pin*>& get_pins() { return _pins; }

  // Convenience adder
  void add_pin(Pin* pin) { _pins.push_back(pin); }

  // Boolean queries
  bool is_buffer() const { return _type == InstType::kBuffer; }

 private:
  std::string _name;
  InstType _type = InstType::kUnknown;
  std::vector<Pin*> _pins;
};
```

---

## Adding New Data Classes

When creating a new data class:

1. Place header in the appropriate `source/database/` subdirectory
2. Follow the getter/setter pattern above
3. Use `enum class` for any type enums (k-prefix values)
4. Initialize all members with defaults
5. If header-only, use INTERFACE CMake library
6. If singleton, follow the Meyers Singleton template and define a macro
