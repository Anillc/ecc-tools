# iCTS Code Compliance Report

> Generated: 2026-02-27 | Branch: `cts_refactor`
> Scope: `src/operation/iCTS/` (51 files: 36 `.hh` + 16 `.cc` - 1 `main.cc`)

---

## Summary

| Check Item | Status | Violations |
|------------|--------|------------|
| File extensions (.cc/.hh) | PASS | 0 |
| File naming (PascalCase) | WARN | 1 |
| Copyright header | FAIL | 13 |
| Header guard (#pragma once) | FAIL | 6 |
| Logging (CTS_LOG_*) | PASS | 0 |
| Forbidden patterns | PASS | 0 |
| Terminology norms | WARN | 11 (across 4 categories) |

**Total violations: 31**

---

## 1. File Extensions

**PASS** — No violations. All C++ files use `.cc`/`.hh` exclusively.

---

## 2. File Naming (PascalCase)

**1 violation:**

| File | Issue |
|------|-------|
| `test/main.cc` | Lowercase `main`, should be `Main.cc` |

> Note: `main.cc` is a common convention for test entry points. Consider whether to exempt test entry files.

---

## 3. Copyright Header

**13 violations** — 7 `.cc` files + 6 `.hh` files missing copyright block.

All are **stub/placeholder files** (1-line content) in routing and flow modules:

| File | Status |
|------|--------|
| `source/module/routing/Router.cc` | Stub — no copyright |
| `source/module/routing/Router.hh` | Stub — no copyright |
| `source/module/routing/flute/FLUTE.cc` | Stub — no copyright |
| `source/module/routing/flute/FLUTE.hh` | Stub — no copyright |
| `source/module/routing/salt/SALT.cc` | Stub — no copyright |
| `source/module/routing/salt/SALT.hh` | Stub — no copyright |
| `source/module/routing/bound_skew_tree/BoundSkewTree.cc` | Stub — no copyright |
| `source/module/routing/bound_skew_tree/BoundSkewTree.hh` | Stub — no copyright |
| `source/module/routing/concurrent_bst_salt/CBS.cc` | Stub — no copyright |
| `source/module/routing/concurrent_bst_salt/CBS.hh` | Stub — no copyright |
| `source/flow/FlowManager.cc` | Stub — no copyright |
| `source/flow/FlowManager.hh` | Stub — no copyright |
| `test/main.cc` | GTest entry — no copyright |

---

## 4. Header Guard (#pragma once)

**6 violations** — Same stub `.hh` files as above:

| File | Status |
|------|--------|
| `source/module/routing/Router.hh` | Missing `#pragma once` |
| `source/module/routing/flute/FLUTE.hh` | Missing `#pragma once` |
| `source/module/routing/salt/SALT.hh` | Missing `#pragma once` |
| `source/module/routing/bound_skew_tree/BoundSkewTree.hh` | Missing `#pragma once` |
| `source/module/routing/concurrent_bst_salt/CBS.hh` | Missing `#pragma once` |
| `source/flow/FlowManager.hh` | Missing `#pragma once` |

No files use `#ifndef` guards (good).

---

## 5. Logging (CTS_LOG_*)

**PASS** — No violations.

- All LOG_* usage in non-Logger files correctly uses `CTS_LOG_*` macros
- No `std::cout`, `printf`, or `std::cerr` found

---

## 6. Forbidden Patterns

**PASS** — No violations.

| Pattern | Result |
|---------|--------|
| `throw` | None found |
| `try`/`catch` (outside Config.cc) | None found |
| Unscoped `enum` | None — all use `enum class` |
| `using namespace std` | None found |
| `assert()` | None found |

---

## 7. Terminology Norms

### 7a. `instance` instead of `inst` (3 violations)

Singleton local variable named `instance` instead of `inst`:

| File | Line | Code |
|------|------|------|
| `source/database/config/Config.hh` | 40-41 | `static Config instance;` |
| `source/database/design/Design.hh` | 38-39 | `static Design instance;` |
| `source/database/io/Wrapper.hh` | 46-47 | `static Wrapper instance;` |

> Note: `CTSAPI.hh` already uses `static CTSAPI inst;` correctly.

### 7b. `wire` instead of `net` (3 locations, borderline)

`wire_width` used as a physical routing width parameter:

| File | Lines | Code |
|------|-------|------|
| `source/database/config/Config.hh` | 82, 105, 135 | `_wire_width`, `get_wire_width()`, `set_wire_width()` |
| `api/CTSAPI.hh` | 63-64 | `queryWireResistance(... wire_width)`, `queryWireCapacitance(... wire_width)` |
| `source/database/config/Config.cc` | 166-167 | JSON key `"wire_width"` |

> Note: `wire_width` refers to physical trace width (not a net object). Consider whether this is an acceptable domain term for the physical dimension concept.

### 7c. `Port` instead of `Pin` in iCTS method names (2 violations)

| File | Lines | Code |
|------|-------|------|
| `api/CTSAPI.hh` | 65-66 | `queryCellOutPortCapLimit()`, `queryCellInPortSlewLimit()` |
| `api/CTSAPI.cc` | 328, 360 | Same methods (definitions) |

> Should be: `queryCellOutPinCapLimit()`, `queryCellInPinSlewLimit()`

### 7d. `cell_name` instead of `cell_master` (2 violations)

Parameter `cell_name` used to refer to Liberty cell master name:

| File | Lines | Code |
|------|-------|------|
| `api/CTSAPI.hh` | 65-66 | `const std::string& cell_name` parameter |
| `api/CTSAPI.cc` | 328-386 | Same parameter used throughout both method bodies |

> Should be: `const std::string& cell_master`

---

## Recommended Actions

### Priority 1 (Quick fixes — stub files)

Add copyright header + `#pragma once` to the 12 stub routing/flow files. These are placeholder files that need proper headers.

### Priority 2 (Terminology — API naming)

| Current | Proposed | Files |
|---------|----------|-------|
| `queryCellOutPortCapLimit` | `queryCellOutPinCapLimit` | CTSAPI.hh/cc |
| `queryCellInPortSlewLimit` | `queryCellInPinSlewLimit` | CTSAPI.hh/cc |
| `cell_name` (parameter) | `cell_master` | CTSAPI.hh/cc |

### Priority 3 (Singleton local variable)

Rename `static Config instance` → `static Config inst` in Config.hh, Design.hh, Wrapper.hh to match CTSAPI.hh pattern.

### Priority 4 (Review decision needed)

- `test/main.cc` → `test/Main.cc` rename: decide if test entry point is exempt
- `wire_width` → decide if this is an acceptable physical-dimension term
