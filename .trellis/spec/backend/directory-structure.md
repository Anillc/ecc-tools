# Directory Structure

CTS backend is layered, not feature-flat.

## Place Files Here
- `src/operation/iCTS/api/`: public CTS entrypoints and flow-level helpers.
- `src/operation/iCTS/source/data_manager/config/`: JSON parsing and runtime config.
- `src/operation/iCTS/source/data_manager/database/`: CTS domain objects.
- `src/operation/iCTS/source/data_manager/io/`: IDB and report I/O adapters.
- `src/operation/iCTS/source/solver/`: synthesis flow and tree-building algorithms.
- `src/operation/iCTS/source/module/`: reusable services shared by solver or API.
- `src/interface/tcl/tcl_icts/`, `src/interface/python/py_icts/`: thin bindings only.

## Patterns To Follow
- Keep orchestration in `CTSAPI`, not in low-level database or algorithm classes.
- Keep IDB or STA conversion in wrappers, not inside solver classes.
- Add new source folders through the nearest `CMakeLists.txt`.

## Examples
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/data_manager/io/CtsDBWrapper.cc`
- `src/operation/iCTS/source/solver/tools/tree_builder/TreeBuilder.cc`
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`

## Avoid
- Calling Tcl, Python, or GUI code from `source/`.
- Mixing config parsing into solver or router classes.
- Creating one-off top-level folders for a single CTS experiment.
