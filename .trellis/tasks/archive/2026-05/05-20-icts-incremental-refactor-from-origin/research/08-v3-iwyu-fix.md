# V3 ecc dev check Fix Report

Scope: all in-scope findings reported by `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.

## Pre-fix

- In-scope findings: **103**
- By category:
  - iwyu: 50 (unnecessary-include 22 / missing-forward-decl 21 / missing-include 7)
  - cppcoreguidelines: 14 (virtual-class-destructor 7 / explicit-virtual-functions+modernize-use-override 7)
  - modernize: 13 (use-trailing-return-type 7 / use-scoped-lock 4 / use-ranges 1 / loop-convert 1)
  - visibility: 16 (should-be-public 9 / should-be-private 7)
  - format: 6 (needs-reformat)
  - simplicity: 4 (redundant-direct-link)

## Post-fix

- In-scope findings: **1**
- Remaining: 1 simplicity/redundant-direct-link advisory (see Unresolved)

## File-by-file changes (24 files touched)

### Source headers (12)

- `src/operation/iCTS/source/database/config/Config.hh` — lambda trailing `-> bool`; destructor `public ~Config() override = default;` (was private).
- `src/operation/iCTS/source/database/design/Design.hh` — lambda trailing `-> bool`; destructor `public ~Design() override;` (was private, definition kept in `.cc`).
- `src/operation/iCTS/source/database/io/Wrapper.hh` — lambda trailing `-> bool`; destructor `public ~Wrapper() override = default;` (was private).
- `src/operation/iCTS/source/flow/Flow.hh` — lambda trailing `-> bool`; destructor `public ~Flow() override = default;` (was private).
- `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh` — lambda trailing `-> bool`; destructor `public ~FastSTA() override;` (was private, definition kept in `.cc`).
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh` — lambda trailing `-> bool`; destructor `public ~STAAdapter() override = default;` (was private).
- `src/operation/iCTS/source/utils/logger/Schema.hh` — lambda trailing `-> bool`; destructor `public ~SchemaWriter() override = default;` (was private).
- `src/operation/iCTS/source/utils/singleton/SingletonRegistry.hh` — removed `#include "ResettableInterface.hh"`; added forward decl.
- `src/operation/iCTS/source/module/characterization/builder/CharBuilder.hh` — removed 2 includes (`BufferingPattern.hh`, `SegmentChar.hh`), added forward decls.
- `src/operation/iCTS/source/module/characterization/builder/CharBuilderImpl.hh` — removed 3 includes, added 3 forward decls.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/BoundSkewTree.hh` — removed 2 includes, added 4 forward decls (Area / 3 BST enums/structs).
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstBalanceSolver.hh` — removed 1 include.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstBottomUpTopDownDriver.hh` — removed Components.hh, added `Area` forward decl.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstEmbeddingSolver.hh` — removed BSTRoutingConfig.hh, added forward decl for `enum class BSTRoutingRCPattern`.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstInfeasibleMergeSolver.hh` — removed Components.hh, added `Area` forward decl.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstTopologyBuilder.hh` — removed 2 includes, added 2 forward decls.

### Source .cc files (8)

- `src/operation/iCTS/api/CTSAPI.cc` — removed 4 unnecessary includes (Config.hh / Design.hh / Wrapper.hh / Schema.hh).
- `src/operation/iCTS/source/module/characterization/builder/CharBuilder.cc` — added 3 forward decls.
- `src/operation/iCTS/source/module/characterization/builder/CharBuilderImpl.cc` — added 3 includes (BufferingPattern.hh / SegmentChar.hh / CharacterizationBufferCell.hh).
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/BoundSkewTree.cc` — added Components.hh include + 3 forward decls.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BoundSkewTreeImpl.cc` — added `#include "Point.hh"`.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstInfeasibleMergeSolver.cc` — removed GeomCalc.hh; reformatted.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstTopologyBuilder.cc` — removed cstdint + GeomCalc.hh.
- `src/operation/iCTS/source/utils/singleton/SingletonRegistry.cc` — added `ResettableInterface.hh` + `<mutex>` + `<ranges>`; rewrote 4 `std::lock_guard` → `std::scoped_lock`; `std::find` → `std::ranges::find`; reverse-iterator loop → `std::ranges::reverse_view`.

### Formatted only (4 — clang-format)

- `src/operation/iCTS/source/module/characterization/builder/CharSetupConfigurator.cc`
- `src/operation/iCTS/source/module/characterization/sampling/CharStaSampler.cc`
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstBalanceSolver.cc`
- `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstEmbeddingSolver.cc`

### Test file (1)

- `src/operation/iCTS/test/utils/singleton/SingletonRegistryTest.cc` — removed `<vector>`; added `<utility>`; reformatted.

### CMakeLists.txt (5)

- `src/operation/iCTS/source/database/adapter/fast_sta/CMakeLists.txt` — 9 sub-targets each promoted `icts_source_utils_singleton` from PRIVATE to PUBLIC (matches the public-header dependency exposed by `FastSta.hh` via `ResettableInterface.hh` / `SingletonRegistry.hh`).
- `src/operation/iCTS/source/flow/CMakeLists.txt` — `icts_source_utils_singleton` and `icts_source_database_design` declared PUBLIC (Flow.hh's public surface depends on both).
- `src/operation/iCTS/source/flow/optimization/CMakeLists.txt` — `icts_source_flow_optimization_stages`: fast_sta + module_routing demoted to PRIVATE (no public-header use). `icts_source_flow_optimization` facade: stages demoted to PRIVATE so no transitive PUBLIC chain remains.
- `src/operation/iCTS/source/flow/synthesis/htree/CMakeLists.txt` — `icts_source_flow_synthesis_htree_topology`: `icts_source_module_characterization` moved to PRIVATE (no public include).
- `src/operation/iCTS/source/flow/synthesis/trace/CMakeLists.txt` — `icts_source_database_design` moved to PRIVATE.
- `src/operation/iCTS/source/flow/report/visualization/CMakeLists.txt` — `icts_source_database_design` moved to PRIVATE.
- `src/operation/iCTS/source/module/topology/CMakeLists.txt` — `lemon` moved to PRIVATE.

## Unresolved (1)

- `icts_source_flow` (TARGET): `simplicity/redundant-direct-link` — direct dependency on `icts_source_utils_singleton` is "may be redundant" because `flow PUBLIC -> flow_evaluation PUBLIC -> icts_source_database (INTERFACE) -> icts_source_database_design PUBLIC -> icts_source_utils_singleton` is also a valid PUBLIC chain.
  - Kept: removing the explicit PUBLIC link would immediately re-introduce a `visibility/should-be-public` finding because `Flow.hh` directly includes `ResettableInterface.hh` and `SingletonRegistry.hh`. Explicit declaration of direct public-header dependencies is preferred over silencing the medium-confidence advisory.

## Verification

- `bash build.sh` PASS — iEDA executable linked (no link-order regressions).
- `icts_test_utils_singleton` PASS — 5/5 tests green after destructor visibility flip.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` In-scope: **103 → 1**; out-of-scope unchanged (5690, all outside CTS).
