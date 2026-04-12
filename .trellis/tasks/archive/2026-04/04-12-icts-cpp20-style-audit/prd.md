# brainstorm: audit iCTS C++20 style consistency

## Goal

Audit the `iCTS` module for consistency of C++20-related style choices, with an initial emphasis on function declaration style differences such as trailing return syntax (`auto foo() -> T`) versus traditional declarations (`T foo()`), then produce a categorized inventory and a clear cleanup baseline for future normalization.

## What I already know

* The target scope is the `iCTS` module in this repository.
* The user wants a comprehensive review rather than a spot check.
* One concrete inconsistency already called out is function return-style syntax.
* The expected output includes classification and a checklist/inventory.
* The module spans `api/`, `source/`, and `test/`; current scan found 171 C/C++ files under `src/operation/iCTS/`.
* Function declaration style is clearly uneven by area:
  * `test/` is heavily biased toward trailing return style.
  * legacy `database/`, `design/`, `config/`, and several headers remain predominantly traditional.
  * some files and submodules are transitional and mix both styles in the same file.
* Beyond return syntax, there are scattered modern C++ markers already in use (`concept`, `requires`, `std::ranges`, `[[nodiscard]]`, `constexpr`, `using` aliases), but their distribution is also uneven.

## Assumptions (temporary)

* This task is primarily an audit/reporting task, not an immediate bulk refactor.
* The audit should cover style consistency that is visible in code structure and declarations, not just compiler feature usage.
* The output should be useful as a decision basis for a later normalization pass.

## Open Questions

* None at the moment.

## Requirements (evolving)

* Scan the `iCTS` module for C++20 style patterns and inconsistencies.
* Identify representative categories instead of listing files as an undifferentiated dump.
* Produce a structured inventory with examples.
* Distinguish between clearly modernized areas, clearly legacy areas, and mixed transitional areas.
* Scope is the full `iCTS` tree: `source/ + api/ + test/`.
* Use function signature style as the primary axis.
* Also classify visible modern-style markers as secondary axes: `std::ranges`, `concept`, `requires`, `[[nodiscard]]`, `noexcept`, and `constexpr`.
* Include a proposed normalization baseline for future style unification.
* Include a follow-up cleanup priority / sequencing recommendation.

## Acceptance Criteria (completed)

* [x] The audit scope within `iCTS` is explicitly defined.
* [x] At least one concrete classification scheme for style inconsistencies is established.
* [x] The resulting report includes representative file/function examples for each category.
* [x] The report explains whether findings are concentrated in `source/`, `api/`, `test/`, or specific submodules.
* [x] The report distinguishes primary signature-style inconsistency from secondary C++20 marker distribution.
* [x] The report proposes a target house style with rationale.
* [x] The report recommends a practical cleanup order instead of a single repo-wide bulk rewrite.

## Definition of Done (team quality bar)

* Analysis is reproducible from repository state.
* Findings are grouped in a way that can drive follow-up cleanup work.
* Any assumptions or exclusions are stated explicitly.
* The proposed target style is actionable enough to guide follow-up refactors.

## Out of Scope (explicit)

* Automatic bulk rewriting of the module.
* Enforcing a formatter or clang-tidy policy in this task unless later requested.
* Deep semantic modernization unrelated to visible style consistency.
* Mandatory conversion of every existing modern feature usage into a single “modernity score”.

## Technical Approach

1. Build a full-tree inventory for `api/`, `source/`, and `test/`.
2. Use function declaration/definition style as the primary normalization axis:
   * trailing return style: `auto foo(...) -> T`
   * traditional return style: `T foo(...)`
   * mixed files / mixed submodules
3. Use secondary marker distribution only as supporting evidence:
   * `std::ranges`
   * `concept` / `requires`
   * `[[nodiscard]]`
   * `noexcept`
   * `constexpr`
4. Produce output in three layers:
   * current-state classification
   * proposed target baseline
   * cleanup priority and sequencing

## Decision (ADR-lite)

**Context**: The user requested a comprehensive iCTS style audit, initially pointing at trailing-return inconsistency. During brainstorming, it became clear that a function-signature-only scan would miss meaningful secondary style clusters, while a purely descriptive inventory would not be actionable for future cleanup.

**Decision**: Audit the full `iCTS` tree (`source/`, `api/`, `test/`) with function signature style as the primary axis, and include selected visible C++20 style markers as secondary axes. The final deliverable will include both a proposed normalization baseline and a recommended cleanup priority.

**Consequences**:
* The report will be more actionable for follow-up refactors.
* `constexpr` and other markers will be interpreted contextually rather than treated as a simplistic modernization score.
* The task remains analysis-first and avoids a risky bulk rewrite.

## Technical Notes

* Task created at `.trellis/tasks/04-12-icts-cpp20-style-audit/`.
* Repo scan identified these top-level areas:
  * production code: `src/operation/iCTS/api`, `src/operation/iCTS/source`
  * tests and helpers: `src/operation/iCTS/test`
* Conservative function-signature scan summary:
  * `source/`: trailing named functions `414`, trailing operators `2`, traditional named functions `496`, traditional operators `17`
  * `api/`: trailing named functions `2`, traditional named functions `5`
  * `test/`: trailing named functions `349`, trailing operators `2`, traditional named functions `88`
* Representative modern-heavy files:
  * `source/module/routing/bound_skew_tree/BoundSkewTree.hh`
  * `source/module/topology/linear_clustering/*.hh|*.cc`
  * `source/database/io/Wrapper.cc`
  * many newer `test/` helpers and topology tests
* Representative traditional-heavy files:
  * `source/database/config/Config.hh`
  * `source/database/design/*.hh`
  * `source/database/timing/RCTree.hh`
  * `source/module/timing/TimingEngine.hh`
* Representative mixed files:
  * `api/CTSAPI.hh`
  * `source/database/io/Wrapper.hh`
  * `source/module/routing/Router.cc`
  * `test/common/TestUtils.hh` / `TestUtils.cc`
  * `source/module/routing/bound_skew_tree/Components.hh`
* Additional C++20-style markers observed in current tree:
  * `std::ranges`: widespread in newer routing/topology/test utilities
  * `concept` / `requires`: present but rare, notably in `bound_skew_tree/Components.hh`
  * `[[nodiscard]]`: sparse and localized
  * `constexpr`: common, but mostly as constants rather than as a broader style discipline
* Secondary marker distribution after full-tree scan:
  * `api/`: `ranges 1`, others nearly absent
  * `source/`: `ranges 58`, `concept 1`, `requires 7`, `nodiscard 8`, `noexcept 5`, `constexpr 58`
  * `test/`: `ranges 15`, `concept 0`, `requires 5`, `nodiscard 2`, `noexcept 0`, `constexpr 230`
* Current interpretation:
  * `constexpr` is heavily used in tests as a data/constants style, so it should not be treated the same way as trailing-return modernization.
  * `concept` / `requires` are niche local adoptions, not a repo-wide style baseline.
  * `std::ranges` is the most meaningful secondary “modern style spread” signal in production code.
