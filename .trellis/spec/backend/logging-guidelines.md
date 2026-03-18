# Logging Guidelines

> How logging is done in the iCTS module.

---

## Overview

iCTS uses its own logging system (`CTS_LOG_*` macros) that provides **dual output**: file logging and console output. The console output delegates to the global `LOG_*` macros from `log/Log.hh` with embedded file/line info.

**Key rule**: Always use `CTS_LOG_*` macros in iCTS code. Never use global `LOG_*` directly.

---

## Logger Architecture

- **Singleton**: `Logger` class accessed via `LogInst` macro
- **Defined in**: `source/utils/logger/Logger.hh` and `Logger.cc`
- **Thread-safe**: File writes protected by `std::mutex`
- **Dual output**: Every message goes to both file and console

```
CTS_LOG_INFO << "message"
    │
    ├──> File output:  [INFO] message   (via Logger::write())
    └──> Console output: LOG_INFO(message)  (via Logger::log_to_console())
```

---

## Log Levels

| Level | Macro | Conditional Variant | Severity |
|-------|-------|---------------------|----------|
| Info | `CTS_LOG_INFO` | `CTS_LOG_INFO_IF(cond)` | Normal operation status |
| Warning | `CTS_LOG_WARNING` | `CTS_LOG_WARNING_IF(cond)` | Non-fatal issue, continues |
| Error | `CTS_LOG_ERROR` | `CTS_LOG_ERROR_IF(cond)` | Error, continues execution |
| Fatal | `CTS_LOG_FATAL` | `CTS_LOG_FATAL_IF(cond)` | Unrecoverable, terminates process |

---

## Usage Syntax

### Unconditional Logging

```cpp
CTS_LOG_INFO << "Clock [" << clock_name << "] have net \"" << net_name << "\"";
CTS_LOG_WARNING << "Topology generation skipped: no loads.";
CTS_LOG_ERROR << "STA IDB adapter is not ready.";
CTS_LOG_FATAL << "idb builder is null";
```

### Conditional Logging

The `_IF` variants suppress output when the condition is `false`:

```cpp
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
CTS_LOG_ERROR_IF(sta_net == nullptr) << "Net " << net_name << " is not found in the STA netlist.";
CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown)
    << "Instance " << name << " type is unknown which cell is " << lib_cell->get_cell_name();
```

---

## When to Use Each Level

### INFO — Normal operation status

Use for:
- Performance/resource bookkeeping
- Section headers and summary tables
- Per-item processing status
- Flow milestone markers

```cpp
// Performance metrics
CTS_LOG_INFO << "**Flow memory usage " << stats.memoryDelta() << "MB";
CTS_LOG_INFO << "**Flow elapsed time " << stats.elapsedRunTime() << "s";

// Section headers
CTS_LOG_INFO << "======== Clock Distribution Summary ========";

// Summary data
CTS_LOG_INFO << "Clock: " << clock_name << ", #Net: " << num_nets
             << ", #Total Sinks: " << num_total_sinks;
```

### WARNING — Non-fatal issues

Use for:
- Empty input causing early return
- Missing optional data
- Unknown enum states that have a fallback

```cpp
// Early return due to empty input
if (loads.empty()) {
  CTS_LOG_WARNING << "Topology generation skipped: no loads.";
  return tree;
}

// Missing optional library cell
if (!lib_cell) {
  CTS_LOG_WARNING << "Liberty cell " << cell_name << " not found.";
  return 0.0;
}

// Unknown type with fallback
CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown)
    << "Instance " << name << " type is unknown which cell is " << lib_cell->get_cell_name();
```

### ERROR — Errors (continues execution)

Use for:
- Missing required infrastructure that has a safe default return
- Query functions that cannot produce a valid result

```cpp
auto* idb_design = WrapperInst.get_idb_design();
if (idb_design == nullptr || idb_design->get_units() == nullptr) {
  CTS_LOG_ERROR << "iDB design units are not ready.";
  return 1;
}
```

### FATAL — Unrecoverable (terminates process)

Use for:
- Null pointers that make further execution impossible
- Missing required data that indicates a bug

```cpp
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";

CTS_LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << name
    << " type is unknown (not found instance in iDB) which cell is "
    << lib_cell->get_cell_name();
```

---

## Forbidden Patterns

| Pattern | Why | Use Instead |
|---------|-----|-------------|
| `LOG_INFO << "..."` | Bypasses iCTS file logging | `CTS_LOG_INFO << "..."` |
| `std::cout << "..."` | No log level, no file output | `CTS_LOG_INFO << "..."` |
| `printf(...)` | Not C++, no log level | `CTS_LOG_INFO << "..."` |
| `assert(condition)` | No descriptive message | `CTS_LOG_FATAL_IF(!condition) << "..."` |

---

## Logger Lifecycle

```cpp
// Initialization (in CTSAPI::init or similar entry point)
LogInst.set_log_file(log_file_path);

// Usage throughout code
CTS_LOG_INFO << "Processing...";

// Cleanup (in CTSAPI::resetAPI)
LogInst.close();
```
