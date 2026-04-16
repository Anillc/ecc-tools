# Component Guidelines

Treat each user-facing CTS command, binding, or GUI action as a thin component.

## Rules
- One command or action should map to one stable backend operation.
- Put option declaration, validation, and dispatch together in the same wrapper.
- Prefer `tool_manager` as the first bridge above CTS core for UI-triggered flows.
- Keep naming aligned across layers when exposing the same feature.
- CTS report wrappers must delegate through `tool_manager` and must not include `CTSAPI.hh` to bypass the shared path.

## Examples
- `CmdCTSAutoRun` in `src/interface/tcl/tcl_icts/tcl_cts.cpp`
- `register_icts` in `src/interface/python/py_icts/py_register_icts.h`
- `MainWindow::CtsCreateNets` in `src/interface/gui/src/mainwindow_cts.cpp`
- `CtsIO::runCTS` in `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`

## Avoid
- A wrapper that both parses UI input and implements CTS algorithms.
- Different public names for the same CTS operation in Tcl and Python.
- Large GUI handlers that bypass the shared platform bridge.
