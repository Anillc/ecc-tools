# Directory Structure

The CTS "frontend" is an interface layer around backend services.

## Place Files Here
- Tcl commands: `src/interface/tcl/tcl_icts/`
- Python bindings: `src/interface/python/py_icts/`
- GUI actions: `src/interface/gui/`
- Shared flow bridge: `src/platform/tool_manager/tool_api/icts_io/`

## Patterns To Follow
- Keep command parsing in interface files and execution in `tool_manager` or `CTSAPI`.
- Register commands or bindings near the wrapper implementation.
- Keep GUI actions small and delegate real work immediately.
- Update the nearest `CMakeLists.txt` when adding a new wrapper file.

## Examples
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`
- `src/interface/tcl/tcl_icts/tcl_register_cts.h`
- `src/interface/python/py_icts/py_icts.cpp`
- `src/interface/gui/src/mainwindow_cts.cpp`

## Avoid
- Calling deep CTS internals directly from multiple UI layers.
- Hiding business logic inside Qt slots or pybind lambdas.
- Creating a new interface directory for a single command.
