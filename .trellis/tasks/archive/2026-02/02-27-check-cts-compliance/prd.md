# Check iCTS Code Compliance

## Goal
Audit all files in `src/operation/iCTS/` against the established code-spec guidelines and produce a violation report.

## Check Items
1. **File extensions** — `.cc`/`.hh` only, PascalCase names
2. **Copyright header** — Mulan PSL v2 block present on every `.cc`/`.hh` file
3. **Header guard** — `#pragma once` in every `.hh` file (no `#ifndef`)
4. **Logging** — `CTS_LOG_*` macros used, no direct `LOG_*` / `std::cout` / `printf`
5. **Code style** — clang-format compliance
6. **Naming conventions** — clang-tidy naming rules
7. **Forbidden patterns** — `throw`/`catch`, unscoped `enum`, `using namespace std`
8. **Terminology** — Consistent use of domain terms (inst not instance, net not wire, etc.)

## Acceptance Criteria
- [ ] Each check item has been audited
- [ ] Violations listed with file path and line number
- [ ] Summary report with counts per category
