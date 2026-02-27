# Backend Development Guidelines

> Best practices for backend development in this project.

---

## ⚠️ IMPORTANT: Read Project Constraints First

**Before starting any development**, read the mandatory project constraints:

📋 **[Project Constraints](../project-constraints.md)** - MANDATORY reading

This document covers:
- Git read-only policy (no commit/push for AI)
- File naming requirements (.cc/.hh only)
- Copyright headers (Mulan PSL v2)
- Header guards (#pragma once)
- EDA terminology standards
- Code style enforcement (clang-format)
- Static analysis (clang-tidy)
- CMake-first workflow

---

## Overview

This directory contains guidelines for C++ backend development in the iCTS (Clock Tree Synthesis) module and similar EDA tools.

**Project Type**: C++ EDA tool with C++20, CMake build system, and pybind11 Python bindings.

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Module organization and file layout | ✅ Filled |
| [Database Guidelines](./database-guidelines.md) | Data model patterns, singletons, memory management | ✅ Filled |
| [Error Handling](./error-handling.md) | Error types, handling strategies | ✅ Filled |
| [Quality Guidelines](./quality-guidelines.md) | Code standards, forbidden patterns | ✅ Filled |
| [Logging Guidelines](./logging-guidelines.md) | Structured logging, log levels | ✅ Filled |

---

## Quick Reference

### File Naming
- Headers: `.hh` extension
- Source: `.cc` extension
- Naming: PascalCase (e.g., `TopologyGen.hh`)

### Code Style
- Format: clang-format (Google style, 140 char limit)
- Standard: C++20
- License: Mulan PSL v2 header required

### Naming Conventions
- Classes: `PascalCase`
- Member variables: `_snake_case` (underscore prefix)
- Methods: `camelBack` (except getters/setters: `get_name()`)
- Enums: `kPascalCase` (k-prefix)
- Namespaces: `lowercase`

### Logging
- Use module-specific macros: `CTS_LOG_INFO`, `CTS_LOG_WARNING`, `CTS_LOG_ERROR`, `CTS_LOG_FATAL`
- NOT global `LOG_*` macros

### Error Handling
- No exceptions - use logging levels
- Fatal errors terminate via `CTS_LOG_FATAL`
- Non-fatal errors log and return early

### Data Model
- Singletons for global resources (Config, Design, Wrapper, Logger)
- Access via macros: `CTSConfigInst`, `CTSDesignInst`, etc.
- Smart pointers for ownership, raw pointers for references

---

## Development Workflow

### Before You Start

1. **Read project constraints**: [../project-constraints.md](../project-constraints.md)
2. **Read the relevant guideline** for your task
3. **Look at existing code** in `src/operation/iCTS/` for examples
4. **Follow the patterns** documented in these guidelines

### CMake-First Workflow

When creating new files:
1. Update CMakeLists.txt
2. Create header with declarations only
3. Create source with stub implementations
4. Compile to verify
5. Implement actual logic

See [project-constraints.md § CMake-First Workflow](../project-constraints.md#cmake-first-workflow) for details.

### Pre-Commit Checklist

Before committing (human responsibility):
```bash
# Run pre-commit checks
./.trellis/scripts/pre-commit-check.sh

# Or manually:
# 1. Format check
git diff --cached --name-only | grep -E '\.(cc|hh)$' | xargs clang-format -i

# 2. Build check
cd build && make

# 3. Verify changes
git diff
```

---

## Tools and Configuration

### clang-format
- Config: `.clang-format` (project root)
- Run: `clang-format -i <file>`

### clang-tidy
- Config: `src/utility/.clang-tidy`
- Run: `clang-tidy <file> -- -std=c++20`

### Pre-commit Script
- Location: `.trellis/scripts/pre-commit-check.sh`
- Checks: file extensions, copyright, header guards, formatting, forbidden patterns

---

**Language**: All documentation should be written in **English**.
