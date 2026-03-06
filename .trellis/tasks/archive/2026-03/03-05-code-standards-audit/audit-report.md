# Code Standards Audit Report - iCTS Module

**Date**: 2026-03-05
**Files Audited**: 54 (37 .hh + 17 .cc)
**Total Issues**: 87+ violations across 3 categories

---

## Category 1: Copyright Header Issues (54 files affected)

### Issue 1.1: Broken Line Wrap — "Sciences Copyright" (53 files)
**Severity**: High
**Description**: Two copyright lines merged due to incorrect line wrapping.

Current (wrong):
```
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
```

Expected:
```
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
```

### Issue 1.2: Additional Line Wrap Issues (all 54 files)
**Severity**: Medium
Lines 7-8 ("Mulan PSL v2." split) and Lines 11-13 (disclaimer split).

### Issue 1.3: Typo "Pinitute" in Clock.hh (1 file)
**Severity**: High
**File**: `source/database/design/Clock.hh`
`Pinitute` appears twice where `Institute` should be.

**Affected files**: ALL 54 .hh and .cc files.

---

## Category 2: Naming Convention Violations (29 violations)

### Issue 2.1: Missing get_/set_ prefix on simple accessors (16 violations)

| # | File | Current | Should Be |
|---|------|---------|-----------|
| 1 | `source/database/spatial/Point.hh:41` | `x()` getter | `get_x()` |
| 2 | `source/database/spatial/Point.hh:42` | `y()` getter | `get_y()` |
| 3 | `source/database/spatial/Point.hh:44` | `x(val)` setter | `set_x(val)` |
| 4 | `source/database/spatial/Point.hh:50` | `y(val)` setter | `set_y(val)` |
| 5 | `source/database/spatial/Tree.hh:43` | `id()` | `get_id()` |
| 6 | `source/database/spatial/Tree.hh:44` | `parent()` | `get_parent()` |
| 7-8 | `source/database/spatial/Tree.hh:45-46` | `children()` x2 | `get_children()` |
| 9-10 | `source/database/spatial/Tree.hh:47-48` | `position()` x2 | `get_position()` |
| 11-12 | `source/database/spatial/Tree.hh:49-50` | `loads()` x2 | `get_loads()` |
| 13-14 | `source/database/spatial/Tree.hh:122,129` | `node(id)` x2 | `get_node(id)` |
| 15 | `source/database/spatial/Tree.hh:138` | `root()` | `get_root()` |
| 16 | `source/database/spatial/Tree.hh:139` | `size()` | `get_size()` |

### Issue 2.2: Complex logic using get_/set_/is_ prefix (4 violations)

| # | File | Current | Should Be | Why |
|---|------|---------|-----------|-----|
| 17 | `source/database/design/Inst.hh:67` | `get_driver_pin()` | `findDriverPin()` | Contains conditional logic + .front() access |
| 18 | `source/database/design/Inst.hh:70` | `set_driver_pin()` | `insertDriverPin()` | Null checks, duplicate detection, insert at begin |
| 19 | `source/database/io/Wrapper.hh:86-91` | `set_idb()` | `initIdb()` | Derives multiple members from input |
| 20 | `source/database/spatial/Tree.hh:61-69` | `is_leaf()` | `isLeaf()` | For-loop iterating children |

### Issue 2.3: Free functions using snake_case (3 violations)

| # | File | Current | Should Be |
|---|------|---------|-----------|
| 21 | `source/utils/geometry/Geometry.hh:43` | `calc_center()` | `calcCenter()` |
| 22 | `source/utils/geometry/Geometry.hh:60` | `calc_median()` | `calcMedian()` |
| 23 | `source/utils/geometry/Geometry.hh:81` | `project_to_l1_circle()` | `projectToL1Circle()` |

### Issue 2.4: Private method using snake_case (1 violation)

| # | File | Current | Should Be |
|---|------|---------|-----------|
| 24 | `source/utils/logger/Logger.hh:99` | `log_to_console()` | `logToConsole()` |

### Issue 2.5: Getter/Setter name mismatch with member (2 violations)

| # | File | Current | Member | Should Be |
|---|------|---------|--------|-----------|
| 25 | `source/database/config/Config.hh:97` | `get_clock_netlist()` | `_net_list` | `get_net_list()` |
| 26 | `source/database/config/Config.hh:121` | `set_netlist()` | `_net_list` | `set_net_list()` |

### Issue 2.6: Complex setter masquerading as simple (1 violation)

| # | File | Current | Should Be | Why |
|---|------|---------|-----------|-----|
| 27 | `source/module/topology/TopologyGen.hh:46` | `set_config()` | `updateConfig()` | Also reconstructs _clustering |

### Issue 2.7: Wrapper getDbUnit inconsistent style (1 violation)

| # | File | Current | Should Be |
|---|------|---------|-----------|
| 28 | `source/database/io/Wrapper.hh:83` | `getDbUnit()` | `get_db_unit()` (simple delegation) |

---

## Category 3: Forbidden Patterns (1 violation)

| # | File | Line | Issue | Fix |
|---|------|------|-------|-----|
| 1 | `source/module/topology/mcf/MinCostFlow.hh:29` | `using namespace lemon;` | Namespace pollution in header | Qualify all lemon types explicitly |

---

## Summary

| Category | Count | Priority |
|----------|-------|----------|
| Copyright: All files need reformatting | 54 files | P0 |
| Copyright: Clock.hh typo | 1 file | P0 |
| Naming: Missing get_/set_ prefix | 16 | P1 |
| Naming: Wrong prefix on complex methods | 4 | P1 |
| Naming: snake_case free functions | 3 | P1 |
| Naming: Other naming issues | 6 | P2 |
| Forbidden: using namespace in header | 1 | P1 |
| **Total unique violations** | **~85** | |
