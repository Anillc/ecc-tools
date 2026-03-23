# brainstorm: cpp code check tool

## Goal

Build a script under `.trellis/ecc_dev_tools` to perform C++ code quality and CMake dependency checks for this repository, focused first on iCTS but designed so the workflow can be reused for other modules.

## Current Status

The tool is **substantially implemented** as a Python package under `.trellis/ecc_dev_tools/` containing 10 files totaling approximately 2700 lines of code. It is the canonical backend quality workflow for this repository.

### Implemented Features

* **clang-format checking** -- report-only and direct-fix modes, using the project root `.clang-format` config.
* **clang-tidy checking** with profile-based configuration, using `src/utility/.clang-tidy` as the config source.
  * **Deep tidy mode** runs 8 check category groups: `bugprone`, `clang-analyzer`, `modernize`, `performance`, `readability`, `misc`, `cppcoreguidelines`, `readability-identifier-naming`.
  * **Naming mode** runs only `readability-identifier-naming`.
  * Translation-unit pass, explicit analyzer pass, and on-demand header-only pass are all supported.
* **Header self-containedness checking** -- compiles each header in isolation to detect missing includes or incomplete target dependencies.
* **CMake dependency graph analysis** -- cycle detection and redundant direct-dependency detection via the CMake File API target model.
* **Clang frontend syntax-only pass** -- detects `clang-diagnostic-error` findings using compile command flags.
* **Native compiler fallback pass** -- on-demand `g++` syntax-only fallback when prior passes produce no diagnostics.
* **Scope-level filtering** -- all findings carry `location_scope_class` and `trigger_scope_class` tags; only in-scope findings affect the default exit code.
* **Multi-pass execution with deduplication** -- tidy passes have `dedupe_priority` and findings are merged across passes.
* **Structured reporting** -- human-readable output with severity/category/file/target statistics, in-scope vs. out-of-scope separation.
* **Profile system** -- `Profile` dataclass with scope roots, target prefixes, tidy config paths, and deep-tidy check lists. The `icts` profile is the first and currently only shipped profile.
* **Validation presets** -- `default`, `quality`, `structure`, `tidy-only` presets controlling which check kinds run.
* **Execution plan model** -- `ExecutionPlan` composed from a preset, a tidy mode (`deep`/`naming`), and a pass plan (`complete`/`legacy`/`tidy-only`), with resolved `TidyPass` objects.
* **Environment detection** -- discovers and validates versioned binaries (`clang-format`, `clang-tidy`, `clang++`, `g++`, `cmake`, `ninja`, `clang-scan-deps`); prefers the newest available binary; supports explicit binary override for `clang-tidy`.
* **Build context management** -- reuses or auto-refreshes `compile_commands.json` and CMake File API reply data from the repository `build/` directory.
* **Parallelism defaults** -- detects total CPU count and idle threads; defaults to half of idle threads.

### Module Architecture

| Module | Lines | Purpose |
|--------|-------|---------|
| `check.py` | 207 | CLI entry point (`argparse`-based) and orchestration |
| `models.py` | 221 | Dataclass-based data models (`Finding`, `CheckResult`, `ExecutionPlan`, `Profile`, `Scope`, etc.) |
| `profiles.py` | 227 | Profile definitions, validation presets, execution plan resolution |
| `checkers.py` | 1152 | All checker implementations (format, tidy TU/header passes, clang-frontend, native-fallback, headers, cmake) |
| `reporting.py` | 231 | Structured human-readable output formatting |
| `environment.py` | 192 | Tool discovery, version probing, validation |
| `build_context.py` | 345 | `compile_commands.json` loading, CMake File API parsing, auto-refresh |
| `scope.py` | 28 | Path scope resolution |
| `utils.py` | 88 | Shared utilities (subprocess runner, version parsing, path helpers) |
| `__init__.py` | 1 | Package marker |

## What I already know

* The requested checks are: code formatting, naming convention checking, header self-contained / target self-dependency completeness, and CMake dependency graph cycle / simplicity checks.
* Initialization should detect the user environment, required binaries, their versions, and choose a default parallelism of half of the idle CPU threads.
* Build/configure should use Ninja parallel builds.
* The tool must support batch operations for a specified folder or module.
* `format` needs both report-only and direct-fix modes; other checks can be report-only.
* Results should emphasize issues inside the requested module scope while classifying out-of-scope warnings/errors separately, with clear logs and summaries.
* Candidate tools named by the user: `clang-format`, `clang-tidy`, CMake File API, and `clang-scan-deps`.
* `.trellis/ecc_dev_tools/check.py` and its supporting modules now exist and should be documented as the canonical backend quality workflow.
* The repo already has a root `.clang-format` and `src/utility/.clang-tidy`.
* The top-level CMake configuration enables `CMAKE_EXPORT_COMPILE_COMMANDS`.
* Existing build tooling already prefers Ninja when available (`build.sh`).
* iCTS has a hierarchical CMake target structure rooted at `src/operation/iCTS/`.

## Assumptions (temporary)

* A Python-based CLI script in `.trellis/ecc_dev_tools` is acceptable.
* V1 can treat formatting as fixable, while naming / include / CMake checks remain report-only.
* The tool should prefer reusing the repository `build/` directory metadata to obtain `compile_commands.json` and File API data, with clear diagnostics when that metadata is missing or stale.
* Scope selection may be by path prefix, module path, and/or CMake target.

## Open Questions

* None for the current MVP boundary.

## Requirements (evolving)

* Run formatting checks using the repo `.clang-format`.
* Support formatting in both report-only and fix modes.
* Run naming/static-analysis checks using the project `clang-tidy` rules and diagnostics emitted for the selected scope.
* Prefer the newest available `clang-tidy` binary by default, while still allowing an explicit binary override for compatibility with other installed versions.
* Adapt config passing to the selected `clang-tidy` version (for example, older versions may need `--config` instead of `--config-file`).
* Use `header-filter`/equivalent scope control so scoped runs surface relevant header diagnostics without flooding output with unrelated repository warnings.
* Detect header self-contained issues and incomplete target dependency expression.
* Analyze CMake target linkage for cycles and unnecessary direct dependencies.
* Detect required binaries and their versions before execution.
* Use Ninja for configure/build/check stages.
* If reused `build/` metadata is missing or stale, auto-refresh it before running checks.
* Support batch operations for a specified folder/module.
* Use path-based scope selection as the primary user-facing entry, and map paths to related targets internally when needed.
* Classify findings by scope, severity, category, and summary.
* Out-of-scope findings must be reported separately and must not affect the default process exit code.
* Expose a single `check` command with selectable check kinds instead of separate top-level subcommands for each checker.
* Keep the MVP focused on the currently requested feature set, with human-readable output as the primary reporting mode.

## Acceptance Criteria (evolving)

* [x] The tool can run against a specified folder or module.
* [x] `format` supports both report-only and fix modes.
* [x] clang-tidy / dependency / CMake checks produce readable summaries with scope classification.
* [x] Scoped clang-tidy runs report all relevant diagnostics for files/headers inside the requested scope instead of only `readability-identifier-naming`.
* [x] Missing tools or unsupported versions fail fast with actionable messages.
* [x] The first version works well for iCTS while preserving a path to broader reuse.
* [x] The MVP delivers the requested checks without adding extra CI-oriented output or execution-control features.

## Definition of Done (team quality bar)

* Tests added/updated where appropriate
* Lint / quality checks green for changed code
* Docs/notes updated if behavior changes
* Failure handling and output format considered for the introduced CLI

## Research Notes

### Constraints from this repo

* Use project formatting and naming configs instead of introducing new rule sources.
* Reuse existing CMake + Ninja workflow where possible.
* iCTS is organized as multiple nested CMake targets, so target-graph and include-scope checks should understand parent/child aggregation.

### Feasible approaches here

**Approach A: iCTS-first checker**

* How it works: hardcode iCTS path and target conventions, with minimal abstraction.
* Pros: fastest to deliver; easiest to validate against current pain points.
* Cons: harder to reuse for other modules later.

**Approach B: generic core + iCTS profile** (Recommended -- implemented)

* How it works: build a small generic execution/reporting framework, with repo-specific profiles for naming config, module roots, and CMake conventions; ship iCTS as the first profile.
* Pros: matches "mainly iCTS, but general"; keeps V1 focused without baking in iCTS-only logic everywhere.
* Cons: slightly more upfront design work.
* Status: **This is the approach that was implemented.** The `Profile` dataclass and `PROFILES` registry in `profiles.py` realize this design.

**Approach C: repo-wide generic checker from day one**

* How it works: arbitrary CMake subtree discovery and rule handling for any module immediately.
* Pros: maximum reuse.
* Cons: larger MVP, more ambiguity, higher risk of over-design.

## Decision (ADR-lite)

**Context**: The checker is primarily for iCTS, but the user explicitly wants it to remain reusable for other modules.

**Decision 1**: Use a generic core with an iCTS profile as the first shipped profile.

**Consequences**: V1 should keep execution, reporting, and scope filtering generic, while allowing repo/module-specific conventions to live in profile configuration. This adds a little upfront structure, but avoids painting the tool into an iCTS-only corner.

**Decision 2**: Prefer reusing the repository's existing `build/` metadata when available, instead of forcing a dedicated checker-owned build directory.

**Consequences**: V1 can integrate more smoothly with the current developer workflow and avoid redundant configure/build work, but results may depend on the current build state. The tool should therefore clearly report whether metadata was reused and fail with actionable guidance when required metadata is missing or stale.

**Decision 3**: Use a single `check` command as the main user-facing entry point, with flags to select one or more check kinds.

**Consequences**: Reporting and summary generation stay unified, and common options such as scope, parallelism, and output mode need to be designed once. Internally, the implementation should still keep per-check executors modular so future commands can be added if needed.

**Decision 4**: If reused `build/` metadata is missing or stale, V1 should auto-refresh it rather than immediately failing.

**Consequences**: The tool becomes more usable in day-to-day development and better matches the requested workflow. It also means V1 needs clear logging around what refresh actions were taken, which build directory was used, and when refresh failed.

**Decision 5**: When scoped checks surface out-of-scope diagnostics, only in-scope findings should affect the default exit code.

**Consequences**: V1 should clearly separate in-scope and out-of-scope summaries, so the tool stays useful for focused module work without hiding broader repository problems. Out-of-scope findings should still be visible in reports, but not fail the command by default.

**Decision 6**: Use path-based scope selection as the primary user-facing scope model.

**Consequences**: V1 should let users point at folders/files/modules in a natural way, while internally resolving related compile commands and CMake targets as needed. This keeps the CLI ergonomic, but requires an internal mapping layer from path scope to build/target scope.

**Decision 7**: Keep the MVP limited to the current requested feature set, without adding extra CI-oriented JSON output or advanced execution-control features.

**Consequences**: V1 can stay focused on delivering the core developer workflow first. Internally, the reporting layer should still be structured enough that machine-readable output can be added later without rewriting the whole tool.

## Technical Approach

Implement a Python-based checker under `.trellis/ecc_dev_tools` with a generic execution/reporting core and an iCTS profile as the first concrete profile. The main entry point will be a single `check` command that accepts path-based scope selection and a selectable set of check kinds. The tool will validate environment/tools first, prefer reusing the repository `build/` metadata, auto-refresh that metadata with Ninja when needed, then run modular check executors for formatting, clang-tidy-based scoped diagnostics, self-contained header / target dependency checks, and CMake graph checks. The clang-tidy executor should prefer the newest available binary by default, support explicit binary selection, choose compatible config flags by version, use scope-aware header filtering to expose relevant header diagnostics without flooding unrelated warnings, and still run a header-oriented tidy fallback when the selected scope has headers but no compile-command-backed translation units. Results will be aggregated into unified human-readable summaries that clearly separate in-scope and out-of-scope findings.

## Out of Scope (explicit)

* Automatic repair for naming, include, or CMake dependency issues in V1
* Dedicated CI/machine-readable JSON reporting in V1
* Advanced per-check execution policy controls beyond the requested default behavior
* IDE/editor integration
* Non-C++ language checks

## Implementation Plan (small PRs)

* PR1: Refactor shared checker models/reporting so findings carry origin, confidence, trigger-vs-location scope semantics, and compressed summary support. **Status: Complete.** The `Finding` dataclass now carries `origin`, `confidence`, `trigger_path`, `trigger_line`, `trigger_target`, `location_scope_class`, and `trigger_scope_class` fields. `CheckResult` supports `in_scope_findings()`, `out_of_scope_findings()`, and `triggered_in_scope_findings()` filters.
* PR2: Refactor tidy execution/parsing to use explicit binary-selection policy, robust diagnostic normalization, scoped header filtering, and deep-tidy family summaries plus separate compiler-diagnostic summaries. **Status: Complete.** The environment module implements `_discover_latest_binary()` with version-sorted candidate selection. `profiles.py` defines pass plans with separate tidy-TU, analyzer, header, clang-frontend, and native-fallback passes. `checkers.py` implements all pass runners with diagnostic deduplication and category grouping.
* PR3: Improve header/dependency and CMake reporting fidelity with confidence/subtype tagging, then rerun the full iCTS workflow and compare results before/after. **Status: In progress.** The `Finding` model supports `confidence` and `subtype` fields. Remaining work is focused on tuning reporting output and validating results against the full iCTS scope.

## Follow-up Refactor Plan

### Issue 1: Tidy category parsing noise -- Addressed
* Diagnostics are now parsed into structured category groups via `TIDY_CATEGORY_GROUPS` in `checkers.py`.
* Compiler diagnostics (`clang-diagnostic-*`) are tracked separately from tidy check families in the reporting summary.
* Status: The core normalization is implemented. Minor edge cases in category classification may still exist.

### Issue 2: Tidy result volume is too large -- Partially addressed
* `CheckResult.detail_limit` caps per-check detail output (default 20).
* Reporting produces per-category and per-file summaries for tidy findings.
* Status: Summaries are generated. Further compression (top-N file ranking, fold-by-check-name) is a potential improvement.

### Issue 3: Scope semantics are too coarse -- Addressed
* `Finding` now carries both `location_scope_class` (where the diagnostic appears) and `trigger_scope_class` (which TU triggered it).
* `trigger_path`, `trigger_line`, and `trigger_target` fields are populated by the tidy and header checkers.
* Status: Implemented. Reporting consumes both scope dimensions.

### Issue 4: Header-check confidence is mixed -- Partially addressed
* `Finding.confidence` field is available (`high`/`medium`/`low`).
* `Finding.subtype` field can distinguish compile-failure headers from heuristic dependency suggestions.
* Status: The model supports it. Not all header checker code paths set confidence/subtype granularly yet.

### Issue 5: Result model is inconsistent across checkers -- Addressed
* All checkers now produce `Finding` objects with the unified field set (origin, confidence, scope dimensions, tags).
* `reporting.py` consumes only the shared `CheckResult`/`Finding` model.
* Status: Implemented. The shared model is the single reporting contract.

### Issue 6: Binary selection policy is scattered -- Addressed
* `environment.py` formalizes the policy: Python uses the current interpreter; all other tools use `_discover_latest_binary()` which probes versioned candidates (e.g., `clang-tidy-18`, `clang-tidy-17`, ..., `clang-tidy`) and selects the newest.
* Explicit binary override is supported for `clang-tidy` via `--clang-tidy-binary`.
* `ToolStatus` records `selection_policy`, `requested_executable`, and `selected_candidate` for transparent reporting.
* Status: Implemented and visible in environment output.

## Known Limitations

* No unit tests for the tool itself.
* No JSON/SARIF machine-readable output format.
* No progress indication during long-running checks.
* Single profile (`icts`) currently shipped; adding new profiles requires editing `profiles.py`.
* `clang-scan-deps` is listed as an optional tool but not yet used by any checker.

## Technical Notes

* Relevant constraints: `.trellis/spec/project-constraints.md`, `.trellis/spec/backend/quality-guidelines.md`
* Related historical task: `.trellis/tasks/archive/2026-02/02-27-check-cts-compliance/prd.md`
* Useful repo files observed so far:
  * `CMakeLists.txt` -- exports `compile_commands.json`
  * `build.sh` -- existing Ninja preference and environment checks
  * `src/operation/iCTS/CMakeLists.txt` and `src/operation/iCTS/source/CMakeLists.txt` -- hierarchical iCTS target structure
