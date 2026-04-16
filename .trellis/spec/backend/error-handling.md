# Error Handling

Error-handling rules for `src/operation/iCTS/`.

## Scope

This document covers the no-exception policy, severity decisions, and return-vs-terminate behavior.

## Rules

### Core Rules

- iCTS uses logging-based error handling plus schema-based structured reports.
- Do not use exceptions in normal iCTS code.
- Choose log level based on whether the flow can continue.
- Return a safe default only when the caller can continue safely.

### Decision Matrix

| Situation | Action |
|-----------|--------|
| Required pointer/resource is missing and execution cannot continue | `LOG_FATAL` / `LOG_FATAL_IF` |
| Required resource is missing but the function can return safely | `LOG_ERROR` + safe default |
| Input is empty, zero, or intentionally skippable | `LOG_WARNING` + early return |
| Non-critical inconsistency with a fallback path | `LOG_WARNING` / `LOG_WARNING_IF` |

### Return vs Terminate

Use `LOG_ERROR` plus a safe return for cases such as:
- unavailable infrastructure in a query path
- lookup failures with a defined default result
- feature paths where the caller already handles failure

Use `LOG_FATAL` for cases such as:
- null builders or required singletons
- missing required database objects that indicate a bug
- invalid state where any continuation would be misleading or unsafe

### Narrow Exception Rule

The repository rule is no exceptions.

A narrow existing exception may remain in config parsing code when converting JSON values with a default fallback. Do not copy that pattern into normal module code.

### Forbidden Patterns

- `throw`, `try`, or `catch` in normal iCTS implementation code
- `exit()` or `abort()` instead of `LOG_FATAL`
- `assert()` as runtime error handling
- silent failure without logging when the caller needs diagnostic context

## Checklist

Before handoff, verify:

- [ ] Fatal conditions really require termination
- [ ] Recoverable failures return a safe default
- [ ] Empty or skippable inputs use warning + early return
- [ ] No exception-based control flow was introduced
- [ ] Error messages include enough context to debug the failure

## Related Docs

- `logging-guidelines.md`
- `quality-guidelines.md`
- `../project-constraints.md`
