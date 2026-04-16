# Cross-Layer Thinking Guide

Use this when a CTS feature crosses more than one of these layers:

`Tcl/Python/GUI -> tool_manager -> CTSAPI -> data_manager/module/utils/report`

## Trace These Boundaries
- Command or binding name: `run_cts`, `cts_report`, `cts_config`.
- Parameters: config path, work dir, output path, optional defaults.
- Runtime state: `flowConfigInst` status stage, runtime, memory.
- Backend entrypoints: `CTSAPIInst.init()`, `runCTS()`, `report()`.
- Build registration: `registerTclCmd`, `py::arg`, and local `CMakeLists.txt`.
- Report visibility: which summary sections must appear in terminal logging, and which are only file-side artifacts.

## Executable CTS Contracts

### `run_cts`
- Canonical path:
  `Tcl/Python wrapper -> iplf::tmInst->autoRunCTS(config, work_dir) -> ToolManager::autoRunCTS(std::string, std::string) -> CtsIO::runCTS(std::string, std::string) -> CTSAPIInst.init(config, work_dir) -> CTSAPIInst.runCTS()`
- Parameter ownership:
  `config` default is resolved in `CtsIO::runCTS()` by `flowConfigInst->get_icts_path()` only when the caller passes an empty string.
  `work_dir` is a wrapper-to-API runtime override and is forwarded unchanged into `CTSAPIInst.init(...)`.
- Verification points:
  `flowConfigInst->set_status_stage("iCTS - Clock Tree Synthesis")` is updated before the CTS run.
  The generated binary used for smoke tests must be `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA`.

### `cts_report`
- Canonical path:
  `Tcl/Python wrapper -> iplf::tmInst->reportCTS(save_dir) -> ToolManager::reportCTS(std::string) -> CtsIO::reportCTS(std::string) -> CTSAPIInst.report(save_dir)`
- Parameter ownership:
  Wrapper-layer `-name` and `-path` compatibility options both forward a single `save_dir` string into `reportCTS(std::string)`.
  `CtsIO::reportCTS()` must preserve the caller-provided string exactly and must not silently rewrite it into a config path.
  `CTSAPIInst.report("")` is the only supported empty-string fallback path; it keeps the legacy API behavior of using the CTS work directory.
- Wrapper rules:
  Interface code must not include `CTSAPI.hh` just to call `CTSAPIInst.report(...)` directly.
  Thin wrappers may perform option validation, but not path reinterpretation or CTS business logic.

### Error And Validation Matrix
- Good:
  The wrapper passes a non-empty `save_dir`; the statistics report is emitted under `save_dir/statistics`.
- Base:
  The wrapper passes `""`; `CTSAPIInst.report("")` uses the CTS work directory and still generates the statistics report.
- Bad:
  The wrapper bypasses `tool_manager` and calls `CTSAPIInst` directly; this is a contract violation even if the behavior appears correct.
- Bad:
  The wrapper rewrites an empty `save_dir` into a config file path; this changes report semantics and must be rejected in review.

### Minimum Smoke Points
- Tcl smoke:
  `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Python/Tcl wrapper audit:
  Search for `CTSAPIInst.report` under `src/interface/` and expect no direct hits for `cts_report` wrappers.
- Build touchpoints:
  Recheck `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`, `src/interface/tcl/tcl_icts/tcl_cts.cpp`, and `src/interface/python/py_icts/py_icts.cpp`.
- Report parity:
  If `cts_report` output changed, verify the summary body appears in both terminal logging and `result/cts/cts.log`.

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
