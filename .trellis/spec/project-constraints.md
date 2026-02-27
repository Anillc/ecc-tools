# Project Constraints

> Critical constraints and policies for this project.

---

## Overview

This document defines **mandatory constraints** that apply to all development work in this project. These are non-negotiable rules that must be followed.

**Target Module**: Primary focus on `src/operation/iCTS/` (Clock Tree Synthesis)

---

## Git Policy

### Git Read-Only for AI

**CRITICAL**: AI agents must NOT execute git commit or push commands.

**Forbidden Commands**:
```bash
git commit      # ❌ FORBIDDEN
git push        # ❌ FORBIDDEN
git commit -am  # ❌ FORBIDDEN
git push -f     # ❌ FORBIDDEN
```

**Allowed Commands** (read-only):
```bash
git status      # ✅ OK
git log         # ✅ OK
git diff        # ✅ OK
git branch      # ✅ OK (list only)
git show        # ✅ OK
```

**Rationale**: All commits must be reviewed and executed by human developers to maintain code quality and accountability.

**AI Workflow**:
1. AI writes code
2. AI reports changes to user
3. Human reviews changes
4. Human commits via git

---

## File Naming Conventions

### C++ File Extensions

**MANDATORY**: All C++ files must use `.cc` and `.hh` extensions.

**Correct**:
```
CTSAPI.hh       ✅
CTSAPI.cc       ✅
TopologyGen.hh  ✅
TopologyGen.cc  ✅
```

**Forbidden**:
```
CTSAPI.h        ❌ Use .hh instead
CTSAPI.cpp      ❌ Use .cc instead
CTSAPI.hpp      ❌ Use .hh instead
CTSAPI.cxx      ❌ Use .cc instead
```

**File Naming Style**: PascalCase (e.g., `TopologyGen.hh`, `Clustering.cc`)

**Rationale**: Consistency across the codebase. The `.hh`/`.cc` convention distinguishes this project's files from third-party libraries.

---

## Copyright Requirements

### Mulan PSL v2 License Header

**MANDATORY**: Every source file (`.cc`, `.hh`) must start with the Mulan PSL v2 copyright header.

**Required Header** (lines 1-16):

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

**Followed by Doxygen comment**:

```cpp
/**
 * @file FileName.hh
 * @author Author Name (email@example.com)
 * @date YYYY-MM-DD
 * @brief Brief description of the file
 */
```

**Verification**:
```bash
# Check if file has copyright header
head -20 src/operation/iCTS/api/CTSAPI.hh | grep "Mulan PSL v2"
```

---

## Header Guard Requirements

### Use #pragma once

**MANDATORY**: All header files must use `#pragma once` instead of traditional include guards.

**Correct**:
```cpp
// CTSAPI.hh
#pragma once

namespace icts {
  // ...
}
```

**Forbidden**:
```cpp
// ❌ Traditional include guards
#ifndef ICTS_CTSAPI_HH
#define ICTS_CTSAPI_HH

namespace icts {
  // ...
}

#endif  // ICTS_CTSAPI_HH
```

**Rationale**: `#pragma once` is simpler, less error-prone, and supported by all modern compilers.

---

## Terminology Standards

### EDA Domain Terminology

**Context**: This is an EDA (Electronic Design Automation) tool for Clock Tree Synthesis (CTS).

**Standard Terms** (use these consistently):

| Concept | Correct Term | Avoid |
|---------|--------------|-------|
| 时钟树综合 | Clock Tree Synthesis (CTS) | Clock tree generation |
| 实例 | Instance (Inst) | Object, Component |
| 网络 | Net | Wire, Connection |
| 引脚 | Pin | Port, Terminal |
| 缓冲器 | Buffer | Repeater |
| 触发器 | Flip-flop | Register, FF |
| 负载 | Load | Sink, Fanout |
| 驱动 | Driver | Source |
| 偏斜 | Skew | Delay difference |
| 拓扑 | Topology | Structure, Tree structure |
| 布局 | Placement | Layout |
| 布线 | Routing | Wiring |

**Class Naming** (follow existing patterns):
- `Clock` - Clock tree data structure
- `Inst` - Instance (not `Instance` to keep it short)
- `Net` - Network connection
- `Pin` - Pin/port
- `Tree` - Topology tree
- `TreeNode` - Tree node

**Method Naming** (use domain terms):
```cpp
// ✅ Correct
void buildTopology();
void insertBuffer();
void routeClockNet();
double calculateSkew();

// ❌ Avoid
void createStructure();
void addRepeater();
void wireUp();
double getDelayDiff();
```

**Comments and Documentation**:
- Use English for all code comments
- Use standard EDA terminology
- Be precise: "buffer insertion" not "adding buffers"

---

## Code Style Enforcement

### clang-format (Mandatory)

**MANDATORY**: All code must be formatted with clang-format before commit.

**Configuration**: `.clang-format` at project root

**Key Rules**:
- Line limit: 140 characters
- Indentation: 2 spaces
- Braces: New line for classes, functions, enums, structs
- Standard: C++20

**Pre-commit Check**:
```bash
# Format all changed files
git diff --name-only | grep -E '\.(cc|hh)$' | xargs clang-format -i

# Verify formatting
git diff --name-only | grep -E '\.(cc|hh)$' | xargs clang-format --dry-run -Werror
```

**AI Workflow**:
1. Write code
2. Run clang-format on all modified files
3. Report formatted code to user

---

## clang-tidy Enforcement

### Static Analysis (Mandatory)

**MANDATORY**: All code must pass clang-tidy checks.

**Configuration**: `src/utility/.clang-tidy`

**Key Checks**:
- Naming conventions (see quality-guidelines.md)
- Modernize checks (use C++20 features)
- Readability checks
- Performance checks

**Running clang-tidy**:
```bash
# Check a single file
clang-tidy src/operation/iCTS/api/CTSAPI.cc -- -std=c++20

# Check all files in module
find src/operation/iCTS -name "*.cc" | xargs -I {} clang-tidy {} -- -std=c++20
```

**Common Issues to Fix**:
- Naming violations (member variables without `_` prefix)
- Use of C-style casts (use `static_cast`, `dynamic_cast`)
- Missing `const` on methods
- Use of raw `new`/`delete` (use smart pointers)

---

## CMake-First Workflow

### Development Process

**MANDATORY**: When creating new files or modules, follow this strict order:

#### Step 1: Update CMakeLists.txt

Before writing any implementation, update the CMake configuration:

```cmake
# Add new source file to CMakeLists.txt
set(SOURCES
  existing_file.cc
  NewFile.cc        # Add this line
)

# Or add new library
add_library(icts_new_module INTERFACE)
target_sources(icts_new_module INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/NewFile.cc
)
```

#### Step 2: Create Header with Declarations

Create the `.hh` file with:
- Copyright header
- Doxygen comment
- `#pragma once`
- Class/function declarations (no implementation)

```cpp
// Copyright header...
/**
 * @file NewFile.hh
 * @author Your Name (email@example.com)
 * @date 2026-02-27
 * @brief Brief description
 */

#pragma once

namespace icts {

class NewClass
{
 public:
  NewClass() = default;
  ~NewClass() = default;

  // Declare methods (no implementation yet)
  void doSomething();
  int calculate();

 private:
  int _member_variable;
};

}  // namespace icts
```

#### Step 3: Create Source with Stub Implementation

Create the `.cc` file with minimal stub implementations:

```cpp
// Copyright header...

#include "path/to/NewFile.hh"

namespace icts {

void NewClass::doSomething()
{
  // TODO: Implement
}

int NewClass::calculate()
{
  // TODO: Implement
  return 0;
}

}  // namespace icts
```

#### Step 4: Compile and Verify

**CRITICAL**: Compile the code before implementing logic:

```bash
cd build
cmake ..
make icts_source_module  # Or relevant target
```

**Verify**:
- No compilation errors
- No linker errors
- All declarations are found

#### Step 5: Implement Logic

Only after successful compilation, implement the actual logic:

```cpp
void NewClass::doSomething()
{
  // Now implement the actual logic
  CTS_LOG_INFO << "Doing something";
  // ...
}
```

#### Step 6: Compile Again

```bash
make icts_source_module
```

**Rationale**: This workflow ensures:
1. CMake configuration is correct before writing code
2. Interface is defined before implementation
3. Compilation issues are caught early
4. No time wasted on implementation that won't compile

---

## Checklist for New Files

When creating a new file, verify:

- [ ] File extension is `.cc` or `.hh` (not `.cpp`, `.h`, `.hpp`)
- [ ] File name is PascalCase (e.g., `TopologyGen.hh`)
- [ ] Copyright header (Mulan PSL v2) is present (lines 1-16)
- [ ] Doxygen comment with @file, @author, @date, @brief
- [ ] Header uses `#pragma once` (not traditional guards)
- [ ] File is added to CMakeLists.txt
- [ ] Code compiles successfully
- [ ] Code passes clang-format
- [ ] Code passes clang-tidy
- [ ] Naming follows conventions (see quality-guidelines.md)
- [ ] Module-specific logging used (CTS_LOG_*, not LOG_*)

---

## Checklist for New Modules

When creating a new module/directory:

- [ ] Create directory structure (api/, source/, test/)
- [ ] Create CMakeLists.txt in each directory
- [ ] Define interface library in parent CMakeLists.txt
- [ ] Add debug option (e.g., `DEBUG_ICTS_SOURCE_NEWMODULE`)
- [ ] Create placeholder header files with declarations
- [ ] Compile to verify CMake configuration
- [ ] Implement logic
- [ ] Add tests in test/ directory
- [ ] Update parent module's CMakeLists.txt to include new module

---

## Enforcement

### Pre-Commit Checks (Human Responsibility)

Before committing, human developers must verify:

```bash
# 1. Format check
git diff --name-only | grep -E '\.(cc|hh)$' | xargs clang-format --dry-run -Werror

# 2. Compilation check
cd build && make

# 3. Copyright check
for file in $(git diff --name-only | grep -E '\.(cc|hh)$'); do
  head -20 "$file" | grep -q "Mulan PSL v2" || echo "Missing copyright: $file"
done

# 4. Extension check
git diff --name-only | grep -E '\.(cpp|hpp|h|cxx)$' && echo "Wrong extension found!"
```

### AI Responsibilities

When AI writes code:

1. **Always use `.cc`/`.hh` extensions**
2. **Always include copyright header**
3. **Always use `#pragma once`**
4. **Always run clang-format** on modified files
5. **Always follow CMake-first workflow**
6. **Never execute git commit/push**
7. **Always use correct terminology**
8. **Always report changes to user for review**

---

## Summary

| Constraint | Requirement | Enforcement |
|------------|-------------|-------------|
| Git Policy | Read-only for AI | Manual check |
| File Extensions | `.cc`/`.hh` only | Pre-commit script |
| Copyright | Mulan PSL v2 header | Pre-commit script |
| Header Guards | `#pragma once` | Code review |
| Terminology | EDA standard terms | Code review |
| Code Style | clang-format | Pre-commit hook |
| Static Analysis | clang-tidy | CI/CD |
| CMake Workflow | Update CMake → Compile → Implement | Process discipline |

---

## References

- Backend Guidelines: `.trellis/spec/backend/index.md`
- Quality Guidelines: `.trellis/spec/backend/quality-guidelines.md`
- clang-format config: `.clang-format`
- clang-tidy config: `src/utility/.clang-tidy`
