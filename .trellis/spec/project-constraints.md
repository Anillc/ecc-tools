# Project Constraints

> **MANDATORY** constraints for all AI agents and developers working on this project.
> Violation of any constraint below is a blocking error.

---

## 1. Git Read-Only

**AI agents must NEVER execute `git commit` or `git push`.**

- Only read-only git commands are allowed: `git status`, `git log`, `git diff`, `git branch`, etc.
- All commits must be performed by human developers after review.
- Do not stage files (`git add`) unless explicitly instructed by the developer.

---

## 2. File Naming and Extensions

**Within iCTS module code** (`src/operation/iCTS/`): Use `.cc` for sources, `.hh` for headers.
**Forbidden in iCTS**: `.h`, `.hpp`, `.cpp`, `.cxx`, `.c`
> Note: External interface files (`src/interface/`, `src/platform/`) follow upstream conventions and may use `.cpp`.

| Type | Extension | Example |
|------|-----------|---------|
| Header | `.hh` | `TopologyGen.hh` |
| Source | `.cc` | `TopologyGen.cc` |

File names use **PascalCase**:
- `Clock.hh`, `CTSAPI.cc`, `BoundSkewTree.hh`
- Acronyms remain all-caps: `CTSAPI`, `FLUTE`, `SALT`, `CBS`

---

## 3. Copyright Header

Every new `.cc` and `.hh` file **must** begin with the following copyright block:

```cpp
// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
```

Immediately after the copyright block, add a Doxygen file comment:

```cpp
/**
 * @file FileName.hh
 * @author Your Name (email@example.com)
 * @date YYYY-MM-DD
 * @brief One-line description of what this file contains
 */
```

---

## 4. Header Guard

All header files must use `#pragma once` (not `#ifndef` guards):

```cpp
#pragma once
```

---

## 5. Code Style (clang-format)

The project uses a Google-based `.clang-format` at the project root. Key rules:

| Setting | Value |
|---------|-------|
| `BasedOnStyle` | Google |
| `ColumnLimit` | 140 |
| `Standard` | c++20 |
| `BreakBeforeBraces` | Custom (new line for class/struct/enum/function) |
| `AllowShortIfStatementsOnASingleLine` | false |
| `AllowShortLoopsOnASingleLine` | false |
| `AllowShortFunctionsOnASingleLine` | InlineOnly |
| `DerivePointerAlignment` | false |
| `BreakBeforeBinaryOperators` | All |

**Before submitting code**, always format with:
```bash
clang-format -i <file>
```

---

## 6. Code Inspection (clang-tidy)

The project uses clang-tidy for static analysis. Reference config at `src/utility/.clang-tidy`.

### Naming Convention Rules (enforced by clang-tidy)

Naming conventions are enforced by `readability-identifier-naming` checks in the project's `.clang-tidy` configuration file (`src/utility/.clang-tidy`). Run `python3 ./.trellis/ecc_dev_tools/check.py check --path <path>` to verify compliance.

### Getter/Setter Naming Boundary

**snake_case** (`get_`/`set_`/`is_`) is reserved for **trivial accessors** -- methods that directly read/write a private member (or perform minimal logic like a single comparison).

**camelBack** should be used when the method involves computation, external queries, multi-step logic, or side effects beyond simple assignment.

**Rule of thumb**: If the method body is more than a direct `return _member;` / `_member = value;` / `return _member == kSomething;`, use camelBack.

---

## 7. Terminology Norms

When writing code for iCTS (Clock Tree Synthesis), use the established domain terminology:

### Instance Types

| Term | Meaning | Enum |
|------|---------|------|
| buffer | Buffer cell inserted in clock tree | `InstType::kBuffer` |
| flipflop / flip-flop | Sequential sink element | `InstType::kFlipFlop` |
| inverter | Inverted-polarity buffer | `InstType::kInverter` |
| clock gate (ICG) | Integrated clock gating cell | `InstType::kClockGate` |
| mux | Clock multiplexer | `InstType::kMux` |

### Clock Tree Terms

| Term | Meaning | Code Reference |
|------|---------|---------------|
| clock_name | SDC clock definition name | `Clock::_clock_name` |
| clock_net_name | Physical net name driven by clock | `Clock::_clock_net_name` |
| clock_source | Driver pin of the clock net | `Clock::_clock_source` |
| loads / sinks | Endpoint pins (flip-flop clock pins) | `Clock::_loads`, `Net::_loads` |
| driver | Single output pin driving a net | `Net::_driver` |
| inserted_insts | Buffers/inverters added by CTS | `Clock::_inserted_insts` |
| inserted_nets | New nets created by CTS | `Clock::_inserted_nets` |
| cell_master | Liberty cell type name | `Inst::_cell_master` |
| dbu | Design Base Unit (integer coordinates) | `Wrapper::queryDbUnit()` |

### Electrical Terms

| Term | Meaning |
|------|---------|
| skew_bound | Maximum allowed clock skew |
| max_buf_tran | Max transition on buffer output |
| max_sink_tran | Max transition at sink input |
| max_cap | Max capacitance limit |
| max_fanout | Max number of loads per driver |
| slew | Signal transition time |
| driven_cap | Capacitance seen by upstream driver |
| load_cap | Capacitance presented downstream |

### Naming Consistency Rules

- Use `inst` not `instance` in variable names (e.g., `cts_inst`, `idb_inst`)
- Use `net` not `wire` for net objects
- Use `pin` not `port` for pin objects (except top-level IO: `_b_io = true`)
- Use `cell_master` not `cell_type` or `cell_name` for Liberty cell references
- Prefix CTS-created objects with `cts_` when disambiguating from iDB objects

---

## 8. CMake-First Workflow

**When creating new files or modules, always follow this order:**

1. **Modify/create CMakeLists.txt** according to existing patterns
2. **Create placeholder header** with copyright + empty class declaration
3. **Compile to verify** CMake configuration is correct
4. **Then implement** the actual logic

See [Directory Structure](backend/directory-structure.md) for CMake target naming conventions and module setup patterns.

---

## 9. Logging

Use **iCTS-specific** `CTS_LOG_*` macros, not global `LOG_*` macros.

```cpp
CTS_LOG_INFO << "message";          // Informational
CTS_LOG_WARNING << "message";       // Non-fatal issue
CTS_LOG_ERROR << "message";         // Error (continues execution)
CTS_LOG_FATAL << "message";         // Fatal (terminates process)
```

See `backend/logging-guidelines.md` for full details.

---

## Summary Checklist

Before submitting any code change, verify:

- [ ] No `git commit` or `git push` executed by AI
- [ ] Files use `.cc`/`.hh` extensions with PascalCase names
- [ ] Copyright header present on all new files
- [ ] `#pragma once` used in all headers
- [ ] Code formatted with `clang-format`
- [ ] Naming follows clang-tidy conventions
- [ ] Domain terminology matches established norms
- [ ] CMake updated and compiles before implementation
- [ ] `CTS_LOG_*` macros used (not global `LOG_*`)
