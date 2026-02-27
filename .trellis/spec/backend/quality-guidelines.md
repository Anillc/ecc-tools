# Quality Guidelines

> Code quality standards for iCTS backend development.

---

## Overview

Code quality is enforced through automated tools (`clang-format`, `clang-tidy`) and naming conventions. This document describes the standards every code change must meet.

---

## Code Formatting (clang-format)

The project uses a Google-based `.clang-format` at the project root.

### Key Rules

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

### Formatting Command

```bash
clang-format -i <file>
```

Always format before submitting code.

---

## Static Analysis (clang-tidy)

Reference config: `src/utility/.clang-tidy`

clang-tidy enforces identifier naming conventions automatically. The rules below are extracted from the `.clang-tidy` config.

---

## Naming Conventions

### Complete Naming Table

| Element | Case | Prefix | Suffix | Example |
|---------|------|--------|--------|---------|
| Class | CamelCase | — | — | `Clock`, `CTSAPI`, `TopologyGen` |
| Abstract class | CamelCase | — | `Interface` | `RouterInterface` |
| Class method | camelBack | — | — | `runCTS()`, `readData()`, `summaryClockDistribution()` |
| Getter | snake_case | `get_` | — | `get_name()`, `get_type()`, `get_clock_source()` |
| Setter | snake_case | `set_` | — | `set_name()`, `set_type()`, `set_location()` |
| Boolean query | snake_case | `is_` | — | `is_buffer()`, `is_flipflop()`, `is_clock_gate()` |
| Member variable | lower_case | `_` | — | `_clock_name`, `_max_fanout`, `_loads` |
| Local variable | lower_case | — | — | `clock_name`, `inst_type`, `num_sinks` |
| Enum (scoped) | CamelCase | — | — | `enum class InstType`, `enum class PinType` |
| Enum value | CamelCase | `k` | — | `kBuffer`, `kFlipFlop`, `kUnknown` |
| Global constant | CamelCase | `k` | — | `kFileName` |
| Global variable | CamelCase | `g` | — | `gFileName` |
| Macro | UPPER_CASE | — | — | `CTS_LOG_INFO`, `CTSAPIInst` |
| Namespace | lower_case | — | — | `icts`, `idb`, `ieda` |

### Getter/Setter clang-tidy Exception

Getters and setters use snake_case (not camelBack) and are explicitly allowed by:
```
ClassMethodIgnoredRegexp: '[gs]et_[a-zA-Z_]+'
```

### Examples from Codebase

```cpp
// Class: PascalCase
class TopologyGen
{
 public:
  // Method: camelBack
  Tree build(const std::vector<Pin*>& loads);

  // Getter/Setter: snake_case with get_/set_ prefix
  const std::string& get_name() const { return _name; }
  void set_name(const std::string& name) { _name = name; }

  // Boolean query: snake_case with is_ prefix
  bool is_buffer() const { return _type == InstType::kBuffer; }

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
| Hungarian notation (`strName`, `nCount`) | Not project convention | Use naming table above |
| Abbreviations in class names | Reduces readability | Full words: `TopologyGen` not `TopoGen` |

---

## Required Patterns

| Pattern | Where | Example |
|---------|-------|---------|
| Copyright header | Every new `.cc` / `.hh` file | See `project-constraints.md` |
| `#pragma once` | Every `.hh` file | First line after copyright |
| Doxygen file comment | Every new file | `@file`, `@author`, `@date`, `@brief` |
| Namespace closing comment | All namespace blocks | `}  // namespace icts` |
| Scoped enums | All enums | `enum class InstType { ... }` |
| Singleton macro | Singleton classes | `#define CTSConfigInst (icts::Config::getInst())` |

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
- [ ] Naming follows the conventions table above
- [ ] No forbidden patterns introduced
- [ ] Copyright header present on new files
- [ ] `CTS_LOG_*` macros used (not global `LOG_*`)
- [ ] CMake updated if new files/modules added
- [ ] Compiles successfully after CMake changes
- [ ] Domain terminology matches established norms (see `project-constraints.md`)
