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

All C++ source files must use the following extensions:

| Type | Extension | Example |
|------|-----------|---------|
| Header | `.hh` | `TopologyGen.hh` |
| Source | `.cc` | `TopologyGen.cc` |

**Forbidden**: `.h`, `.hpp`, `.cpp`, `.cxx`, `.c`

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

| Element | Case | Prefix | Suffix | Example |
|---------|------|--------|--------|---------|
| Class | CamelCase | — | — | `Clock`, `CTSAPI`, `TopologyGen` |
| Abstract class | CamelCase | — | `Interface` | `RouterInterface` |
| Class method | camelBack | — | — | `runCTS()`, `readData()`, `summaryClockDistribution()` |
| Getter (simple) | snake_case | `get_` | — | `get_name()`, `get_type()`, `get_clock_source()` |
| Setter (simple) | snake_case | `set_` | — | `set_name()`, `set_type()`, `set_location()` |
| Getter (complex) | camelBack | — | — | `calcDelay()`, `estimateCapacitance()`, `fetchNetlist()` |
| Setter (complex) | camelBack | — | — | `updateTiming()`, `applyTransform()` |
| Boolean query (simple) | snake_case | `is_` | — | `is_buffer()`, `is_flipflop()`, `is_clock_gate()` |
| Boolean query (complex) | camelBack | — | — | `hasViolation()`, `canInsertBuffer()` |
| Member variable | lower_case | `_` | — | `_clock_name`, `_max_fanout`, `_loads` |
| Local variable | lower_case | — | — | `clock_name`, `inst_type`, `num_sinks` |
| Enum (scoped) | CamelCase | — | — | `enum class InstType`, `enum class PinType` |
| Enum value | CamelCase | `k` | — | `kBuffer`, `kFlipFlop`, `kUnknown` |
| Global/Free function | CamelCase | — | — | `Manhattan()`, `CalcCenter()` |
| Global constant | CamelCase | `k` | — | `kFileName` |
| Global variable | CamelCase | `g` | — | `gFileName` |
| Macro | UPPER_CASE | — | — | `CTS_LOG_INFO`, `CTSAPIInst` |
| Namespace | lower_case | — | — | `icts`, `idb`, `ieda` |

### Getter/Setter Exception

Getters and setters are explicitly allowed in snake_case by the clang-tidy regex:
```
ClassMethodIgnoredRegexp: '[gs]et_[a-zA-Z_]+'
```

### Getter/Setter Naming Boundary

**snake_case** (`get_`/`set_`/`is_`) is reserved for **trivial accessors** — methods that directly read/write a private member (or perform minimal logic like a single comparison).

**camelBack** should be used when the method involves:
- Computation or transformation (e.g., `calcDelay()`, `estimateCapacitance()`)
- External system queries (e.g., `fetchTimingData()`, `queryLiberty()`)
- Multi-step logic or aggregation (e.g., `buildNetlist()`, `hasViolation()`)
- Side effects beyond simple assignment

**Rule of thumb**: If the method body is more than a direct `return _member;` / `_member = value;` / `return _member == kSomething;`, use camelBack.

Examples:
```cpp
// Simple accessor → snake_case
const std::string& get_name() const { return _name; }
void set_type(InstType type) { _type = type; }
bool is_buffer() const { return _type == InstType::kBuffer; }

// Complex logic → camelBack
double calcDelay() const { /* multi-step computation */ }
bool hasViolation() const { /* checks multiple conditions */ }
std::vector<Pin*> collectSinkPins() const { /* traversal logic */ }
```

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
| dbu | Design Base Unit (integer coordinates) | `Wrapper::get_db_unit()` |

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

### CMake Target Naming Convention

Targets follow a hierarchical dot-less underscore pattern:
```
icts_{tier}_{category}_{module}
```

Examples:
- `icts_api` — API tier
- `icts_source_database_config` — source tier, database category, config module
- `icts_source_module_topology` — source tier, module category, topology sub-module
- `icts_source_utils_logger` — source tier, utils category, logger sub-module

### Adding a New Module

For a real library (has `.cc` files):
```cmake
add_library(icts_source_module_newmod ${PATH}/NewMod.cc)
target_include_directories(icts_source_module_newmod PUBLIC ${PATH})
target_link_libraries(icts_source_module_newmod PRIVATE <dependencies>)
```

For header-only (INTERFACE library):
```cmake
add_library(icts_source_database_newdata INTERFACE)
target_include_directories(icts_source_database_newdata INTERFACE ${PATH})
target_link_libraries(icts_source_database_newdata INTERFACE <dependencies>)
```

Then link the new target in the parent CMakeLists.txt aggregator.

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
