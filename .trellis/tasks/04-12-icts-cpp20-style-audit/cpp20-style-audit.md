# iCTS C++20 Style Consistency Audit

## Scope

This audit covers the full `iCTS` tree under:

- `src/operation/iCTS/api`
- `src/operation/iCTS/source`
- `src/operation/iCTS/test`

Primary axis:

- function signature style
  - trailing return style: `auto foo(...) -> T`
  - traditional style: `T foo(...)`

Secondary axes:

- `std::ranges`
- `concept`
- `requires`
- `[[nodiscard]]`
- `noexcept`
- `constexpr`

## Method

- Count named function declarations and definitions conservatively from C/C++ source files.
- Exclude constructors, destructors, most control-flow statements, and obvious non-function noise.
- Treat secondary markers as distribution signals, not as a single "modernity score".

## Executive Summary

- The strongest inconsistency is still function signature style.
- `test/` is already strongly aligned with trailing return style.
- `source/` is split:
  - newer routing and topology code is often trailing-return-heavy
  - older database, data-model, characterization, timing, and utility headers remain traditional
- `api/` is small but visibly mixed.
- Secondary C++20 markers are not uniformly adopted:
  - `std::ranges` has meaningful spread in newer production code
  - `concept` and `requires` are niche, local to a few generic or algorithm-heavy areas
  - `[[nodiscard]]` and `noexcept` are sparse and should remain semantics-driven
  - `constexpr` is common, but heavily dominated by test constants rather than broad style modernization

## Inventory Summary

### Function-signature totals

| Area | Files with signatures | All trailing | All traditional | Mixed |
|------|------------------------|--------------|-----------------|-------|
| `api/` | 2 | 1 | 0 | 1 |
| `source/` | 71 | 13 | 39 | 19 |
| `test/` | 63 | 32 | 5 | 26 |

### Signature counts

| Area | Trailing signatures | Traditional signatures |
|------|----------------------|------------------------|
| `api/` | 2 | 5 |
| `source/` | 416 | 513 |
| `test/` | 351 | 88 |

### Secondary marker totals

| Area | `ranges` | `concept` | `requires` | `[[nodiscard]]` | `noexcept` | `constexpr` |
|------|----------|-----------|------------|------------------|------------|-------------|
| `api/` | 1 | 0 | 0 | 0 | 0 | 0 |
| `source/` | 58 | 1 | 7 | 8 | 5 | 58 |
| `test/` | 15 | 0 | 5 | 2 | 0 | 230 |

## Current-State Classification

### A. Modern-heavy areas

These areas already look like they are trending toward a modern house style centered on trailing return syntax and selective use of `std::ranges`.

- `source/module/routing/bound_skew_tree`
  - signatures: trailing `246`, traditional `129`
  - markers: `ranges 32`, `concept 1`, `requires 6`, `nodiscard 8`, `constexpr 19`
  - note: modern-heavy overall, but still not internally consistent because several files remain mixed or traditional
- `source/module/topology/linear_clustering`
  - signatures: trailing `63`, traditional `24`
  - markers: `ranges 5`, `requires 1`, `constexpr 11`
  - note: this is one of the clearest production submodules already moving toward a trailing-return baseline
- `source/database/io`
  - signatures: trailing `21`, traditional `7`
  - markers: `ranges 2`
  - note: implementation file is already modern-heavy, header is mixed
- `test/module/topology`
  - signatures: trailing `249`, traditional `21`
  - markers: `ranges 10`, `requires 5`, `constexpr 107`
  - note: tests are overwhelmingly trailing-return-oriented
- `test/common/io`, `test/common/realtech`, `test/common/visualization`
  - mostly modern helper style with trailing returns dominating

Representative examples:

- `source/module/routing/bound_skew_tree/BoundSkewTree.hh`
- `source/module/topology/linear_clustering/SequenceSplitter.hh`
- `source/database/io/Wrapper.cc`
- `test/module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh`

### B. Legacy-heavy areas

These areas still read like earlier-generation iCTS code and are predominantly traditional in function signatures, even when individual C++20 markers appear occasionally.

- `source/database/design`
  - signatures: trailing `0`, traditional `50`
- `source/database/characterization`
  - signatures: trailing `0`, traditional `41`
- `source/database/spatial`
  - signatures: trailing `0`, traditional `34`
  - note: this area does use `std::ranges`, but signature style remains fully traditional
- `source/database/timing`
  - signatures: trailing `0`, traditional `16`
- `source/module/characterization`
  - signatures: trailing `9`, traditional `44`
  - note: mostly traditional despite a few modern helpers
- `source/utils/geometry`
  - signatures: trailing `0`, traditional `4`

Representative examples:

- `source/database/config/Config.hh`
- `source/database/design/Clock.hh`
- `source/database/design/Inst.hh`
- `source/database/timing/RCTree.hh`
- `source/module/characterization/PatternCombiner.hh`

### C. Transitional mixed areas

These are the best indicators of inconsistency because both styles coexist in the same file or tightly related file pair.

- `api`
  - `CTSAPI.hh` mixes traditional API declarations with one trailing-return feature API
  - `CTSAPI.cc` is trailing-only
- `source/database/adapter`
  - `STAAdapter.cc` is modern-heavy
  - `STAAdapter.hh` is traditional-heavy
- `source/database/io`
  - `Wrapper.cc` is all trailing
  - `Wrapper.hh` mixes traditional getters with trailing-return query helpers
- `source/database/config`
  - `Config.hh` is mostly traditional
  - `Config.cc` introduces some trailing-return helpers
- `source/module/routing`
  - root files, `local_legalization`, and `concurrent_bst_salt` remain mixed
- `source/module/topology/clustering`
  - close to balanced between the two styles
- `test/common/data`, `test/common/io`, `test/common/visualization`
  - tests and helpers are not fully uniform yet even though their overall direction is modern

Representative examples:

- `api/CTSAPI.hh`
- `source/database/io/Wrapper.hh`
- `source/module/routing/Router.cc`
- `source/module/routing/local_legalization/LocalLegalization.cc`
- `test/common/TestUtils.hh`
- `test/common/data/distribution/DistributionGenerators.cc`

## Secondary Marker Interpretation

### `std::ranges`

This is the most meaningful secondary marker in production code.

- Concentrated in newer algorithmic and data-processing code:
  - `source/module/routing/bound_skew_tree/BoundSkewTree.cc`
  - `source/module/routing/bound_skew_tree/GeomCalc.cc`
  - `source/database/adapter/sta/STAAdapter.cc`
  - `source/module/topology/linear_clustering/LinearOrderGenerator.cc`
  - `source/module/timing/TimingEngine.cc`
- Present in tests too, but less decisively than trailing returns

Recommendation:

- Treat `std::ranges` as preferred in new algorithm-heavy code when it improves readability.
- Do not rewrite old data-model headers just to introduce `ranges`.

### `concept` and `requires`

These are highly localized and should not be treated as a repo-wide baseline yet.

- `concept` appears in `source/module/routing/bound_skew_tree/Components.hh`
- most `requires` hits cluster around:
  - `source/module/routing/bound_skew_tree/BoundSkewTree.cc`
  - `source/module/topology/linear_clustering/LinearClusteringTypes.hh`
  - topology-related test support

Recommendation:

- Allow them in genuinely generic code.
- Do not impose them broadly as a consistency target for all of iCTS.

### `[[nodiscard]]`

Adoption is sparse and targeted.

- mostly in `source/module/routing/bound_skew_tree/GeomCalc.cc`
- minor use in `test/common/visualization/core/SvgCommon.hh`

Recommendation:

- Apply only to helpers where ignoring the result is plausibly a bug.
- Do not make blanket expansion part of the first cleanup phase.

### `noexcept`

Very limited and semantically scoped.

- mostly in `source/utils/logger/Logger.hh`
- `source/utils/logger/Logger.cc`
- one hash-style helper in `source/database/characterization/PatternId.hh`

Recommendation:

- Keep it semantics-driven.
- Do not use it as a primary style unification target.

### `constexpr`

This is common, but heavily skewed by tests.

- `test/` contains `230` hits, mostly constants and fixtures
- `source/` contains `58` hits, often for small constants or table helpers

Recommendation:

- Count it as evidence of modern local habits, not as a reliable proxy for overall style consistency.
- Avoid using `constexpr` density as a modernization score.

## Recommended Target Baseline

### Primary baseline

Use trailing return syntax as the default house style for new and touched iCTS code:

- prefer `auto foo(...) -> T`
- keep constructors and destructors as usual
- keep operators consistent with the same rule where practical

Rationale:

- it already dominates in `test/`
- it is the clear direction in newer routing and topology code
- it works better with attributes, qualifiers, long return types, and complex template-heavy helpers
- choosing traditional syntax as the future baseline would create more churn in the actively evolving parts of the tree

### Secondary baseline

Do not force all other C++20 markers into a single repo-wide mandate.

- `std::ranges`: prefer in new algorithmic code when clearer
- `concept` / `requires`: use only where generic constraints add value
- `[[nodiscard]]`: use only for bug-prone ignored results
- `noexcept`: use only when semantically stable
- `constexpr`: use for actual compile-time constants and helpers, not as style decoration

## Cleanup Priority

### Priority 1: Mixed files inside modern-heavy areas

These give the highest consistency win for the least conceptual risk.

- `api/CTSAPI.hh`
- `source/database/io/Wrapper.hh`
- `source/module/routing/Router.cc`
- `source/module/routing/local_legalization/LocalLegalization.cc`
- `source/module/routing/concurrent_bst_salt/CBSRouter.cc`
- `source/module/topology/clustering/Clustering.cc`
- `test/common/TestUtils.hh`
- `test/common/TestUtils.cc`

Why first:

- they are already adjacent to trailing-return-heavy code
- the local team direction is visible
- cleanup here reduces the "same file, two dialects" problem immediately

### Priority 2: Modern-heavy submodules with residual traditional signatures

Normalize entire submodules that already lean modern.

- `source/module/routing/bound_skew_tree`
- `source/module/topology/linear_clustering`
- `test/module/topology`
- `test/common/io`
- `test/common/visualization`

Why second:

- these are active algorithmic areas
- they already use other modern C++20 features
- the benefit compounds because these files are read together

### Priority 3: Cross-boundary adapter and API surfaces

These are important for consistency at module boundaries but have slightly more compatibility and readability concerns.

- `source/database/adapter/sta`
- `source/database/config`
- `source/module/timing`
- `source/utils/logger`
- `api`

Why third:

- these files sit at boundary layers
- they are visible to many callers
- they are mixed, but not always worth mass-editing before high-churn algorithmic modules

### Priority 4: Legacy data-model and characterization headers

These are the lowest-priority bulk cleanup candidates.

- `source/database/design`
- `source/database/characterization`
- `source/database/spatial`
- `source/database/timing`
- `source/module/characterization`
- `source/utils/geometry`

Why last:

- they are internally consistent already, even if they use the old style
- they tend to contain many compact getters, setters, and value-type helpers
- mass-conversion here creates large diff volume with relatively small readability benefit

## Practical Rule Set For Follow-up Refactors

- Rule 1: In new and touched files, default to trailing return style.
- Rule 2: When a file is already clearly trailing-return-oriented, convert the remaining traditional signatures while editing nearby code.
- Rule 3: Do not mix styles within the same file unless there is a deliberate, documented reason.
- Rule 4: Do not force `concept`, `requires`, `[[nodiscard]]`, `noexcept`, or `constexpr` as blanket transformations.
- Rule 5: Convert legacy all-traditional headers only as focused module cleanups, not as opportunistic drive-by churn.

## Suggested First Cleanup Batch

If the next step is an actual code cleanup, start here:

- `api/CTSAPI.hh`
- `source/database/io/Wrapper.hh`
- `source/module/routing/Router.cc`
- `source/module/routing/local_legalization/LocalLegalization.cc`
- `source/module/topology/clustering/Clustering.cc`
- `test/common/TestUtils.hh`
- `test/common/TestUtils.cc`

This batch is small enough to review, but representative enough to establish the baseline.
