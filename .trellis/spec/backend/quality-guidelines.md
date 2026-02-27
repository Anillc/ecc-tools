# Quality Guidelines

> Code quality standards for backend development.

---

## Overview

This project enforces quality through:
- **clang-format** for code formatting (Google style, C++20)
- **clang-tidy** for static analysis and naming conventions
- **Mulan PSL v2** license headers on all files
- **Modern C++20** features and patterns

**Key principle**: Consistency over personal preference. Follow the established patterns.

---

## Code Formatting

### clang-format Configuration

Located at project root: `.clang-format`

```yaml
Language: Cpp
BasedOnStyle: Google
ColumnLimit: 140
Standard: c++20
BreakBeforeBraces: Custom
BraceWrapping:
  AfterClass: true
  AfterFunction: true
  AfterEnum: true
  AfterStruct: true
```

### Key Formatting Rules

1. **Line length**: Maximum 140 characters
2. **Braces**: Opening brace on new line for classes, functions, enums, structs
3. **Indentation**: 2 spaces (no tabs)
4. **Standard**: C++20 features enabled

### Running clang-format

```bash
# Format a single file
clang-format -i src/operation/iCTS/api/CTSAPI.cc

# Format all files in a directory
find src/operation/iCTS -name "*.cc" -o -name "*.hh" | xargs clang-format -i
```

---

## Naming Conventions

### clang-tidy Configuration

Reference: `src/utility/.clang-tidy`

```yaml
CheckOptions:
  - { key: readability-identifier-naming.LocalVariableCase, value: lower_case }
  - { key: readability-identifier-naming.ClassMemberCase, value: lower_case }
  - { key: readability-identifier-naming.ClassMemberPrefix, value: _ }
  - { key: readability-identifier-naming.ClassMethodCase, value: camelBack }
  - { key: readability-identifier-naming.ClassMethodIgnoredRegexp, value: '[gs]et_[a-zA-Z_]+' }
  - { key: readability-identifier-naming.ClassCase, value: CamelCase }
  - { key: readability-identifier-naming.StructCase, value: CamelCase }
  - { key: readability-identifier-naming.GlobalConstantPrefix, value: k }
  - { key: readability-identifier-naming.GlobalConstantCase, value: CamelCase }
  - { key: readability-identifier-naming.EnumConstantCase, value: CamelCase }
  - { key: readability-identifier-naming.EnumConstantPrefix, value: k }
```

### Naming Rules Summary

| Element | Convention | Example |
|---------|------------|---------|
| Classes | PascalCase | `CTSAPI`, `Clock`, `TopologyGen` |
| Structs | PascalCase | `TreeNode`, `Point` |
| Member variables | `_` prefix + snake_case | `_clock_name`, `_source_pin` |
| Local variables | snake_case | `clock_name`, `load_count` |
| Class methods | camelBack | `runCTS()`, `buildTopology()` |
| Getters/Setters | `get_`/`set_` + snake_case | `get_name()`, `set_location()` |
| Boolean methods | `is_` prefix | `is_buffer()`, `is_clock_net()` |
| Enum values | `k` prefix + PascalCase | `kBuffer`, `kInfo`, `kUnknown` |
| Global constants | `k` prefix + PascalCase | `kMaxFanout`, `kDefaultSkew` |
| Namespaces | lowercase | `icts`, `geometry` |
| Files | PascalCase + `.hh`/`.cc` | `CTSAPI.hh`, `TopologyGen.cc` |

### Examples

```cpp
// Class with proper naming
class TopologyGen
{
 public:
  // camelBack method
  Tree buildTopology(const std::vector<Pin*>& loads);

  // Getter/setter (exception to camelBack rule)
  int get_max_fanout() const { return _max_fanout; }
  void set_max_fanout(int fanout) { _max_fanout = fanout; }

  // Boolean query
  bool is_valid() const { return _valid; }

 private:
  // Member variables with underscore prefix
  int _max_fanout = 32;
  bool _valid = false;
  std::string _algorithm_name;
};

// Enum with k-prefix values
enum class InstType
{
  kBuffer,
  kFlipFlop,
  kInverter,
  kUnknown
};

// Local variables in snake_case
void processData()
{
  int load_count = 0;
  std::string clock_name = "clk";
  bool is_valid = true;
}
```

---

## File Structure

### Header Files (.hh)

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
/**
 * @file FileName.hh
 * @author Author Name (email@example.com)
 * @date YYYY-MM-DD
 * @brief Brief description
 */

#pragma once

#include <standard_headers>
#include "project/headers.hh"

namespace icts {

class ClassName
{
 public:
  // Public interface

 private:
  // Private implementation
};

}  // namespace icts
```

### Source Files (.cc)

```cpp
// Copyright header (same as .hh)

#include "path/to/ClassName.hh"

#include <additional_headers>

namespace icts {

// Implementation

}  // namespace icts
```

### Required Elements

1. **Copyright header**: Mulan PSL v2 (lines 1-16)
2. **Doxygen comment**: File description (lines 17-22)
3. **Header guard**: `#pragma once` (not traditional guards)
4. **Namespace**: All code in `icts` namespace
5. **Include order**: Standard library → Project headers

---

## Forbidden Patterns

### 1. Traditional Include Guards

**Wrong**:
```cpp
#ifndef ICTS_CLOCK_HH
#define ICTS_CLOCK_HH
// ...
#endif
```

**Right**:
```cpp
#pragma once
```

### 2. Using Namespace in Headers

**Wrong**:
```cpp
// In .hh file
using namespace std;
using namespace icts;
```

**Right**:
```cpp
// In .hh file - use fully qualified names
std::string name;
icts::Clock* clock;

// In .cc file - OK to use in implementation
using namespace icts;
```

### 3. Raw Pointers for Ownership

**Wrong**:
```cpp
class Tree
{
 private:
  TreeNode* _root;  // Who owns this?
  std::vector<TreeNode*> _nodes;  // Manual delete needed
};
```

**Right**:
```cpp
class Tree
{
 private:
  TreeNode* _root = nullptr;  // Non-owning reference
  std::vector<std::unique_ptr<TreeNode>> _nodes;  // Owns nodes
};
```

### 4. Manual Memory Management

**Wrong**:
```cpp
TreeNode* node = new TreeNode();
// ... use node ...
delete node;
```

**Right**:
```cpp
auto node = std::make_unique<TreeNode>();
// ... use node ...
// Automatic cleanup
```

### 5. C-Style Casts

**Wrong**:
```cpp
double value = (double)int_value;
Base* base = (Base*)derived;
```

**Right**:
```cpp
double value = static_cast<double>(int_value);
Base* base = dynamic_cast<Base*>(derived);
```

### 6. Global LOG_* Macros in Module Code

**Wrong**:
```cpp
LOG_INFO << "CTS: Processing clock";  // Global logger
```

**Right**:
```cpp
CTS_LOG_INFO << "Processing clock";  // Module-specific logger
```

### 7. Exceptions for Error Handling

**Wrong**:
```cpp
if (error) {
  throw std::runtime_error("Error occurred");
}
```

**Right**:
```cpp
CTS_LOG_ERROR_IF(error) << "Error occurred";
if (error) {
  return;  // Early return
}
```

---

## Required Patterns

### 1. Singleton Pattern

For global resources:

```cpp
#define CTSConfigInst (icts::Config::getInst())

class Config
{
 public:
  static Config& getInst()
  {
    static Config instance;
    return instance;
  }

  Config(const Config&) = delete;
  Config(Config&&) = delete;
  Config& operator=(const Config&) = delete;
  Config& operator=(Config&&) = delete;

 private:
  Config() = default;
  ~Config() = default;
};
```

### 2. Rule of Five/Zero

Either define all five or none:

```cpp
// Rule of Zero (preferred)
class Clock
{
 public:
  Clock() = default;
  ~Clock() = default;
  // Compiler-generated copy/move OK
};

// Rule of Five (when needed)
class Tree
{
 public:
  Tree() = default;
  ~Tree() = default;
  Tree(const Tree&) = delete;
  Tree(Tree&&) = default;
  Tree& operator=(const Tree&) = delete;
  Tree& operator=(Tree&&) = default;
};
```

### 3. Const Correctness

```cpp
class Pin
{
 public:
  // Const getter returns const reference
  const std::string& get_name() const { return _name; }

  // Const method doesn't modify state
  Point<int> get_location() const { return _location; }

  // Non-const setter
  void set_name(const std::string& name) { _name = name; }

 private:
  std::string _name;
  Point<int> _location;
};
```

### 4. Modern C++20 Features

**Use ranges algorithms**:
```cpp
std::ranges::copy(loads, std::back_inserter(pins));
std::ranges::for_each(clocks, [](Clock* clock) { /* ... */ });
std::ranges::transform(paths, std::back_inserter(results), [](auto& p) { return p.string(); });
```

**Use structured bindings**:
```cpp
for (const auto& [name, clock] : clock_map) {
  // Use name and clock
}
```

**Use auto for type deduction**:
```cpp
auto* inst = findInstance(name);
auto loads = clock->get_loads();
```

### 5. Lambda Functions

Use lambdas for callbacks and predicates:

```cpp
// Lambda as predicate
auto result = kmeans.run(loads, 2, [](Pin* pin) {
  return pin->get_location();
}, 5);

// Lambda for iteration
std::ranges::for_each(clocks, [&](Clock* clock) {
  processClockTree(clock);
});
```

---

## Testing Requirements

### Test Structure

Tests mirror source structure:

```
test/
├── common/           # Test utilities
├── database/         # Tests for source/database/
│   ├── config/
│   ├── design/
│   └── spatial/
└── module/           # Tests for source/module/
    ├── topology/
    └── routing/
```

### Test Naming

- Test files: `Test<ClassName>.cc`
- Test cases: `TEST(<SuiteName>, <TestName>)`

### Example Test

```cpp
#include "gtest/gtest.h"
#include "database/design/Clock.hh"

namespace icts_test {

TEST(ClockTest, DefaultConstruction)
{
  icts::Clock clock;
  EXPECT_TRUE(clock.get_clock_name().empty());
  EXPECT_EQ(clock.get_source_pin(), nullptr);
  EXPECT_TRUE(clock.get_loads().empty());
}

TEST(ClockTest, SettersAndGetters)
{
  icts::Clock clock;
  clock.set_clock_name("clk");
  EXPECT_EQ(clock.get_clock_name(), "clk");
}

}  // namespace icts_test
```

---

## Code Review Checklist

### Before Submitting

- [ ] Code formatted with clang-format
- [ ] Naming follows clang-tidy conventions
- [ ] Copyright header present (Mulan PSL v2)
- [ ] Doxygen comments for public APIs
- [ ] No compiler warnings
- [ ] Tests pass
- [ ] Module-specific logging used (not global LOG_*)
- [ ] No manual memory management (use smart pointers)
- [ ] Const correctness maintained
- [ ] Error handling follows project patterns

### Reviewer Checklist

- [ ] Code follows naming conventions
- [ ] Proper error handling (logging, not exceptions)
- [ ] No forbidden patterns used
- [ ] Memory management is safe
- [ ] Const correctness maintained
- [ ] Tests cover new functionality
- [ ] Documentation is clear
- [ ] No unnecessary complexity

---

## Common Mistakes

### 1. Inconsistent Naming

**Wrong**:
```cpp
class TopologyGen
{
 private:
  int maxFanout;        // Missing underscore prefix
  std::string m_name;   // Wrong prefix style
  bool isValid;         // Should be _is_valid
};
```

**Right**:
```cpp
class TopologyGen
{
 private:
  int _max_fanout;
  std::string _name;
  bool _is_valid;
};
```

### 2. Missing Copyright Header

**Wrong**:
```cpp
#pragma once
// Missing copyright!

namespace icts {
```

**Right**:
```cpp
// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// ... (full header)
// ***************************************************************************************

#pragma once

namespace icts {
```

### 3. Non-Const Getters

**Wrong**:
```cpp
std::string& get_name() { return _name; }  // Allows modification!
```

**Right**:
```cpp
const std::string& get_name() const { return _name; }
```

### 4. Forgetting to Mark Methods Const

**Wrong**:
```cpp
bool is_buffer() { return _type == InstType::kBuffer; }  // Should be const
```

**Right**:
```cpp
bool is_buffer() const { return _type == InstType::kBuffer; }
```

### 5. Using Wrong File Extensions

**Wrong**:
```cpp
#include "Clock.h"    // Should be .hh
#include "Clock.cpp"  // Should be .cc
```

**Right**:
```cpp
#include "Clock.hh"
// Clock.cc for implementation
```

---

## Best Practices

### 1. Use clang-format Before Commit

```bash
# Format changed files
git diff --name-only | grep -E '\.(cc|hh)$' | xargs clang-format -i
```

### 2. Run clang-tidy Regularly

```bash
# Check a file
clang-tidy src/operation/iCTS/api/CTSAPI.cc -- -std=c++20
```

### 3. Keep Functions Small

- Aim for < 50 lines per function
- Extract complex logic into helper functions
- Use early returns to reduce nesting

### 4. Prefer Composition Over Inheritance

```cpp
// Good: Composition
class TopologyGen
{
 private:
  Clustering _clustering;
  KMeans _kmeans;
};

// Avoid: Deep inheritance hierarchies
```

### 5. Document Public APIs

```cpp
/**
 * @brief Build topology tree from load pins
 * @param loads Load pins to connect
 * @return Tree structure (empty if loads is empty)
 */
Tree buildTopology(const std::vector<Pin*>& loads);
```

### 6. Use Modern C++ Features

- Prefer `auto` for complex types
- Use range-based for loops
- Use structured bindings
- Use `std::optional` for optional values
- Use `std::variant` for type-safe unions
