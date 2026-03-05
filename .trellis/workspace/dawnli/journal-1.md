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


## Session 2: CTS Characterization: iSTA Clock Propagation + Steps-Based Discretization

**Date**: 2026-02-28
**Task**: CTS Characterization: iSTA Clock Propagation + Steps-Based Discretization

### Summary

Refactored CTS characterization module to use iSTA clock propagation and unified unsigned types

### Main Changes


## Major Changes

### 1. CTSAPI Extension (src/operation/iCTS/api/)
- Added characterization circuit management APIs:
  - `createCharClock()` / `destroyCharClock()` - Clock propagation setup
  - `queryCharClockAT()` - Total delay via iSTA clock arrival time
  - `createCharInstance/Net/Pin`, `buildCharRcTree` - Temporary circuit construction
  - `updateCharTiming()`, `setCharInputSlew()`, `queryCharSlew()`

### 2. CharBuilder Implementation (src/operation/iCTS/source/module/characterization/)
- New `CharBuilder` class for pattern enumeration and characterization
- Uses real iDB nets with RC trees (Pi-model)
- Clock-based timing: create clock on source buffer → `updateTiming()` → `getClockAT()` at sink
- Variable wire segment lengths based on buffer positions
- Monotonic buffer enumeration (non-decreasing buffer indices)
- Configurable near-neighbor redundancy removal (default off)

### 3. Data Structure Refactoring (src/operation/iCTS/source/database/characterization/)
- Unified all integer types to `unsigned` (removed uint16_t/uint32_t/uint64_t)
- Steps-based discretization: `*_steps` (count) instead of `*_unit` (step size)
- Naming convention: `*_idx` suffix for discretized indices (e.g., `input_slew_idx`)
- Physical values use `*_um`/`*_ns`/`*_pf` suffixes when ambiguous
- Hash key packing: `(slew << 16) | cap` (32-bit), `(domain << 30) | local_id`

### 4. Config Updates (src/operation/iCTS/source/database/config/)
- Replaced `slew_unit/cap_unit/length_unit` with `slew_steps/cap_steps/length_steps`
- Default: 20 bins for each dimension
- Added `get_char_buf_redundancy_pct()` for buffer pruning threshold

### 5. Test Updates
- Updated `CharacterizationTest.cc` to use new `_idx` getters and `unsigned` types

## Verification
- Build: 248 targets, zero errors
- clang-format: clean on all modified files
- Redundant casts removed from HashJoinEngine.hh and Pruner.hh

## Task Spec Updates
- Section 4: Unit Protocol (um/ns/pF, no DBU conversion in char module)
- Section 7: Steps-based discretization documentation
- Added data type unification guidelines (`unsigned`, `*_idx` naming)


### Git Commits

| Hash | Message |
|------|---------|
| `53241a8e5` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 3: Restore and update iCTS backend spec from codebase

**Date**: 2026-03-05
**Task**: Restore and update iCTS backend spec from codebase

### Summary

(Add summary)

### Main Changes

## Summary
Restored backend spec files from empty templates and updated against current iCTS codebase.

## Changes

| File | Key Updates |
|------|-------------|
| `database-guidelines.md` | Added HTreeTopologyPattern, PatternId factories, updated CharCore fields from uint16_t to unsigned |
| `directory-structure.md` | Added routing sub-modules, characterization internals (CharBuilder, HashJoinEngine), module enable/disable status |
| `error-handling.md` | Verified examples match current CTSAPI.cc |
| `logging-guidelines.md` | Updated Logger API (set_log_file vs init) |
| `quality-guidelines.md` | Deduplicated clang-format section (ref project-constraints.md) |

## Process
- Analyzed git diff to recover original spec content (the `-` lines)
- Verified all content against current source code
- Launched 3 research agents to explore data model, directory structure, logging patterns
- Added new characterization types discovered in codebase

## Files Modified
- `.trellis/spec/backend/index.md`
- `.trellis/spec/backend/database-guidelines.md`
- `.trellis/spec/backend/directory-structure.md`
- `.trellis/spec/backend/error-handling.md`
- `.trellis/spec/backend/logging-guidelines.md`
- `.trellis/spec/backend/quality-guidelines.md`


### Git Commits

| Hash | Message |
|------|---------|
| `4f4a3732e` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 4: Free Function CamelCase Rename & Spec Alignment

**Date**: 2026-03-06
**Task**: Free Function CamelCase Rename & Spec Alignment

### Summary

(Add summary)

### Main Changes

## Summary

Completed GlobalFunctionCase: CamelCase enforcement across the entire iCTS module, and aligned the naming convention tables in both spec files.

## Changes

### Free Function Renames (GlobalFunctionCase: CamelCase)

| File | Old Name | New Name |
|------|----------|----------|
| `Geometry.hh` | `manhattan`, `calcCenter`, `calcMedian`, `projectToL1Circle` | `Manhattan`, `CalcCenter`, `CalcMedian`, `ProjectToL1Circle` |
| `HashJoinEngine.hh` | `pack`, `hashJoinConcat` | `Pack`, `HashJoinConcat` |
| `TopologyGen.cc` | `calcLoadBounds` | `CalcLoadBounds` |
| `Clustering.cc` | `splitByPosition`, `calcCenters` | `SplitByPosition`, `CalcCenters` |
| `CharacterizationTest.cc` | `makeSegmentChar`, `makeHTreeChar` | `MakeSegmentChar`, `MakeHTreeChar` |

### Call Site Updates (11 files total)

- `TopologyGen.cc` — geometry calls + `CalcLoadBounds`
- `Clustering.cc` — geometry calls + `SplitByPosition`, `CalcCenters`
- `KMeans.hh` — `geometry::Manhattan`
- `TopologyGenTest.cc` — `geometry::Manhattan`
- `SegmentTraits.hh` — `detail::Pack` ×2
- `HTreeTraits.hh` — `detail::Pack` ×2
- `SegmentCharTable.hh` — `detail::HashJoinConcat`
- `HTreeTopologyCharTable.hh` — `detail::HashJoinConcat`
- `CharacterizationTest.cc` — `detail::Pack` ×3, `MakeSegmentChar`, `MakeHTreeChar`

### Geometry.hh Type Generalization

- `Manhattan<T>` — template input, `double` return (distance is real-valued)
- `CalcCenter` — auto return via `std::decay_t` + `std::conditional_t` (int→Point<double>, float→Point<float>)
- `CalcMedian` — auto return, preserves getter's Point coordinate type
- `ProjectToL1Circle` — explicitly `Point<int>` (DBU grid-specific)

### Spec Alignment

- Added `Global/Free function | CamelCase` row to both naming tables
- Unified both tables to 5-column format (Element | Case | Prefix | Suffix | Example)
- Unified row order and example sets (union of both)
- Fixed `Wrapper::getDbUnit()` → `Wrapper::get_db_unit()` in project-constraints.md

### Verification

- Grep confirmed zero remaining old function names across entire iCTS module


### Git Commits

| Hash | Message |
|------|---------|
| `6c88474ab` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
