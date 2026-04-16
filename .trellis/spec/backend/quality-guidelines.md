# Quality Guidelines

Code-quality rules for backend work in `src/operation/iCTS/`.

## Scope

This document covers naming, includes, dependency visibility, and validation.

Repository-wide constraints such as file extensions, copyright headers, and `#pragma once` live in `../project-constraints.md`.

## Rules

### Naming

- Use `snake_case` only for trivial accessors:
  - `get_name()`
  - `set_type(...)`
  - `is_buffer()`
- Use `camelBack` for computed or multi-step behavior:
  - `calcDelay()`
  - `hasViolation()`
  - `collectSinkPins()`
- Classes use `PascalCase`.
- Enums use `enum class` with `kPrefix` values.
- Members use `_lower_case`.
- Namespaces use lowercase.

Rule of thumb:
- if the body is more than a direct read, write, or simple comparison, use `camelBack`

### Modern C++

- Prefer consistent modern C++ style in touched iCTS code.
- Keep declarations and definitions aligned when you clean up an existing interface.

### Includes

- Prefer forward declarations when a header only needs a pointer or reference.
- Keep implementation-only includes in `.cc` files.
- Every header must be self-contained.

Use this include order, with blank lines between groups:
1. corresponding header in `.cc`
2. project headers
3. third-party headers
4. C++ standard library headers
5. C standard library headers

### Dependencies

- Express dependencies with `target_link_libraries`, not duplicated include paths.
- Default to `PRIVATE`.
- Use `PUBLIC` only when the dependency appears in public headers.
- Use `INTERFACE` for header-only libraries.
- Link an existing target instead of recreating its include path.
- Use the nearest logical CMake path variable.

### Forbidden Patterns

- `using namespace std;`
- `throw`, `try`, or `catch` outside the narrow exception documented in `error-handling.md`
- duplicated include-path wiring when a CMake target already exists
- adding heavy includes to headers when a forward declaration is enough

### Validation

Use the repository-local checker at `.trellis/ecc_dev_tools/check.py`.

Recommended flow:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path <touched-path>
python3 ./.trellis/ecc_dev_tools/check.py check --path <touched-path> --preset structure
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS --no-fail-on-findings --quiet
```

Detailed checker usage, presets, outputs, suppressions, and tool behavior live in `../../ecc_dev_tools/README.md`.

## Checklist

Before handoff, verify:

- [ ] Naming follows the backend rules
- [ ] Touched code keeps a consistent modern C++ style
- [ ] Headers are minimal and self-contained
- [ ] Include order is correct
- [ ] CMake dependencies use the correct visibility
- [ ] Public-header changes were checked with `--preset structure`
- [ ] IWYU findings were fixed or explicitly justified
- [ ] Path-scoped `ecc_dev_tools` validation has been run

## Related Docs

- `../project-constraints.md`
- `directory-structure.md`
- `logging-guidelines.md`
- `error-handling.md`
- `../../ecc_dev_tools/README.md`
