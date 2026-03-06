# Code Standards Audit: iCTS Module Full Compliance Check

## Goal
Audit all C++ source files in `src/operation/iCTS/` against the project spec guidelines, identify all violations, and fix them.

## Known Issues (from user)
1. `source/database/design/Clock.hh` — copyright has typo 'Pinitute' (likely Inst→Pin renaming error)
2. Multiple files have broken copyright format — 'Sciences Copyright (c)' from incorrect line wrapping
3. `source/utils/geometry/Geometry.hh` — naming violations
4. Various getter/setter and boolean method naming violations

## Audit Categories
1. **Copyright header** — must match spec exactly (no typos, no broken lines)
2. **Naming conventions** — classes, methods, getters/setters, booleans, members, locals, enums
3. **File naming** — PascalCase with .hh/.cc extensions
4. **Header guards** — must use `#pragma once`
5. **Forbidden patterns** — no `#ifndef` guards, no `.h/.hpp/.cpp`, no `throw`, no `using namespace std`
6. **Enum style** — must be `enum class` with `k` prefix values

## Acceptance Criteria
- [ ] Complete issue list generated
- [ ] All copyright headers fixed
- [ ] All naming violations fixed
- [ ] All forbidden patterns removed
- [ ] Final review passes
