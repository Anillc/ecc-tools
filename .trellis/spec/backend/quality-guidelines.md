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

### CTS Semantic Boundaries

Names and types in iCTS should reflect EDA/CTS concepts:

- Prefer CTS/physical-design terms such as `clock tree`, `sink domain`, `source-to-root`, `downstream tree`, `root buffer`, `topology level`, `routing segment`, `flyline segment`, and `committed design object`.
- Avoid generic backend/service terms for CTS flow code unless the code is truly generic infrastructure.
- Do not introduce source or test names containing `snapshot`; name the CTS object and purpose instead, such as `LibertyCellModel`,
  `ClockNetMembershipRestore`, `TimingState`, or `ReportSample`.
- Avoid standalone generic names such as `Internal`, `Support`, `Request`, `Response`, `Types`, `Input`, `Session`, `rollback`, and `fallback`.
  Replace them with CTS object/action names, such as `ClockWritebackPlan`, `StaWireRcQuery`, `ClockSizingEdit`, `SinkDomainBuild`, or
  `ClockNetMembershipRestore`.
- Avoid standalone `Network` in new CTS names because it can be confused with `Net` / clock-net semantics. `ClockNetwork` is allowed when it refers
  to the established database clock-network model.
- Domain terms that are not structural placeholders remain valid when they name the actual EDA concept, for example Liberty `internal_power` tables
  or root-buffer input pins.
- Use `enum class` or narrow value types for behavioral concepts such as instance role, net role, sink domain, synthesis phase, route role, violation type, and topology level.
- Module-qualified flow contracts such as `HTreeInput`, `HTreeConfig`, `HTreeOutput`, and `HTreeSummary` are preferred when they define a stable
  algorithm or stage boundary. Avoid standalone generic `Input`, `Output`, `Options`, or `Result` names unless they are nested in a narrow class
  or are private local helper types.
- Strings are allowed for object names, logs, diagnostics, file paths, and display labels.
- Do not use object-name substrings to decide CTS behavior.

### Namespaces

- In `.cc` files, define top-level iCTS implementations in `namespace icts {}` and internal submodules in qualified namespaces such as `namespace icts::htree_builder {}`.
- Keep anonymous namespaces inside the active named namespace.
- Avoid mixing `namespace icts {}` and `namespace icts::submodule {}` in one `.cc` unless a minimal outer forward declaration is necessary.
- Do not import whole namespaces with `using namespace`; use explicit qualification or narrow symbol-level `using` declarations in the smallest practical scope.

### Modern C++

- Prefer consistent modern C++ style in touched iCTS code.
- Keep declarations and definitions aligned when you clean up an existing interface.

### Includes

- Prefer forward declarations when a header only needs a pointer or reference.
- Keep implementation-only includes in `.cc` files.
- Every header must be self-contained.
- Do not use `../` or `../../` relative include traversal in iCTS source or test code; if a rooted include does not resolve, fix the target's `PUBLIC` or `INTERFACE` include directories instead.

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
- behavioral branching based on CTS object-name substrings
- broad snapshots that duplicate queryable CTS/iDB state without a clear stage contract

### Validation

Use the repository-local checker at `.trellis/ecc_dev_tools/check.py`.

Default iCTS workflow:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Guidance:
- Do not run `ecc_dev_tools` during the normal edit/build/test loop unless you are explicitly debugging the checker itself.
- Reserve `ecc_dev_tools` for the final `finish-work` pass.
- Run only one `ecc_dev_tools` invocation per build directory at a time. Concurrent checker runs that share `build/` can race during CMake metadata refresh or static archive generation.
- Treat the final full-module pass as the source of truth; if it reports in-scope findings, fix them and rerun the same full pass until they are clean.
- Do not use `NOLINTNEXTLINE`, broad suppressions, or similar local checker bypasses to make `ecc_dev_tools` pass. Fix the code structure, types, includes, or target wiring instead.
- When splitting a large translation unit, rebuild each new `.cc` with the minimal include list for the symbols it directly uses; do not copy the original broad include block into every split file.

### AI Preflight Heuristics

Use targeted preflight scans before large CTS edits or reviews when the touched area is likely to cross a spec boundary. These scans are not a substitute for the final `ecc_dev_tools` pass; they are fast ambiguity reducers for AI agents before implementation.

| Trigger | Probe | Interpret as a problem when |
|---------|-------|-----------------------------|
| Changing flow/module/report/evaluation code near database access | `rg -n "idb::|ista::TimingEngine|TimingIDBAdapter|api/Power.hh|STAAdapter" src/operation/iCTS/source/flow src/operation/iCTS/source/module src/operation/iCTS/api` | Raw external types appear in flow/module/API contracts instead of staying behind `Wrapper` or CTS-local adapters. |
| Changing SDC, Wrapper, or FastSTA adapter code | `rg -n "idb::|SdcClock|FastSta|ista::" src/operation/iCTS/source/database/adapter src/operation/iCTS/source/database/io` | The result is leaving adapter files as raw pointers or engine dependencies rather than CTS value types. SDC setup-time projection and raw Liberty data use are allowed by `database-guidelines.md`. |
| Changing runtime ownership or API entry code | `rg -n "CTS_API_INST|getInst\(|_INST\b|CTSRuntime" src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test` | A source-layer module starts using an API singleton, service locator, or whole runtime instead of a narrow dependency. |
| Adding or renaming files | `find src/operation/iCTS -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.c' \) -print` | A new iCTS file uses a forbidden extension, except external headers outside the task scope should not be reformatted or renamed opportunistically. |
| Editing tests with shared fixtures | `rg -n "using namespace" src/operation/iCTS/test src/operation/iCTS/source -g '*.cc' -g '*.hh'` | A new whole-namespace import appears. Existing narrow exceptions are listed in `../project-constraints.md`; prefer explicit qualification when touching those files. |
| Touching hot lookup/report/evaluation code | `rg -n "_insts|_nets|get_instance_list|std::erase_if|std::ranges::find_if|findInst|findNet|findPin" src/operation/iCTS/source/database/design src/operation/iCTS/source/flow/report src/operation/iCTS/source/flow/evaluation` | A name lookup, report, or evaluation path compensates for missing indexes by scanning all CTS objects or all iDB objects. Fix the owning index/query path instead. |

When a probe finds matches, classify each match as:

1. allowed adapter-local implementation;
2. accepted existing exception from `../project-constraints.md`;
3. in-scope violation to fix now;
4. out-of-scope legacy finding to report but not churn.

Detailed checker usage, presets, outputs, suppressions, and tool behavior live in `../../ecc_dev_tools/README.md`.

## Checklist

Before handoff, verify:

- [ ] Naming follows the backend rules
- [ ] Touched code keeps a consistent modern C++ style
- [ ] Boundary-sensitive edits had targeted preflight scans or an explicit reason they were unnecessary
- [ ] Final full `src/operation/iCTS` `ecc_dev_tools` validation was run in `finish-work`

## Related Docs

- `../project-constraints.md`
- `directory-structure.md`
- `logging-guidelines.md`
- `error-handling.md`
- `../../ecc_dev_tools/README.md`
