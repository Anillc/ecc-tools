# Error Handling

> How errors are handled in this project.

---

## Overview

This project uses **logging-based error handling** rather than exceptions:
- Fatal errors terminate the program via `CTS_LOG_FATAL`
- Non-fatal errors are logged and execution continues
- Validation uses conditional logging macros (`_IF` variants)
- Early returns for invalid states

**Key principle**: No custom exception classes - use logging levels to indicate severity.

---

## Error Levels

### Fatal Errors (Program Termination)

Use `CTS_LOG_FATAL` or `CTS_LOG_FATAL_IF` for unrecoverable errors:

```cpp
// Null pointer to required resource
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";

// Missing required configuration
CTS_LOG_FATAL_IF(config_file.empty()) << "Configuration file not specified";

// Invalid state that prevents continuation
CTS_LOG_FATAL_IF(idb_inst == nullptr)
    << "Instance " << name << " type is unknown (not found instance in iDB)";
```

**When to use FATAL**:
- Required external resources are missing (database, files)
- Invalid program state that cannot be recovered
- Precondition violations that indicate programming errors

### Errors (Logged, Execution Continues)

Use `CTS_LOG_ERROR` or `CTS_LOG_ERROR_IF` for errors that allow continued execution:

```cpp
// Instance not found in external database
CTS_LOG_ERROR_IF(sta_inst == nullptr)
    << "Instance " << name << " is not found in the STA netlist.";

// Invalid data that can be skipped
CTS_LOG_ERROR << "Failed to process instance " << name << ", skipping";
```

**When to use ERROR**:
- Data validation failures that can be skipped
- Optional operations that fail
- Recoverable external resource issues

### Warnings (Potential Issues)

Use `CTS_LOG_WARNING` or `CTS_LOG_WARNING_IF` for unexpected but acceptable situations:

```cpp
// Unknown instance type (can continue with default)
CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown)
    << "Instance " << name << " type is unknown which cell is " << cell_name;

// Empty input (can return early)
CTS_LOG_WARNING << "Topology generation skipped: no loads.";

// Unexpected configuration
CTS_LOG_WARNING << "Instance " << name << " pin " << pin_name
                << " connected net " << net_name << " is not clock net";
```

**When to use WARNING**:
- Unexpected but valid input
- Degraded functionality
- Configuration issues that don't prevent operation

---

## Error Handling Patterns

### Pattern 1: Conditional Fatal

Check preconditions and terminate if violated:

```cpp
void CTSAPI::init(const std::string& config_file)
{
  // Fatal if required parameter is missing
  CTS_LOG_FATAL_IF(config_file.empty()) << "Configuration file not specified";

  // Fatal if external resource is null
  auto* idb_builder = dmInst->get_idb_builder();
  CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";

  // Continue with initialization...
}
```

### Pattern 2: Early Return on Invalid State

Return early with warning/error for invalid input:

```cpp
Tree TopologyGen::build(const std::vector<Pin*>& loads)
{
  Tree tree;

  // Early return with warning
  if (loads.empty()) {
    CTS_LOG_WARNING << "Topology generation skipped: no loads.";
    return tree;
  }

  const std::size_t leaf_count = calcLeafCount(loads.size());
  if (leaf_count == 0) {
    CTS_LOG_WARNING << "Topology generation skipped: leaf count is zero.";
    return tree;
  }

  // Continue processing...
}
```

### Pattern 3: Null Checks Before Operations

Check for null pointers before dereferencing:

```cpp
void Wrapper::read()
{
  // Early return if null (silent failure)
  if (_idb == nullptr) {
    return;
  }

  auto* def_service = _idb->get_def_service();
  if (def_service == nullptr) {
    return;
  }

  auto* idb_design = def_service->get_design();
  if (idb_design == nullptr) {
    return;
  }

  // Continue processing...
}
```

### Pattern 4: Conditional Error Logging

Log error if condition is true, but continue execution:

```cpp
void CTSAPI::processInstance(const std::string& name)
{
  auto* sta_inst = findSTAInstance(name);

  // Log error but continue (instance will be skipped)
  CTS_LOG_ERROR_IF(sta_inst == nullptr)
      << "Instance " << name << " is not found in the STA netlist.";

  if (sta_inst == nullptr) {
    return;  // Skip this instance
  }

  // Process instance...
}
```

### Pattern 5: Validation with Default Values

Warn about unexpected values but use defaults:

```cpp
icts::InstType queryInstType(const std::string& inst_name)
{
  auto* lib_cell = findLibCell(inst_name);

  icts::InstType inst_type = determineType(lib_cell);

  // Warn if type is unknown, but return it anyway
  CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown)
      << "Instance " << inst_name << " type is unknown which cell is "
      << lib_cell->get_cell_name();

  return inst_type;  // Return kUnknown as default
}
```

---

## Error Propagation

### No Exception Throwing

This project does **not use C++ exceptions**. Instead:

1. **Fatal errors terminate immediately** via `CTS_LOG_FATAL`
2. **Non-fatal errors return early** with default/empty values
3. **Callers check return values** for validity

### Return Value Patterns

**Return empty/default on error**:

```cpp
Tree TopologyGen::build(const std::vector<Pin*>& loads)
{
  Tree tree;  // Default-constructed (empty)

  if (loads.empty()) {
    CTS_LOG_WARNING << "Topology generation skipped: no loads.";
    return tree;  // Return empty tree
  }

  // Build tree...
  return tree;
}
```

**Return nullptr on error**:

```cpp
Pin* findPin(const std::string& name)
{
  auto it = _pins.find(name);
  if (it == _pins.end()) {
    CTS_LOG_WARNING << "Pin " << name << " not found";
    return nullptr;  // Caller must check
  }
  return it->second;
}
```

**Return boolean success flag**:

```cpp
bool processData()
{
  if (!validateInput()) {
    CTS_LOG_ERROR << "Input validation failed";
    return false;
  }

  // Process...
  return true;
}
```

---

## Validation Patterns

### Input Validation

```cpp
void setSkewBound(double skew_bound)
{
  // Validate range
  CTS_LOG_WARNING_IF(skew_bound < 0.0)
      << "Skew bound " << skew_bound << " is negative, using 0.0";

  _skew_bound = std::max(0.0, skew_bound);
}
```

### Pointer Validation

```cpp
void processPin(Pin* pin)
{
  // Fatal if required pointer is null
  CTS_LOG_FATAL_IF(pin == nullptr) << "Pin pointer is null";

  // Continue processing...
}
```

### Collection Validation

```cpp
void processLoads(const std::vector<Pin*>& loads)
{
  // Early return if empty
  if (loads.empty()) {
    CTS_LOG_WARNING << "No loads to process";
    return;
  }

  // Process loads...
}
```

---

## API Error Responses

### Public API Pattern

Public APIs use logging and return values to indicate errors:

```cpp
class CTSAPI
{
 public:
  // Returns void, logs fatal errors
  void runCTS()
  {
    CTS_LOG_FATAL_IF(!_initialized) << "API not initialized";

    // Run CTS...
  }

  // Returns query result, logs warnings for invalid input
  icts::InstType queryInstType(const std::string& inst_name) const
  {
    auto* inst = findInstance(inst_name);
    CTS_LOG_WARNING_IF(inst == nullptr) << "Instance " << inst_name << " not found";

    return inst ? inst->get_type() : icts::InstType::kUnknown;
  }

  // Returns boolean success flag
  bool isClockNet(const std::string& net_name) const
  {
    auto* net = findNet(net_name);
    if (net == nullptr) {
      CTS_LOG_WARNING << "Net " << net_name << " not found";
      return false;
    }
    return net->is_clock_net();
  }
};
```

---

## Common Mistakes

### 1. Using FATAL for Recoverable Errors

**Wrong**:
```cpp
CTS_LOG_FATAL_IF(loads.empty()) << "No loads found";  // This is recoverable!
```

**Right**:
```cpp
if (loads.empty()) {
  CTS_LOG_WARNING << "No loads found";
  return;  // Early return
}
```

### 2. Not Checking Return Values

**Wrong**:
```cpp
Pin* pin = findPin(name);
auto location = pin->get_location();  // Crash if pin is nullptr
```

**Right**:
```cpp
Pin* pin = findPin(name);
if (pin == nullptr) {
  CTS_LOG_ERROR << "Pin " << name << " not found";
  return;
}
auto location = pin->get_location();
```

### 3. Silent Failures

**Wrong**:
```cpp
if (loads.empty()) {
  return;  // No indication of why we returned early
}
```

**Right**:
```cpp
if (loads.empty()) {
  CTS_LOG_WARNING << "Topology generation skipped: no loads.";
  return;
}
```

### 4. Redundant Checks

**Wrong**:
```cpp
if (ptr == nullptr) {
  CTS_LOG_FATAL << "Pointer is null";
}
```

**Right**:
```cpp
CTS_LOG_FATAL_IF(ptr == nullptr) << "Pointer is null";
```

### 5. Not Providing Context

**Wrong**:
```cpp
CTS_LOG_ERROR << "Not found";
```

**Right**:
```cpp
CTS_LOG_ERROR << "Instance " << inst_name << " not found in STA netlist";
```

---

## Best Practices

### 1. Fail Fast for Programming Errors

Use `CTS_LOG_FATAL` for precondition violations:

```cpp
void processData(Data* data)
{
  CTS_LOG_FATAL_IF(data == nullptr) << "Data pointer is null";
  // This should never happen - indicates a bug
}
```

### 2. Fail Gracefully for User Errors

Use `CTS_LOG_ERROR` or `CTS_LOG_WARNING` for invalid user input:

```cpp
void loadConfig(const std::string& file)
{
  if (!std::filesystem::exists(file)) {
    CTS_LOG_ERROR << "Config file " << file << " does not exist";
    return;  // Use defaults
  }
}
```

### 3. Provide Actionable Error Messages

Include enough context for debugging:

```cpp
// Bad
CTS_LOG_ERROR << "Invalid value";

// Good
CTS_LOG_ERROR << "Invalid skew bound " << skew_bound
              << " for clock " << clock_name
              << " (must be positive)";
```

### 4. Use Early Returns

Avoid deep nesting with early returns:

```cpp
// Bad
void process() {
  if (valid) {
    if (initialized) {
      if (data != nullptr) {
        // Deep nesting...
      }
    }
  }
}

// Good
void process() {
  if (!valid) {
    CTS_LOG_ERROR << "Invalid state";
    return;
  }
  if (!initialized) {
    CTS_LOG_ERROR << "Not initialized";
    return;
  }
  if (data == nullptr) {
    CTS_LOG_ERROR << "Data is null";
    return;
  }
  // Process...
}
```

### 5. Document Error Conditions

Use comments to document expected error conditions:

```cpp
/**
 * @brief Build topology tree from load pins
 * @param loads Load pins to connect
 * @return Tree structure (empty if loads is empty)
 * @note Returns empty tree with warning if loads is empty
 */
Tree build(const std::vector<Pin*>& loads);
```
