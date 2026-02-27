# Journal - dawnli (Part 1)

> AI development session journal
> Started: 2026-02-27

---



## Session 1: Bootstrap Guidelines + Code Compliance Audit & Fix

**Date**: 2026-02-27
**Task**: Bootstrap Guidelines + Code Compliance Audit & Fix

### Summary

Created project-constraints and backend spec files, audited iCTS codebase against specs, fixed all violations

### Main Changes

## What Was Done

### 1. Bootstrap Guidelines (project-constraints + backend specs)

Created and filled all `.trellis/spec/` guideline files with real iCTS codebase patterns:

| File | Content |
|------|---------|
| `project-constraints.md` | 9 mandatory constraints: git read-only, .cc/.hh extensions, copyright, #pragma once, clang-format, clang-tidy naming, terminology, CMake-first, CTS_LOG_* |
| `backend/index.md` | Quick reference + filled status |
| `backend/directory-structure.md` | Three-tier architecture, CMake target naming, module organization |
| `backend/quality-guidelines.md` | Naming convention table, clang-format/clang-tidy rules, forbidden patterns |
| `backend/logging-guidelines.md` | CTS_LOG_* macros, 4 log levels, usage examples |
| `backend/error-handling.md` | 5 error patterns (Fatal/Error/Warning/Conditional/Silent) |
| `backend/database-guidelines.md` | Data model hierarchy, Meyers Singleton, memory management |

### 2. Code Compliance Audit

Ran 5 parallel checks against the established specs. Results:

| Check | Violations |
|-------|-----------|
| File extensions | 0 |
| Copyright header | 13 (stub files + test/main.cc) |
| #pragma once | 6 (stub .hh files) |
| Logging (CTS_LOG_*) | 0 |
| Forbidden patterns | 0 |
| Terminology | 11 (singleton naming, Port→Pin, cell_name→cell_master) |

### 3. Compliance Fixes (26 files, +450/-41 lines)

- **12 stub files**: Added copyright + #pragma once + placeholder classes (Router, FLUTE, SALT, BoundSkewTree, CBS, FlowManager)
- **test/main.cc**: Added copyright header
- **Config.hh, Design.hh, Wrapper.hh**: `static X instance` → `static X inst`
- **CTSAPI.hh/cc**: `queryCellOutPortCapLimit` → `queryCellOutPinCapLimit`, `queryCellInPortSlewLimit` → `queryCellInPinSlewLimit`, `cell_name` → `cell_master`
- **Characterization module (7 files)**: `build_key`→`buildKey`, `probe_key`→`probeKey`, `group_key`→`groupKey`, `max_per_group`→`maxPerGroup`, `HashJoinConcat`→`hashJoinConcat`

**Updated Files**:
- `.trellis/spec/project-constraints.md` (NEW)
- `.trellis/spec/backend/*.md` (6 files updated)
- `src/operation/iCTS/api/CTSAPI.hh`, `CTSAPI.cc`
- `src/operation/iCTS/source/database/config/Config.hh`
- `src/operation/iCTS/source/database/design/Design.hh`
- `src/operation/iCTS/source/database/io/Wrapper.hh`
- `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh`
- `src/operation/iCTS/source/module/characterization/*.hh` (7 files)
- `src/operation/iCTS/source/module/routing/**/*.{hh,cc}` (10 files)
- `src/operation/iCTS/source/flow/FlowManager.{hh,cc}`
- `src/operation/iCTS/test/main.cc`


### Git Commits

| Hash | Message |
|------|---------|
| `d230616bb` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
