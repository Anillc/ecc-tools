# Error Handling

> How errors are handled in the iCTS module.

---

## Overview

iCTS uses **logging-based error handling**. There are no exceptions thrown in the codebase. Errors are reported via `CTS_LOG_*` macros at the appropriate severity level, and functions return default values or terminate the process depending on severity.

**Key rule**: No `throw` / `try` / `catch` in iCTS code (with one narrow exception for JSON parsing).

---

## Error Handling Patterns

### Pattern A: Fatal Termination (`CTS_LOG_FATAL_IF`)

**When**: A null pointer or missing resource makes further execution impossible (indicates a bug).

```cpp
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";

CTS_LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << name
    << " type is unknown (not found instance in iDB) which cell is "
    << lib_cell->get_cell_name();

CTS_LOG_FATAL_IF(pin_list == nullptr) << "Instance " << name
    << " type is unknown (none pin in iDB) which cell is "
    << lib_cell->get_cell_name();
```

**Behavior**: Delegates to global `LOG_FATAL`, which terminates the process. No return value needed.

**Use for**:
- Required singletons or builders being null
- Data integrity violations that indicate a programming error
- Missing required database objects

---

### Pattern B: Error + Return Default (`CTS_LOG_ERROR`)

**When**: A required resource is unavailable, but the function can return a safe default.

```cpp
auto* idb_adapter = STAInst->getIDBAdapter();
if (!idb_adapter) {
  CTS_LOG_ERROR << "STA IDB adapter is not ready.";
  return 0.0;
}
```

**Use for**:
- Query functions that cannot produce a valid result
- Infrastructure not yet initialized
- Functions where the caller can handle a zero/false/empty return

---

### Pattern C: Warning + Early Return (`CTS_LOG_WARNING`)

**When**: Preconditions fail in a non-fatal way (empty input, zero count).

```cpp
if (loads.empty()) {
  CTS_LOG_WARNING << "Topology generation skipped: no loads.";
  return tree;  // return default-constructed object
}

if (leaf_count == 0) {
  CTS_LOG_WARNING << "Topology generation skipped: leaf count is zero.";
  return tree;
}
```

**Use for**:
- Empty input collections
- Zero-count edge cases
- Conditions where skipping is acceptable

---

### Pattern D: Conditional Warning (`CTS_LOG_WARNING_IF`)

**When**: A soft failure is non-fatal and processing continues.

```cpp
CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown)
    << "Instance " << name << " type is unknown which cell is " << lib_cell->get_cell_name();

if (!lib_cell) {
  CTS_LOG_WARNING << "Liberty cell " << cell_name << " not found.";
  return 0.0;
}
```

**Use for**:
- Missing optional library data
- Unknown enum states with fallback behavior
- Non-critical data inconsistencies

---

### Pattern E: Silent Fallback (Config.cc only)

**When**: Numeric conversion from JSON strings. This is the **only** place `try/catch` is used.

```cpp
try {
  return std::stod(value.get<std::string>());
} catch (...) {
  return default_value;
}
```

**Restricted to**: `Config.cc` JSON parsing only. Do not use this pattern elsewhere.

---

## Decision Matrix

| Severity | Condition | Action | Return |
|----------|-----------|--------|--------|
| **Fatal** | Required pointer is null (bug) | `CTS_LOG_FATAL_IF(ptr == nullptr)` | Process terminates |
| **Error** | Required resource unavailable | `CTS_LOG_ERROR` | `0.0`, `false`, or empty |
| **Warning** | Empty input / optional missing | `CTS_LOG_WARNING` | Default object or `0.0` |
| **Warning** | Non-critical inconsistency | `CTS_LOG_WARNING_IF(cond)` | Continue processing |

---

## Forbidden Patterns

| Pattern | Why | Use Instead |
|---------|-----|-------------|
| `throw std::runtime_error(...)` | Exceptions not used in iCTS | `CTS_LOG_FATAL` or `CTS_LOG_ERROR` + return |
| `assert(condition)` | No descriptive message, removed in release | `CTS_LOG_FATAL_IF(!condition) << "message"` |
| `try { ... } catch (...)` (outside Config.cc) | Silent error swallowing | Explicit logging + return |
| `exit(1)` or `abort()` | Bypasses cleanup | `CTS_LOG_FATAL << "message"` |
| Error return codes (int) | Not the project pattern | Log + return default value |

---

## Summary

The error handling philosophy in iCTS is:
1. **Fatal errors terminate** — if a required resource is null, it's a bug, stop immediately
2. **Recoverable errors log and return** — use the appropriate log level + a safe default
3. **No exceptions** — errors are communicated through logging, not exception propagation
4. **Always include context** — log messages should include the variable name, value, and what was expected
