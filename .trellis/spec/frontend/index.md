# Frontend Development Guidelines

> Best practices for frontend development in this project.

---

## Overview

**This project does not have a traditional frontend.**

This is a C++ EDA (Electronic Design Automation) tool with:
- C++ backend modules (primary codebase)
- Python bindings via pybind11 (for scripting)
- MCP server interface (for AI integration)

Frontend guidelines are **not applicable** to this project.

---

## Python Interface

For Python bindings and scripting, see:
- Python bindings: `src/interface/python/`
- MCP server: `src/interface/mcp-iEDA/`

Python code should follow:
- PEP 8 style guide
- Type hints for function signatures
- Docstrings for public APIs

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Module organization and file layout | N/A |
| [Component Guidelines](./component-guidelines.md) | Component patterns, props, composition | N/A |
| [Hook Guidelines](./hook-guidelines.md) | Custom hooks, data fetching patterns | N/A |
| [State Management](./state-management.md) | Local state, global state, server state | N/A |
| [Quality Guidelines](./quality-guidelines.md) | Code standards, forbidden patterns | N/A |
| [Type Safety](./type-safety.md) | Type patterns, validation | N/A |

---

**Note**: If you need frontend development guidelines, refer to the backend guidelines which contain the primary development standards for this project.

---

**Language**: All documentation should be written in **English**.
