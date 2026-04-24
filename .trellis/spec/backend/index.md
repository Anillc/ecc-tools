# Backend Guidelines

Scope: C++ backend work, especially CTS core changes under `src/operation/iCTS/`.

## Pre-Development Checklist
- Read [directory-structure.md](./directory-structure.md).
- Read [database-guidelines.md](./database-guidelines.md).
- Read [error-handling.md](./error-handling.md).
- Read [logging-guidelines.md](./logging-guidelines.md).
- Read [quality-guidelines.md](./quality-guidelines.md).
- Read [../guides/index.md](../guides/index.md).
- Read `cross-layer-thinking-guide.md` when touching `interface/`, `platform/`, or config payloads.

## Module Map
- `api/`: public CTS facade only; keep external entrypoints in `CTSAPI`.
- `source/data_manager/`: config parsing, DB wrappers, CTS domain objects.
- `source/module/`: business-stage services such as `flow`, `session`, `synthesis`, `committer`, `evaluator`, and `router`.
- `source/utils/`: operators, algorithms, internal synthesis IR/runtime, tree builders, timing propagation, and math helpers.
- `src/platform/tool_manager/tool_api/icts_io/`: platform bridge above CTS core.

## Quality Check
- Re-read `quality-guidelines.md`, `error-handling.md`, and `logging-guidelines.md`.
- Confirm the nearest `CMakeLists.txt`, target links, include paths, tests, and interface callers were updated when required.
- Structural or report changes must be rechecked with `clang-format`, `clang-tidy` against `src/utility/.clang-tidy`, and the `ics55_dev` CTS script.

## Canonical Examples
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/module/flow/CTSFlow.cc`
- `src/operation/iCTS/source/utils/synthesis_operator/TopologyBuilderOperator.cc`
- `src/operation/iCTS/source/data_manager/io/CtsDBWrapper.cc`
- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`
