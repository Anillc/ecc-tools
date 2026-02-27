# Logging Guidelines

> How logging is done in this project.

---

## Overview

Each module has its own **module-specific logger** that wraps the global logging system.

**For iCTS module**:
- Logger location: `src/operation/iCTS/source/utils/logger/Logger.hh`
- Use `CTS_LOG_*` macros (NOT global `LOG_*` macros)
- Dual output: file + console
- Thread-safe with `std::mutex`
- Stream-based API (supports `operator<<`)

**Key principle**: Use module-specific logging macros to maintain clear log ownership.

---

## Log Levels

### Available Levels

```cpp
enum class Level
{
  kInfo,     // Normal operation information
  kWarning,  // Potential issues that don't stop execution
  kError,    // Errors that allow continued execution
  kFatal     // Critical errors that terminate the program
};
```

### When to Use Each Level

| Level | When to Use | Example |
|-------|-------------|---------|
| `INFO` | Normal operation milestones, progress updates | "Flow memory usage 123MB", "Processing clock [clk]" |
| `WARNING` | Unexpected but recoverable situations | "Instance type is unknown", "Topology generation skipped: no loads" |
| `ERROR` | Errors that allow continued execution | "Instance not found in STA netlist", "Pin connected net is not clock net" |
| `FATAL` | Critical errors requiring immediate termination | "idb builder is null", "Required configuration missing" |

---

## Logging Macros

### Basic Macros

Located in `src/operation/iCTS/source/utils/logger/Logger.hh`:

```cpp
#define CTS_LOG_INFO \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kInfo, __FILE__, __LINE__)

#define CTS_LOG_WARNING \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kWarning, __FILE__, __LINE__)

#define CTS_LOG_ERROR \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kError, __FILE__, __LINE__)

#define CTS_LOG_FATAL \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kFatal, __FILE__, __LINE__)
```

### Conditional Macros

Log only if condition is true:

```cpp
#define CTS_LOG_INFO_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kInfo, __FILE__, __LINE__, (condition))

#define CTS_LOG_WARNING_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kWarning, __FILE__, __LINE__, (condition))

#define CTS_LOG_ERROR_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kError, __FILE__, __LINE__, (condition))

#define CTS_LOG_FATAL_IF(condition) \
  ::icts::Logger::Stream(&::icts::Logger::getInst(), ::icts::Logger::Level::kFatal, __FILE__, __LINE__, (condition))
```

---

## Usage Examples

### Basic Logging

```cpp
// Info: normal operation
CTS_LOG_INFO << "Flow memory usage " << stats.memoryDelta() << "MB";

// Info with context
CTS_LOG_INFO << "Clock [" << clock_name << "] have net \"" << net_name << "\"";

// Warning: unexpected but recoverable
CTS_LOG_WARNING << "Instance " << name << " type is unknown which cell is " << cell_name;

// Error: problem but can continue
CTS_LOG_ERROR << "Instance " << name << " is not found in the STA netlist.";

// Fatal: must terminate
CTS_LOG_FATAL << "idb builder is null";
```

### Conditional Logging

```cpp
// Fatal if condition is true (terminates program)
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";

// Error if condition is true
CTS_LOG_ERROR_IF(sta_inst == nullptr)
    << "Instance " << name << " is not found in the STA netlist.";

// Warning if condition is true
CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown)
    << "Instance " << name << " type is unknown which cell is " << cell_name;
```

### Early Return Pattern

Combine logging with early return for cleaner code:

```cpp
Tree TopologyGen::build(const std::vector<Pin*>& loads)
{
  Tree tree;
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

---

## What to Log

### DO Log

1. **Flow milestones**:
   ```cpp
   CTS_LOG_INFO << "Starting clock tree synthesis";
   CTS_LOG_INFO << "Topology generation completed";
   ```

2. **Resource usage**:
   ```cpp
   CTS_LOG_INFO << "Flow memory usage " << memory_mb << "MB";
   CTS_LOG_INFO << "Processing " << clock_count << " clocks";
   ```

3. **Important decisions**:
   ```cpp
   CTS_LOG_INFO << "Using buffer type: " << buffer_type;
   CTS_LOG_INFO << "Clustering into " << cluster_count << " groups";
   ```

4. **Validation failures**:
   ```cpp
   CTS_LOG_ERROR_IF(sta_inst == nullptr) << "Instance not found: " << name;
   CTS_LOG_WARNING_IF(loads.empty()) << "No loads for clock: " << clock_name;
   ```

5. **Configuration issues**:
   ```cpp
   CTS_LOG_FATAL_IF(config_file.empty()) << "Configuration file not specified";
   ```

### DO NOT Log

1. **Sensitive data**: API keys, passwords, credentials
2. **Personal information**: User names, email addresses (unless necessary)
3. **Excessive detail in loops**:
   ```cpp
   // BAD: Logs once per pin (could be thousands)
   for (auto* pin : pins) {
     CTS_LOG_INFO << "Processing pin " << pin->get_name();
   }

   // GOOD: Log summary
   CTS_LOG_INFO << "Processing " << pins.size() << " pins";
   ```

4. **Debug spam**: Temporary debug logs should be removed before commit

---

## Structured Logging

### Stream-Based API

The logger uses C++ stream operators for flexible formatting:

```cpp
CTS_LOG_INFO << "Clock [" << clock_name << "] has " << load_count << " loads";
```

### Automatic Features

- **Newline appending**: No need to add `\n`
- **File location**: Automatically includes `__FILE__` and `__LINE__`
- **Thread safety**: Protected by `std::mutex`
- **Dual output**: Writes to both file and console

### Log Format

```
[INFO] [CTSAPI.cc:66] Flow memory usage 123MB
[WARNING] [TopologyGen.cc:85] Topology generation skipped: no loads.
[ERROR] [CTSAPI.cc:235] Instance inst_1 is not found in the STA netlist.
[FATAL] [CTSAPI.cc:183] idb builder is null
```

---

## Logger Implementation

### Singleton Pattern

```cpp
class Logger
{
 public:
  static Logger& getInst()
  {
    static Logger instance;
    return instance;
  }

  void init(const std::string& log_file);
  void log(Level level, const std::string& file, int line, const std::string& message);

 private:
  Logger() = default;
  ~Logger() = default;

  std::mutex _mutex;
  std::string _log_file;
};

#define CTSLogInst (icts::Logger::getInst())
```

### Stream Helper

```cpp
class Logger::Stream
{
 public:
  Stream(Logger* logger, Level level, const char* file, int line, bool condition = true)
      : _logger(logger), _level(level), _file(file), _line(line), _condition(condition) {}

  ~Stream()
  {
    if (_condition) {
      _logger->log(_level, _file, _line, _stream.str());
    }
  }

  template <typename T>
  Stream& operator<<(const T& value)
  {
    if (_condition) {
      _stream << value;
    }
    return *this;
  }

 private:
  Logger* _logger;
  Level _level;
  const char* _file;
  int _line;
  bool _condition;
  std::ostringstream _stream;
};
```

---

## Best Practices

### 1. Use Module-Specific Macros

**Wrong**:
```cpp
LOG_INFO << "CTS: Processing clock";  // Global logger
```

**Right**:
```cpp
CTS_LOG_INFO << "Processing clock";  // Module-specific logger
```

### 2. Provide Context

**Wrong**:
```cpp
CTS_LOG_ERROR << "Instance not found";
```

**Right**:
```cpp
CTS_LOG_ERROR << "Instance " << inst_name << " not found in STA netlist";
```

### 3. Use Conditional Macros for Validation

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

### 4. Log at Appropriate Granularity

**Wrong**:
```cpp
for (auto* inst : instances) {
  CTS_LOG_INFO << "Processing instance " << inst->get_name();  // Too verbose
}
```

**Right**:
```cpp
CTS_LOG_INFO << "Processing " << instances.size() << " instances";
// ... process instances ...
CTS_LOG_INFO << "Completed processing instances";
```

### 5. Use FATAL Sparingly

Only use `CTS_LOG_FATAL` for truly unrecoverable errors:

```cpp
// Good use of FATAL
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";

// Bad use of FATAL (should be ERROR or WARNING)
CTS_LOG_FATAL_IF(loads.empty()) << "No loads found";  // This is recoverable
```

---

## Common Mistakes

### 1. Forgetting Module Prefix

**Wrong**:
```cpp
LOG_INFO << "Message";  // Uses global logger
```

**Right**:
```cpp
CTS_LOG_INFO << "Message";  // Uses CTS logger
```

### 2. Adding Manual Newlines

**Wrong**:
```cpp
CTS_LOG_INFO << "Message\n";  // Newline added automatically
```

**Right**:
```cpp
CTS_LOG_INFO << "Message";
```

### 3. Logging in Tight Loops

**Wrong**:
```cpp
for (int i = 0; i < 10000; ++i) {
  CTS_LOG_INFO << "Iteration " << i;  // 10000 log lines!
}
```

**Right**:
```cpp
CTS_LOG_INFO << "Starting 10000 iterations";
for (int i = 0; i < 10000; ++i) {
  // ... work ...
}
CTS_LOG_INFO << "Completed 10000 iterations";
```

### 4. Not Using Conditional Macros

**Wrong**:
```cpp
if (condition) {
  CTS_LOG_ERROR << "Error message";
}
```

**Right**:
```cpp
CTS_LOG_ERROR_IF(condition) << "Error message";
```
