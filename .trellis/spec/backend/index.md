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
| [Quality Guidelines](./quality-guidelines.md) | clang-format, clang-tidy, naming conventions, forbidden patterns, IWYU | Filled |
| [Quality Workflow](../../ecc_dev_tools/) | Repository-local C++ code quality checker (`doctor`, `check` commands) | Filled |

---

## Quick Reference

### File Extensions
- Headers: `.hh` (PascalCase)
- Sources: `.cc` (PascalCase)

### Naming Conventions
Naming conventions are enforced by `readability-identifier-naming` checks in the project's `.clang-tidy` configuration file (`src/utility/.clang-tidy`). Run `python3 ./.trellis/ecc_dev_tools/check.py check --path <path>` to verify compliance.

Key rules:
- Trivial getters/setters: `get_name()`, `set_name()`, `is_buffer()` (snake_case)
- Complex accessors: `camelBack` (e.g., `calcDelay()`, `hasViolation()`)
- Members: `_lower_case` (e.g., `_clock_name`)
- Classes: `PascalCase`, Enums: `enum class` with `kPrefix` values

### Singleton Macros
- `CTS_API_INST` — Main external API entry point
- `DESIGN_INST` — Design database
- `CONFIG_INST` — Configuration
- `WRAPPER_INST` — iDB adapter
- `STA_ADAPTER_INST` — iSTA adapter for internal source-layer use
- `LOG_INST` — Logger

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
