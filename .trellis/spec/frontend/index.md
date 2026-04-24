# Frontend Guidelines

Scope: user-facing CTS entrypoints. In this repo that means Tcl, Python, and GUI wrappers rather than a web app.

## Pre-Development Checklist
- Read [directory-structure.md](./directory-structure.md).
- Read [component-guidelines.md](./component-guidelines.md).
- Read [hook-guidelines.md](./hook-guidelines.md).
- Read [state-management.md](./state-management.md).
- Read [type-safety.md](./type-safety.md).
- Read [quality-guidelines.md](./quality-guidelines.md).
- Read [../guides/index.md](../guides/index.md).

## Interface Map
- `src/interface/tcl/tcl_icts/`: shell commands and option parsing.
- `src/interface/python/py_icts/`: pybind registration and thin wrappers.
- `src/interface/gui/`: Qt actions that trigger CTS flows.
- `src/platform/tool_manager/tool_api/icts_io/`: stable bridge from UI to CTS core.

## Quality Check
- Re-read `quality-guidelines.md`, `type-safety.md`, and `hook-guidelines.md`.
- Confirm registration files, build scripts, and manual smoke paths still match the wrapper change.

## Canonical Examples
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`
- `src/interface/python/py_icts/py_register_icts.h`
- `src/interface/gui/src/mainwindow_cts.cpp`
- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`
