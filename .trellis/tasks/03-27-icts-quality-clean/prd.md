# Clean All In-Scope iCTS Quality Findings

## Goal

Resolve all 350 in-scope findings reported by ecc_dev_tools full analysis on `src/operation/iCTS`, producing clean, idiomatic C++ code — not just suppressing warnings.

## Requirements

### Format (8 findings)
- Run `clang-format` fix on 8 files with formatting deviations

### Tidy (329 findings)
- `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` (154): Use `.at()` where bounds checking is appropriate
- `readability-math-missing-parentheses` (57): Add explicit parentheses for operator precedence clarity
- `modernize-use-trailing-return-type` (55): Convert lambdas to use trailing return types
- `modernize-use-designated-initializers` (48): Use C++20 designated initializers for aggregate init
- `modernize-use-ranges` (5): Modernize to std::ranges where applicable
- `modernize-use-scoped-lock` (3): Replace lock_guard with scoped_lock
- `readability-use-std-min-max` (3): Replace manual min/max patterns
- `performance-unnecessary-copy-initialization` (2): Use const reference instead of copy

### IWYU (13 findings)
- Add 5 missing includes (`<string>`)
- Add 4 missing forward declarations (idb namespace types in Wrapper.hh)
- Remove 4 unnecessary includes (Wrapper.hh, TopologyGenTest.cc)

## Acceptance Criteria

- [ ] `ecc_dev_tools check --path src/operation/iCTS` reports 0 in-scope findings
- [ ] All changes are idiomatic and do not break compilation
- [ ] No functional behavior changes

## Out of Scope

- Out-of-scope findings (621 items in external headers)
- Suppressions for false positives (already handled)

## Technical Notes

- Top problem files: BoundSkewTree.cc (127), GeomCalc.cc (61), LocalLegalization.cc (20), BSTRouter.cc (19), CharBuilder.cc (17)
- Headers and CMake checks already clean (0 findings)
