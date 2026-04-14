# Bootstrap Guidelines

## Goal
Create concise, project-wide development guidelines for this repo, with emphasis on CTS-related development patterns.

## Requirements
- Fill backend spec files with real conventions extracted from `src/operation/iCTS/`.
- Fill frontend spec files as interface-layer rules for Tcl, Python, and GUI wrappers.
- Keep every generated spec file under 50 lines.
- Include concrete example paths and explicit anti-patterns.

## Acceptance Criteria
- [x] `backend/index.md` and `frontend/index.md` contain short checklists.
- [x] All backend guideline files are CTS-oriented and reference real code.
- [x] All frontend guideline files reflect the repo's actual interface layer, not generic web patterns.
- [x] Guides under `.trellis/spec/guides/` are CTS-oriented and remain concise.
- [x] Each generated spec file stays under 50 lines.

## Code Patterns Used
- CTS lifecycle and logging: `src/operation/iCTS/api/CTSAPI.cc`
- Solver structure and guard style: `src/operation/iCTS/source/solver/Solver.cc`
- DB wrapper and config parsing: `src/operation/iCTS/source/data_manager/io/CtsDBWrapper.cc`
- Tcl/Python/GUI wrapper patterns: `src/interface/tcl/tcl_icts/tcl_cts.cpp`, `src/interface/python/py_icts/py_icts.cpp`, `src/interface/gui/src/mainwindow_cts.cpp`

## Notes
- This task updates spec documents only; no runtime code or build targets were changed.
