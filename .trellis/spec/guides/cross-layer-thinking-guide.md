# Cross-Layer Thinking Guide

Use this when a CTS feature crosses more than one of these layers:

`Tcl/Python/GUI -> tool_manager -> CTSAPI -> config/db/solver/report`

## Trace These Boundaries
- Command or binding name: `run_cts`, `cts_report`, `cts_config`.
- Parameters: config path, work dir, output path, optional defaults.
- Runtime state: `flowConfigInst` status stage, runtime, memory.
- Backend entrypoints: `CTSAPIInst.init()`, `runCTS()`, `report()`.
- Build registration: `registerTclCmd`, `py::arg`, and local `CMakeLists.txt`.

## Questions Before Editing
- Which layer owns validation?
- Which layer sets defaults?
- Which layer writes logs or reports?
- Will Tcl, Python, and GUI behavior still match after this change?
- Does the result need both DB updates and report output updates?

## CTS Failure Patterns
- Public name changed in one wrapper but not the others.
- Config field added in JSON parser but not consumed consistently.
- `tool_manager` stage or runtime reporting drifted from the real CTS flow.
- A new file was added but not registered or built.

## Done Checklist
- Search the full call path.
- Update all exposed wrappers.
- Recheck config, report, and build touchpoints.
- Smoke-test at least one interface entrypoint.
