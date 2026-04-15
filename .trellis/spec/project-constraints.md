# Project Constraints

Repository-wide hard constraints for work under `src/operation/iCTS/`.

## Scope

- Applies to backend code in `src/operation/iCTS/`.
- These rules are mandatory.
- Topic-specific rules live in `.trellis/spec/backend/`.

## Constraints

### Process

- AI agents must not run `git commit` or `git push`.
- Use read-only Git commands unless the human explicitly asks for more.

### Files and Naming

- Use `.hh` for headers and `.cc` for sources.
- Do not use `.h`, `.hpp`, `.cpp`, `.cxx`, or `.c` in iCTS.
- File names use PascalCase.
- Acronyms stay uppercase: `CTSAPI.hh`, `FLUTE.cc`, `CBS.cc`.
- All headers use `#pragma once`.

### New File Header

Every new `.hh` and `.cc` file must start with this copyright block:

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

Immediately after it, add a Doxygen file comment:

```cpp
/**
 * @file FileName.hh
 * @author Your Name (email@example.com)
 * @date YYYY-MM-DD
 * @brief One-line description of what this file contains
 */
```

### Mandatory Coding Rules

- Format code with the repository `.clang-format`.
- Do not use exceptions in iCTS code.
- Use `CTS_LOG_*` macros in iCTS code. Do not use global `LOG_*`, `std::cout`, or `printf`.
- Follow `backend/quality-guidelines.md` for naming, includes, and dependency visibility.
- Update CMake before implementing new files or modules.

### External Module Touches

- If an iCTS task must touch external modules such as iSTA or iPA, keep the diff minimally invasive and avoid unrelated formatting or cleanup.
- Keep iCTS-specific additions visually scoped with an appropriate namespace, struct, facade, or similarly explicit boundary.
- Do not use `ecc_dev_tools` to repair external-module findings as part of an iCTS task.
- In `finish-work`, explicitly remind the human to review external-module diffs.

### Terminology

Use established iCTS terms:
- `inst`, not `instance`
- `net`, not `wire`
- `pin`, not `port` except real top-level IO
- `cell_master`, not `cell_type` or `cell_name`
- `dbu` for integer design-base-unit coordinates
- `loads`, `clock_source`, `inserted_insts`, and `inserted_nets` consistently

### Required Validation

- Run `ecc_dev_tools` on touched paths before handoff.
- If public headers or CMake targets changed, also run structure checks.
- Use full `src/operation/iCTS` checks only as a final regression pass.

## Checklist

Before handoff, verify:

- [ ] File extensions and names follow the iCTS rules
- [ ] New headers use `#pragma once`
- [ ] New files include the required copyright and Doxygen header
- [ ] Code follows backend quality, logging, and error-handling specs
- [ ] CMake was updated before implementation when structure changed
- [ ] Path-scoped validation has been run
- [ ] External-module diffs, if any, remain minimal and are called out for human review

## Related Docs

- `backend/index.md`
- `backend/directory-structure.md`
- `backend/quality-guidelines.md`
- `backend/logging-guidelines.md`
- `backend/error-handling.md`
- `.trellis/ecc_dev_tools/README.md`
