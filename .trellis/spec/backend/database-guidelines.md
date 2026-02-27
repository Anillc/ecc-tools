# Database Guidelines

> Data model patterns and conventions for this project.

---

## Overview

This project uses **in-memory data models** (not traditional databases). The "database" layer refers to:
- Core data classes (Clock, Inst, Net, Pin)
- Singleton managers (Config, Design, Wrapper)
- Spatial data structures (Point, Tree)
- Integration with external databases (iDB wrapper)

**No ORM or SQL** - all data is managed through C++ objects.

---

## Core Data Classes

### Design Data Model

Located in `src/operation/iCTS/source/database/design/`

| Class | Purpose | Key Members |
|-------|---------|-------------|
| `Clock` | Clock tree input/output | `_clock_name`, `_source_pin`, `_loads`, `_inserted_insts`, `_inserted_nets` |
| `Inst` | Instance representation | `_name`, `_cell_master`, `_type`, `_location`, `_pins` |
| `Net` | Connection structure | `_name`, `_driver_pin`, `_load_pins` |
| `Pin` | Pin representation | `_name`, `_type`, `_location`, `_inst`, `_net` |

### Example: Clock Class

```cpp
class Clock
{
 public:
  Clock() = default;
  ~Clock() = default;

  // Getters
  const std::string& get_clock_name() const { return _clock_name; }
  Pin* get_source_pin() const { return _source_pin; }
  const std::vector<Pin*>& get_loads() const { return _loads; }
  const std::vector<Inst*>& get_inserted_insts() const { return _inserted_insts; }
  const std::vector<Net*>& get_inserted_nets() const { return _inserted_nets; }

  // Setters
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_source_pin(Pin* source_pin) { _source_pin = source_pin; }
  void add_load(Pin* load) { _loads.push_back(load); }
  void add_inserted_inst(Inst* inst) { _inserted_insts.push_back(inst); }
  void add_inserted_net(Net* net) { _inserted_nets.push_back(net); }

 private:
  std::string _clock_name;
  Pin* _source_pin = nullptr;
  std::vector<Pin*> _loads;
  std::vector<Inst*> _inserted_insts;
  std::vector<Net*> _inserted_nets;
};
```

---

## Singleton Pattern

### Purpose

Singletons provide global access to shared resources:
- Configuration (`Config`)
- Design database (`Design`)
- External DB wrapper (`Wrapper`)
- Logger (`Logger`)

### Implementation Pattern

```cpp
#define CTSConfigInst (icts::Config::getInst())

class Config
{
 public:
  static Config& getInst()
  {
    static Config instance;
    return instance;
  }

  // Delete copy and move constructors
  Config(const Config& rhs) = delete;
  Config(Config&& rhs) = delete;
  Config& operator=(const Config& rhs) = delete;
  Config& operator=(Config&& rhs) = delete;

 private:
  Config() = default;
  ~Config() = default;

  // Member variables
  double _skew_bound = 0.0;
  std::vector<std::string> _buffer_types;
  // ...
};
```

### Available Singletons

| Singleton | Macro | File | Purpose |
|-----------|-------|------|---------|
| `CTSAPI` | `CTSAPIInst` | `api/CTSAPI.hh` | Main API entry point |
| `Config` | `CTSConfigInst` | `source/database/config/Config.hh` | Configuration management |
| `Design` | `CTSDesignInst` | `source/database/design/Design.hh` | Design database (holds clocks) |
| `Wrapper` | `CTSWrapperInst` | `source/database/io/Wrapper.hh` | iDB integration wrapper |
| `Logger` | `CTSLogInst` | `source/utils/logger/Logger.hh` | CTS-specific logger |

### Usage Example

```cpp
// Access singleton via macro
CTSConfigInst.set_skew_bound(100.0);
double skew = CTSConfigInst.get_skew_bound();

// Add clock to design
Clock* clock = new Clock();
clock->set_clock_name("clk");
CTSDesignInst.add_clock(clock);

// Get all clocks
const auto& clocks = CTSDesignInst.get_clocks();
```

---

## Spatial Data Structures

### Point<T> Template

Generic 2D point with arithmetic operators:

```cpp
template <typename T>
class Point
{
 public:
  Point() = default;
  Point(T x, T y) : _x(x), _y(y) {}

  T get_x() const { return _x; }
  T get_y() const { return _y; }
  void set_x(T x) { _x = x; }
  void set_y(T y) { _y = y; }

  // Arithmetic operators
  Point operator+(const Point& rhs) const { return Point(_x + rhs._x, _y + rhs._y); }
  Point operator-(const Point& rhs) const { return Point(_x - rhs._x, _y - rhs._y); }
  Point operator*(T scalar) const { return Point(_x * scalar, _y * scalar); }
  Point operator/(T scalar) const { return Point(_x / scalar, _y / scalar); }

 private:
  T _x = 0;
  T _y = 0;
};

// Common instantiations
using PointI = Point<int>;
using PointD = Point<double>;
```

### Tree Structure

Topology tree for clock tree synthesis:

```cpp
class TreeNode
{
 public:
  TreeNode* get_parent() const { return _parent; }
  const std::vector<TreeNode*>& get_children() const { return _children; }
  Point<int> get_location() const { return _location; }
  const std::vector<Pin*>& get_loads() const { return _loads; }

  void set_parent(TreeNode* parent) { _parent = parent; }
  void add_child(TreeNode* child) { _children.push_back(child); }
  void set_location(const Point<int>& location) { _location = location; }
  void add_load(Pin* load) { _loads.push_back(load); }

 private:
  TreeNode* _parent = nullptr;
  std::vector<TreeNode*> _children;
  Point<int> _location;
  std::vector<Pin*> _loads;
};

class Tree
{
 public:
  TreeNode* get_root() const { return _root; }
  void set_root(TreeNode* root) { _root = root; }

  TreeNode* makeNode();  // Factory method

 private:
  TreeNode* _root = nullptr;
  std::vector<std::unique_ptr<TreeNode>> _nodes;  // Owns all nodes
};
```

---

## Naming Conventions

### Classes

- **PascalCase**: `Clock`, `Inst`, `Net`, `Pin`, `Config`, `Design`

### Member Variables

- **Underscore prefix + snake_case**: `_clock_name`, `_source_pin`, `_loads`

### Getters/Setters

- **Getters**: `get_<name>()` - returns `const` reference for complex types
- **Setters**: `set_<name>()` - takes value or const reference
- **Adders**: `add_<name>()` - for collection members

```cpp
// Getter returns const reference
const std::string& get_name() const { return _name; }
const std::vector<Pin*>& get_loads() const { return _loads; }

// Setter takes const reference
void set_name(const std::string& name) { _name = name; }

// Adder for collections
void add_load(Pin* load) { _loads.push_back(load); }
```

### Boolean Queries

- **Prefix with `is_`**: `is_buffer()`, `is_flipflop()`, `is_clock_net()`

```cpp
bool is_buffer() const { return _type == InstType::kBuffer; }
bool is_flipflop() const { return _type == InstType::kFlipFlop; }
```

---

## Memory Management

### Ownership Rules

1. **Singletons own their data**:
   - `Design` owns all `Clock*` objects
   - `Clock` owns all inserted `Inst*` and `Net*` objects

2. **Tree owns nodes**:
   - `Tree` uses `std::unique_ptr<TreeNode>` for automatic cleanup
   - Factory method `makeNode()` creates and registers nodes

3. **Raw pointers for references**:
   - Use raw pointers (`Pin*`, `Inst*`) for non-owning references
   - No manual `delete` needed for referenced objects

### Example: Tree Node Factory

```cpp
TreeNode* Tree::makeNode()
{
  auto node = std::make_unique<TreeNode>();
  TreeNode* ptr = node.get();
  _nodes.push_back(std::move(node));
  return ptr;
}
```

---

## External Database Integration

### iDB Wrapper Pattern

The `Wrapper` class provides a bridge to the external iDB database:

```cpp
class Wrapper
{
 public:
  static Wrapper& getInst();

  void init(idb::IdbBuilder* idb) { _idb = idb; }
  void read();  // Read from iDB into internal data model
  void write(); // Write internal data model back to iDB

 private:
  idb::IdbBuilder* _idb = nullptr;
};
```

### Usage Pattern

```cpp
// Initialize wrapper with external DB
CTSWrapperInst.init(idb_builder);

// Read data from iDB
CTSWrapperInst.read();

// Work with internal data model
// ...

// Write results back to iDB
CTSWrapperInst.write();
```

---

## Common Mistakes

### 1. Forgetting Null Checks

**Wrong**:
```cpp
Pin* pin = inst->get_pin("CLK");
auto location = pin->get_location();  // Crash if pin is nullptr
```

**Right**:
```cpp
Pin* pin = inst->get_pin("CLK");
if (pin == nullptr) {
  CTS_LOG_WARNING << "Pin CLK not found";
  return;
}
auto location = pin->get_location();
```

### 2. Modifying Returned Collections

**Wrong**:
```cpp
auto loads = clock->get_loads();  // Copy!
loads.push_back(new_pin);  // Modifies copy, not original
```

**Right**:
```cpp
clock->add_load(new_pin);  // Use adder method
```

### 3. Manual Memory Management

**Wrong**:
```cpp
TreeNode* node = new TreeNode();
// ... use node ...
delete node;  // Manual delete
```

**Right**:
```cpp
TreeNode* node = tree.makeNode();  // Tree owns it
// ... use node ...
// No delete needed
```

### 4. Singleton Copy

**Wrong**:
```cpp
Config config = CTSConfigInst;  // Compile error (deleted copy constructor)
```

**Right**:
```cpp
Config& config = CTSConfigInst;  // Reference
// Or just use CTSConfigInst directly
```
