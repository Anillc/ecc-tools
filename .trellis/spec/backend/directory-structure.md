# Directory Structure

CTS backend is layered, not feature-flat.

## Place Files Here
- `src/operation/iCTS/api/`: public CTS facade only.
- `src/operation/iCTS/source/data_manager/config/`: JSON parsing and runtime config.
- `src/operation/iCTS/source/data_manager/database/`: CTS domain objects.
- `src/operation/iCTS/source/data_manager/io/`: IDB and report I/O adapters.
- `src/operation/iCTS/source/module/`: CTS business-stage components such as flow, session, synthesis, committer, evaluator, and router.
- `src/operation/iCTS/source/utils/`: operators, algorithms, internal synthesis IR/runtime, tree building, timing propagation, and math helpers.
- `src/interface/tcl/tcl_icts/`, `src/interface/python/py_icts/`: thin bindings only.

## Patterns To Follow
- Keep `source/` top-level ownership at `data_manager/module/utils`; do not reintroduce `model/` or `solver/` as peer modules.
- Keep public CTS entrypoints in `CTSAPI`; put flow orchestration under `source/module/flow/`.
- Keep low-level database, utils, and commit services free of public flow lifecycle ownership.
- Keep IDB or STA conversion in wrappers, not inside synthesis or algorithm classes.
- Put synthesis-result commit logic in `source/module/committer/` or an equivalent service module, not inside interface wrappers.
- If a folder exists, give it the nearest `CMakeLists.txt` and a target with matching ownership.
- Prefer target-scoped include paths; do not add relative includes just to reach sibling directories.

## Examples
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/module/flow/CTSFlow.cc`
- `src/operation/iCTS/source/data_manager/io/CtsDBWrapper.cc`
- `src/operation/iCTS/source/module/committer/DesignCommitter.cc`
- `src/operation/iCTS/source/utils/tree_builder/TreeBuilder.cc`
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`

## Avoid
- Calling Tcl, Python, or GUI code from `source/`.
- Mixing config parsing into synthesis or router classes.
- Letting `source/` modules become a second public API surface for flow startup or wrapper dispatch.
- Reintroducing `source/solver/` or `source/model/` as a catch-all layer.
- Using relative includes as an architectural escape hatch.
- Creating one-off top-level folders for a single CTS experiment.
