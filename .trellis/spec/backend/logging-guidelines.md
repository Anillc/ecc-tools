# Logging Guidelines

Logging rules for `src/operation/iCTS/`.

## Scope

This document covers iCTS runtime logging, schema-driven report output, log-level meaning, and logging-specific forbidden patterns.

## Rules

### Core Rule

Use the repository `LOG_*` macros for console/runtime logging.
Use the iCTS schema/report helpers for structured file output such as `cts.log`.
Do not build new dual-write wrappers that hide both console logging and file writing behind one macro.

### Log Levels

| Level | Use For |
|------|---------|
| `LOG_INFO` | Normal progress, summaries, counters, timing, and status |
| `LOG_WARNING` | Non-fatal problems, early skips, optional data missing |
| `LOG_ERROR` | Recoverable errors where the function returns a safe default |
| `LOG_FATAL` | Unrecoverable state that must terminate |

Conditional forms are allowed:
- `LOG_INFO_IF(...)`
- `LOG_WARNING_IF(...)`
- `LOG_ERROR_IF(...)`
- `LOG_FATAL_IF(...)`

### Usage

- Include enough context in the message: object name, value, or expected state.
- Use `INFO` for milestones and summaries, not for every trivial step.
- Use `WARNING` when the flow can continue safely.
- Use `ERROR` when the function must return a safe fallback.
- Use `FATAL` when continuing would mean corrupted or invalid execution.
- Do not repeat file paths or line numbers in the message body; the logger prefix already provides call-site context.
- Prefer titled schema tables or detail blocks for dense summaries such as config, RC, and unit metadata.
- If a value comes from fallback or auto-derivation, warn at the decision point and label the source explicitly in the emitted summary/report.
- Build schema fields near the data owner. API/flow entry layers coordinate stage boundaries and output timing; they should not own low-level field assembly for config, design, or adapter data.

Example:

```cpp
LOG_WARNING << "Topology generation skipped: no loads.";
LOG_ERROR << "iDB design units are not ready.";
LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
```

### Forbidden Patterns

- `std::cout` or `printf`
- `assert()` for user-visible runtime validation
- logging the same invariant failure repeatedly in many downstream locations

### Lifecycle

- Initialize schema/report output at the API or test-flow entry boundary.
- Use `LOG_*` for console/runtime diagnostics throughout the codebase.
- Route structured file output through schema helpers such as stage scopes, titled tables, diagnostics, and artifacts.
- Close schema/report resources during the normal API/reset cleanup path.

## Related Docs

- `error-handling.md`
- `quality-guidelines.md`
- `../project-constraints.md`
