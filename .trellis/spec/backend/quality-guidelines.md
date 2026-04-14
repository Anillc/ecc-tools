# Quality Guidelines

## Coding Bar
- Match existing C++ style: file banner, `namespace icts`, `.hh/.cc` pairing, and local `CMakeLists.txt` updates.
- Prefer extending existing singletons and helpers before adding a parallel service path.
- Keep wrappers thin: interface code should delegate to `tool_manager` or `CTSAPI`, not rebuild CTS logic.
- Respect current ownership style. If you introduce `new`, make the long-lived owner explicit.

## Verification
- Build the nearest CTS targets after structural changes.
- Add or update `gtest` cases under `src/operation/iCTS/test/` for algorithm changes.
- Manually exercise Tcl or Python bindings when interface signatures change.

## Examples
- `src/operation/iCTS/source/CMakeLists.txt`
- `src/operation/iCTS/test/TreeBuilderTest.cc`
- `src/interface/python/py_icts/py_register_icts.h`
- `src/interface/tcl/tcl_icts/tcl_register_cts.h`

## Avoid
- Duplicating tree-building, config, or wrapper logic in a new file.
- Mixing experimental debug output into production code paths.
- Adding CTS files without wiring them into the nearest build script.
