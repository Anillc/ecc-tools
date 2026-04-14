# Quality Guidelines

## Coding Bar
- Keep interface files readable and small; wrappers should mostly declare options and delegate.
- Reuse existing command names and binding patterns before adding a new public surface.
- When adding a new wrapper, register it in the matching `register_*` file and build script.
- Log failures at the bridge point instead of silently swallowing backend errors.

## Verification
- Manually run the affected Tcl command or Python binding after signature changes.
- Rebuild the nearest interface target after adding new files.
- If GUI behavior changes, smoke-test the menu or toolbar action path.

## Examples
- `src/interface/tcl/tcl_icts/tcl_register_cts.h`
- `src/interface/python/py_icts/py_register_icts.h`
- `src/interface/gui/src/mainwindow_cts.cpp`
- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`

## Avoid
- Adding wrapper code without registration.
- Duplicating the same CTS command in multiple styles with inconsistent behavior.
- Letting interface code become the only place that knows the workflow.
