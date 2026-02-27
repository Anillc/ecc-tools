# Database Guidelines

> Data model, singleton patterns, and memory management in iCTS.

---

## Overview

iCTS uses a singleton-based data model where core design objects (Clock, Inst, Net, Pin) are managed through the `Design` singleton. Configuration is handled by the `Config` singleton, and iDB integration through the `Wrapper` singleton.

---

## Singleton Pattern

All singletons use the **Meyers Singleton** pattern with a macro alias.

### Singleton Template

```cpp
// In header file
#define CTSConfigInst (icts::Config::getInst())

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
| `CTSAPIInst` | `CTSAPI` | `api/CTSAPI.hh` | Main API entry point |
| `CTSDesignInst` | `Design` | `database/design/Design.hh` | Design database |
| `CTSConfigInst` | `Config` | `database/config/Config.hh` | Configuration |
| `CTSWrapperInst` | `Wrapper` | `database/io/Wrapper.hh` | iDB adapter |
| `CTSLogInst` | `Logger` | `utils/logger/Logger.hh` | Logging |

### Usage Example

```cpp
// Read config
if (CTSConfigInst.is_use_netlist()) {
  auto& net_list = CTSConfigInst.get_clock_netlist();
  for (auto& [clock_name, net_name] : net_list) {
    auto* clock = new icts::Clock(clock_name, net_name);
    CTSDesignInst.add_clock(clock);
  }
}

// Read design data via wrapper
CTSWrapperInst.read();

// Reset all singletons
CTSConfigInst.reset();
CTSDesignInst.reset();
CTSWrapperInst.reset();
CTSLogInst.close();
```

---

## Data Model Hierarchy

```
Design (singleton, CTSDesignInst)
 └─ vector<Clock*>  _clocks
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
 ├─ InstType    _type        (kBuffer, kFlipFlop, kInverter, ...)
 ├─ Point<int>  _location
 └─ vector<Pin*> _pins       (first pin = driver pin by convention)
```

### Spatial Types

```
Point<T>  — Template 2D point with arithmetic operators (used as Point<int>)

Tree      — Topology tree
 └─ vector<unique_ptr<TreeNode>>  _nodes

TreeNode
 ├─ size_t           _id, _parent
 ├─ vector<size_t>   _children
 ├─ Point<int>       _position
 └─ vector<Pin*>     _loads
```

### Characterization Types

```
CharCore — Base electrical boundary + cost
 ├─ uint16_t  _input_slew, _output_slew, _driven_cap, _load_cap
 ├─ double    _delay, _power
 └─ PatternId _pattern_id

SegmentChar     (CharCore + uint64_t _length_dbu)
HTreeTopologyChar (CharCore + uint32_t _levels)

BufferingPattern
 ├─ uint64_t           _length_dbu
 ├─ PatternId          _pattern_id
 ├─ vector<double>     _buffer_positions  (normalized 0..1)
 └─ vector<string>     _cell_masters

PatternId — Domain-tagged pattern ID
 ├─ PatternDomain domain  (kSegmentPattern, kTopologyPattern)
 └─ uint32_t local_id
```

---

## Memory Management

### Design Objects: Raw Pointers

Design layer objects (`Clock`, `Inst`, `Net`, `Pin`) use raw pointers.

**Object creation** (in `Wrapper.cc`):
```cpp
auto* cts_pin  = new Pin(name, type, location, nullptr, nullptr, is_io);
auto* cts_inst = new Inst(name, cell_master, type, location);
auto* cts_net  = new Net(name);
```

**Object ownership**: `Design` owns `Clock*` objects and frees them:
```cpp
// Design::reset()
void reset()
{
  for (auto* clock : _clocks) {
    delete clock;
  }
  _clocks.clear();
}
```

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
| `Clock*` | `vector<Clock*>` in `Design` | `Design` | `Design::reset()` |
| `Pin*`, `Inst*`, `Net*` | Raw pointers | Implicit (singleton lifecycle) | Singleton `reset()` |
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
