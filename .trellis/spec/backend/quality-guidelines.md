# Quality Guidelines

> Code quality standards for iCTS backend development.

---

## Overview

Code quality is enforced through automated tools (`clang-format`, `clang-tidy`) and naming conventions. This document describes the standards every code change must meet.

For full clang-format/clang-tidy details, see [Project Constraints](../project-constraints.md).

---

## Naming Conventions

Naming conventions are enforced by `readability-identifier-naming` checks in the project's `.clang-tidy` configuration file (`src/utility/.clang-tidy`). Run `python3 ./.trellis/ecc_dev_tools/check.py check --path <path>` to verify compliance.

### Simple vs Complex Accessor Naming

**snake_case** (`get_`/`set_`/`is_`) — only for trivial accessors that directly read/write a private member or do a simple comparison:
```cpp
const std::string& get_name() const { return _name; }
void set_type(InstType type) { _type = type; }
bool is_buffer() const { return _type == InstType::kBuffer; }
```

**camelBack** — for anything involving computation, traversal, external queries, or multi-step logic:
```cpp
double calcDelay() const { /* computation */ }
bool hasViolation() const { /* checks multiple conditions */ }
std::vector<Pin*> collectSinkPins() const { /* traversal */ }
```

**Rule of thumb**: If the body is more than `return _member;` / `_member = value;` / `return _member == kX;`, use camelBack.

### Examples from Codebase

```cpp
// Class: PascalCase
class TopologyGen
{
 public:
  // Method: camelBack
  Tree build(const std::vector<Pin*>& loads);

  // Simple getter/setter: snake_case with get_/set_ prefix
  const std::string& get_name() const { return _name; }
  void set_name(const std::string& name) { _name = name; }

  // Simple boolean query: snake_case with is_ prefix
  bool is_buffer() const { return _type == InstType::kBuffer; }

  // Complex accessor: camelBack (involves computation / multi-step logic)
  double calcDelay() const { /* ... */ }
  bool hasViolation() const { /* ... */ }

 private:
  // Member variable: underscore prefix + lower_case
  std::string _name;
  InstType _type = InstType::kUnknown;
};

// Enum: scoped, k-prefix values
enum class InstType
{
  kBuffer,
  kFlipFlop,
  kInverter,
  kClockGate,
  kMux,
  kUnknown
};

// Namespace: lowercase
namespace icts {
// ...
}  // namespace icts
```

---

## Forbidden Patterns

| Pattern | Why | Use Instead |
|---------|-----|-------------|
| `#ifndef` / `#define` include guards | Inconsistent with codebase | `#pragma once` |
| `.h` / `.hpp` / `.cpp` file extensions | Project standard is `.hh` / `.cc` | `.hh` / `.cc` only |
| Global `LOG_*` macros in iCTS code | Bypasses iCTS dual-output logging | `CTS_LOG_*` macros |
| `throw` / exceptions | Not used in iCTS | `CTS_LOG_FATAL` / `CTS_LOG_ERROR` + return |
| Plain `enum` (unscoped) | Type-unsafe | `enum class` (scoped enum) |
| `using namespace std;` | Pollutes namespace | Explicit `std::` prefix |
| Hungarian notation (`strName`, `nCount`) | Not project convention | Follow `.clang-tidy` naming rules |
| Abbreviations in class names | Reduces readability | Full words: `TopologyGen` not `TopoGen` |

---

## Include & Dependency Conventions

### Include Minimization

1. **Prefer forward declarations over includes**
   - When a header only uses pointers or references to a type, use a forward declaration instead of `#include`
   - Only include when the complete type is needed (sizeof, member access, inheritance)
   ```cpp
   // Good: BoundSkewTree.hh only uses Area by pointer
   namespace icts::bst {
   class Area;  // forward declaration
   }

   // Bad: including a full header just for a pointer type
   #include "Components.hh"  // pulls in entire header just for Area*
   ```

2. **Keep includes in `.cc` when possible**
   - Headers should only include the minimum dependencies needed for the interface
   - Move implementation dependencies to the `.cc` file
   ```cpp
   // BSTRouter.hh -- only what the interface needs
   #pragma once
   #include <vector>
   namespace icts { class CtsNet; }  // forward declare

   // BSTRouter.cc -- implementation deps go here
   #include "BSTRouter.hh"
   #include "BoundSkewTree.hh"
   #include "GeomCalc.hh"
   #include "Components.hh"
   ```

3. **Headers must be self-contained** -- each `.hh` must compile on its own without errors (`ecc_dev_tools` header check verifies this automatically)

### Include Order

Arrange includes in the following groups, separated by blank lines, alphabetically sorted within each group:

```cpp
// 1. Corresponding header (only in .cc files)
#include "ThisFile.hh"

// 2. Project headers (quoted)
#include "Components.hh"
#include "GeomCalc.hh"

// 3. Third-party headers
#include <Eigen/Core>
#include "json/json.hpp"

// 4. C++ standard library (angle brackets)
#include <algorithm>
#include <memory>
#include <vector>

// 5. C standard library (angle brackets)
#include <cassert>
#include <cmath>
```

### CMake Dependency Rules

1. **Express dependencies through `target_link_libraries`, not duplicated include paths**
   ```cmake
   # Good: link target, automatically inherit its PUBLIC/INTERFACE include paths
   target_link_libraries(icts_source_module_routing PRIVATE icts_source_database_spatial)

   # Bad: manually duplicating another module's include paths
   target_include_directories(icts_source_module_routing PRIVATE ${ICTS_DATABASE_SPATIAL})
   ```

2. **Visibility keywords**
   | Keyword | Meaning | When to use |
   |---------|---------|-------------|
   | `PRIVATE` | Only for own compilation | Implementation deps (libraries included in .cc) |
   | `PUBLIC` | Own compilation + downstream | Interface deps (libraries included in .hh) |
   | `INTERFACE` | Downstream only | All deps of header-only libraries |

   **Default to `PRIVATE`.** Only use `PUBLIC` when the header exposes types from the dependency.

3. **Use the nearest logical-level `set` variable for paths**
   ```cmake
   # Each layer's CMakeLists.txt defines path variables for its own subdirectories
   # source/CMakeLists.txt
   set(ICTS_DATABASE ${ICTS_SOURCE}/database)
   set(ICTS_MODULE ${ICTS_SOURCE}/module)
   set(ICTS_UTILS ${ICTS_SOURCE}/utils)

   # source/database/CMakeLists.txt
   set(ICTS_DATABASE_SPATIAL ${ICTS_DATABASE}/spatial)
   set(ICTS_DATABASE_CONFIG ${ICTS_DATABASE}/config)

   # Reference the nearest-level variable; do not concatenate across layers
   # Good
   target_include_directories(... PUBLIC ${ICTS_DATABASE_SPATIAL})
   # Bad: concatenating paths across layers
   target_include_directories(... PUBLIC ${ICTS_SOURCE}/database/spatial)
   ```

4. **Check for existing INTERFACE targets before adding new dependencies** -- do not redefine an existing INTERFACE library; just link to it

---

## Required Patterns

| Pattern | Where | Example |
|---------|-------|---------|
| Copyright header | Every new `.cc` / `.hh` file | See `project-constraints.md` |
| `#pragma once` | Every `.hh` file | First line after copyright |
| Doxygen file comment | Every new file | `@file`, `@author`, `@date`, `@brief` |
| Namespace closing comment | All namespace blocks | `}  // namespace icts` |
| Scoped enums | All enums | `enum class InstType { ... }` |
| Singleton macro | Singleton classes | `#define CONFIG_INST (icts::Config::getInst())` |

---

## Testing Requirements

- Test files go in `test/` mirroring the `source/` directory structure
- Test file naming: `{ClassName}Test.cc` (e.g., `TopologyGenTest.cc`)
- Tests link against `icts_source`, `icts_api`, and `icts_test_external_libs`
- All test sources are listed explicitly in `test/CMakeLists.txt`

---

## Code Review Checklist

Before submitting code for review:

- [ ] `clang-format` applied to all modified files
- [ ] Naming follows clang-tidy conventions
- [ ] No forbidden patterns introduced
- [ ] Copyright header present on new files
- [ ] `CTS_LOG_*` macros used (not global `LOG_*`)
- [ ] Include minimization: prefer forward declarations; keep implementation deps in `.cc`
- [ ] Include order: own header -> project -> third-party -> standard library
- [ ] CMake deps via `target_link_libraries` with correct visibility (default PRIVATE)
- [ ] CMake updated if new files/modules added
- [ ] Compiles successfully after CMake changes
- [ ] Domain terminology matches established norms (see `project-constraints.md`)
- [ ] IWYU analysis clean (no unnecessary includes in modified headers)

---

## Quality Workflow

The repository provides a local C++ quality checker at `.trellis/ecc_dev_tools/check.py`.

### Environment setup
```bash
# Verify all tools are available
python3 ./.trellis/ecc_dev_tools/check.py doctor
```

### Default check (format + tidy + headers + cmake + iwyu)
```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

### Common presets
```bash
# Code quality only (format + tidy)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset quality

# Structure only (headers + cmake)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset structure

# Tidy only (clang-tidy checks without format/headers/cmake)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset tidy-only

# IWYU include analysis only
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset iwyu-only

# Deep analysis (all tidy categories + analyzer + compiler frontend)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --tidy-mode deep --pass-plan complete
```

### Output formats
```bash
# Machine-readable JSON (for CI integration)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --output-format json

# Editor-friendly (file:line: severity: message)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --output-format compiler

# Quiet mode (results only, no environment/plan details)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --quiet
```

### Useful flags
```bash
# Auto-fix formatting issues
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --fix

# Return exit code 0 even with findings (useful in scripts)
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --no-fail-on-findings

# Show findings suppressed by the whitelist
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --show-suppressed
```

### Suppression whitelist

Known false positives and intentionally ignored findings are managed in `.trellis/ecc_dev_tools/suppressions.jsonl` (JSONL format, one rule per line). Each rule specifies match criteria:

```json
{"path": "file.cc", "category": "iwyu", "subtype": "missing-include", "pattern": "some_header.h", "reason": "explanation", "added": "2026-03-23"}
```

- `path` (string|null): file path substring to match, or null for any file
- `category` (string|null): finding category to match, or null for any
- `subtype` (string|null): finding subtype to match, or null for any
- `pattern` (string|null): message substring to match, or null for any
- `reason` (string): human-readable explanation

Suppressed findings are excluded from the exit code. Use `--show-suppressed` to review them.

### Tool dependencies

| Tool | Required | Purpose |
|------|----------|---------|
| cmake | Yes | Build metadata refresh |
| ninja | Yes | Build backend |
| clang-format | Yes | Code formatting check |
| clang-tidy | Yes | Static analysis (tidy, naming, deep analysis) |
| clang++ | Yes | Clang frontend syntax-only pass |
| g++ | Yes | Native compiler fallback, header self-containedness |
| clang-scan-deps | Optional | Accurate include dependency scanning |
| include-what-you-use | Optional | Include minimization analysis |

Naming conventions are enforced by `readability-identifier-naming` checks in the project `.clang-tidy` configuration. The checker reads this configuration automatically.
