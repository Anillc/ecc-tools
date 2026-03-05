# Backend Development Guidelines

> Best practices for backend development in the iCTS module.
>
> **Note**: This is a backend-only project (C++ / iCTS). Frontend guidelines are N/A.

---

## Overview

This directory contains guidelines for C++ backend development in `src/operation/iCTS/`. All guidelines are based on actual codebase patterns and conventions.

**Before writing any code**, read `project-constraints.md` for mandatory constraints.

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Project Constraints](../project-constraints.md) | **MANDATORY** — Git read-only, file extensions, copyright, clang-format, clang-tidy, CMake workflow, terminology | Filled |
| [Directory Structure](./directory-structure.md) | Three-tier architecture (api/source/test), module organization, CMake patterns | Filled |
| [Database Guidelines](./database-guidelines.md) | Data model, singleton pattern, memory management, getter/setter conventions | Filled |
| [Error Handling](./error-handling.md) | Logging-based error handling, no exceptions, severity patterns | Filled |
| [Logging Guidelines](./logging-guidelines.md) | CTS_LOG_* macros, log levels, usage patterns | Filled |
| [Quality Guidelines](./quality-guidelines.md) | clang-format, clang-tidy, naming conventions, forbidden patterns | Filled |

---

## Quick Reference

### File Extensions
- Headers: `.hh` (PascalCase)
- Sources: `.cc` (PascalCase)

### Naming Summary
- Classes: `PascalCase` (e.g., `Clock`, `TopologyGen`)
- Methods: `camelBack` (e.g., `runCTS()`)
- Getters/Setters (simple): `get_name()`, `set_name()` — direct member access only
- Getters/Setters (complex): `camelBack` (e.g., `calcDelay()`) — computation/logic involved
- Boolean (simple): `is_buffer()` — direct member comparison
- Boolean (complex): `camelBack` (e.g., `hasViolation()`) — multi-step check
- Members: `_lower_case` (e.g., `_clock_name`)
- Locals: `lower_case` (e.g., `clock_name`)
- Enums: `enum class` with `kPrefix` values
- Namespace: `icts`

### Singleton Macros
- `CTSAPIInst` — Main API
- `CTSDesignInst` — Design database
- `CTSConfigInst` — Configuration
- `CTSWrapperInst` — iDB adapter
- `CTSLogInst` — Logger

### Logging
```cpp
CTS_LOG_INFO << "message";       // Status
CTS_LOG_WARNING << "message";    // Non-fatal
CTS_LOG_ERROR << "message";      // Error
CTS_LOG_FATAL << "message";      // Terminates
```

### CMake-First Workflow
1. Update CMakeLists.txt
2. Create placeholder header
3. Compile to verify
4. Then implement

---

**Language**: All documentation is written in **English**.
