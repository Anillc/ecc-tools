# Logging Guidelines

Logging rules for `src/operation/iCTS/`.

## Scope

This document covers `CTS_LOG_*` usage, log-level meaning, and logging-specific forbidden patterns.

## Rules

### Core Rule

Always use `CTS_LOG_*` macros in iCTS code. Do not use global `LOG_*` directly.

### Log Levels

| Level | Use For |
|------|---------|
| `CTS_LOG_INFO` | Normal progress, summaries, counters, timing, and status |
| `CTS_LOG_WARNING` | Non-fatal problems, early skips, optional data missing |
| `CTS_LOG_ERROR` | Recoverable errors where the function returns a safe default |
| `CTS_LOG_FATAL` | Unrecoverable state that must terminate |

Conditional forms are allowed:
- `CTS_LOG_INFO_IF(...)`
- `CTS_LOG_WARNING_IF(...)`
- `CTS_LOG_ERROR_IF(...)`
- `CTS_LOG_FATAL_IF(...)`

### Usage

- Include enough context in the message: object name, value, or expected state.
- Use `INFO` for milestones and summaries, not for every trivial step.
- Use `WARNING` when the flow can continue safely.
- Use `ERROR` when the function must return a safe fallback.
- Use `FATAL` when continuing would mean corrupted or invalid execution.

Example:

```cpp
CTS_LOG_WARNING << "Topology generation skipped: no loads.";
CTS_LOG_ERROR << "iDB design units are not ready.";
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
```

### Forbidden Patterns

- global `LOG_*` macros in iCTS code
- `std::cout` or `printf`
- `assert()` for user-visible runtime validation
- logging the same invariant failure repeatedly in many downstream locations

### Lifecycle

- Initialize logger setup at the API or flow entry boundary.
- Use logging macros throughout the codebase.
- Close logger resources during the normal API/reset cleanup path.

## Related Docs

- `error-handling.md`
- `quality-guidelines.md`
- `../project-constraints.md`
