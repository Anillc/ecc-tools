# Hook Guidelines

CTS does not have a React hook layer. The comparable extension points are Tcl `exec`, Qt slots, and pybind wrappers.

## Rules
- Keep callbacks side-effect-light and hand work to `tool_manager` or `CTSAPI`.
- Perform required option checks before dispatching backend calls.
- Preserve a stable wrapper signature when the backend call is unchanged.
- If multiple interfaces expose the same flow, keep parameter order and names aligned.

## Examples
- `CmdCTSAutoRun::exec` in `src/interface/tcl/tcl_icts/tcl_cts.cpp`
- `ctsAutoRun` in `src/interface/python/py_icts/py_icts.cpp`
- `MainWindow::CtsCreateNets` in `src/interface/gui/src/mainwindow_cts.cpp`

## Avoid
- Embedding config parsing or file I/O directly in the callback.
- Adding wrapper-only state that can drift from backend state.
- Changing one interface signature without updating the others.
