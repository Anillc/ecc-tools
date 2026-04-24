# Research: build-test-integration

- Query: Research build/test integration constraints for adding isolated modules under `src/operation/iCTS/source/module/numerical_characterization` and `src/operation/iCTS/source/flow/numerical_htree` without modifying existing code where possible. Inspect CMake patterns, test target registration macros, dependency boundaries, and whether self-contained CMakeLists inside new directories can be picked up automatically or require parent edits.
- Scope: internal
- Date: 2026-04-24

## Findings

### Files Found

- `src/operation/iCTS/CMakeLists.txt` - iCTS root CMake entry; defines root paths and explicitly adds `external_libs`, `source`, `api`, and `test`.
- `src/operation/iCTS/source/CMakeLists.txt` - source-layer aggregator; explicitly adds `database`, `utils`, `flow`, and `module`, then exposes `icts_source`.
- `src/operation/iCTS/source/module/CMakeLists.txt` - module-layer aggregator; defines module path variables, explicitly adds each active module, and links module targets into `icts_source_module`.
- `src/operation/iCTS/source/flow/CMakeLists.txt` - flow-layer aggregator; explicitly adds `htree` and `synthesis`, then links flow targets into `icts_source_flow`.
- `src/operation/iCTS/source/module/characterization/CMakeLists.txt` - nearest existing module pattern for characterization-style source targets.
- `src/operation/iCTS/source/flow/htree/CMakeLists.txt` - nearest existing flow target pattern for H-tree flow code.
- `src/operation/iCTS/test/CMakeLists.txt` - test base targets and `icts_add_test_executable()` macro.
- `src/operation/iCTS/test/module/CMakeLists.txt` - module test aggregator; explicitly adds module test subdirectories.
- `src/operation/iCTS/test/flow/CMakeLists.txt` - flow test aggregator; explicitly adds flow test subdirectories.
- `src/operation/iCTS/test/module/characterization/CMakeLists.txt` - existing characterization unit/realtech test registration pattern.
- `src/operation/iCTS/test/flow/htree/CMakeLists.txt` - existing H-tree flow unit/realtech/slow-regression test registration pattern.
- `src/operation/iCTS/test/README.md` - describes test layout, support target conventions, realtech fallback behavior, and artifact output locations.
- `src/operation/iCTS/source/module/characterization/CharBuilder.hh` / `.cc` - existing enumerative characterization entry point and STA/iPA dependency surface.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh` / `.cc` - existing H-tree flow entry point, result shape, composition pipeline, and native comparison target.
- `src/operation/iCTS/source/database/characterization/*.hh` - shared characterization data-model types (`CharCore`, `SegmentChar`, `HTreeTopologyChar`, `PatternId`, etc.).
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.*` - realtech characterization session/config/artifact helper used by existing tests.
- `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.*` - H-tree test artifact helper used by existing flow tests.

### CMake Discovery Is Explicit

- iCTS root CMake adds only four fixed subdirectories: `${ICTS_EXTERNAL_LIBS}`, `${ICTS_SOURCE}`, `${ICTS_API}`, and `${ICTS_TEST}` (`src/operation/iCTS/CMakeLists.txt:39`, `src/operation/iCTS/CMakeLists.txt:44`).
- Source aggregation is also explicit: `${ICTS_DATABASE}`, `${ICTS_UTILS}`, `${ICTS_FLOW}`, and `${ICTS_MODULE}` are added manually (`src/operation/iCTS/source/CMakeLists.txt:1`, `src/operation/iCTS/source/CMakeLists.txt:6`).
- Module aggregation explicitly lists known module paths. `numerical_characterization` is not represented; active modules are routing, timing, topology, and characterization (`src/operation/iCTS/source/module/CMakeLists.txt:5`, `src/operation/iCTS/source/module/CMakeLists.txt:14`).
- Flow aggregation explicitly lists `htree` and `synthesis`; `numerical_htree` is not represented (`src/operation/iCTS/source/flow/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/CMakeLists.txt:4`).
- Tests are explicit too: iCTS test root adds fixed groups (`common`, `database`, `flow`, `module`) (`src/operation/iCTS/test/CMakeLists.txt:50`), then module and flow aggregators list concrete subdirectories (`src/operation/iCTS/test/module/CMakeLists.txt:1`, `src/operation/iCTS/test/flow/CMakeLists.txt:1`).
- Search found no `file(GLOB ...)`, `CONFIGURE_DEPENDS`, or recursive add-subdirectory mechanism under `src/operation/iCTS`; source and test files are enumerated in target CMakeLists. A self-contained `CMakeLists.txt` placed only in a new directory will not be discovered automatically.

Conclusion: parent CMake edits are unavoidable if the new source/test directories must compile in the normal iCTS build. The minimal existing-code edits are CMake-only parent wiring:

- Add `ICTS_MODULE_NUMERICAL_CHARACTERIZATION`, `add_subdirectory(${ICTS_MODULE_NUMERICAL_CHARACTERIZATION})`, and link `icts_source_module_numerical_characterization` from `icts_source_module` in `src/operation/iCTS/source/module/CMakeLists.txt`.
- Add `ICTS_FLOW_NUMERICAL_HTREE`, `add_subdirectory(${ICTS_FLOW_NUMERICAL_HTREE})`, and link `icts_source_flow_numerical_htree` from `icts_source_flow` in `src/operation/iCTS/source/flow/CMakeLists.txt`.
- Add `add_subdirectory(numerical_characterization)` in `src/operation/iCTS/test/module/CMakeLists.txt` if module tests are added.
- Add `add_subdirectory(numerical_htree)` in `src/operation/iCTS/test/flow/CMakeLists.txt` if flow tests are added.
- No iCTS root CMake edit should be needed because `source` and `test` are already added from the root.

### Source Target Patterns

- Existing real source modules use real library targets when they have `.cc` files. Characterization defines `ICTS_MODULE_CHAR_SRC`, adds `icts_source_module_characterization`, and enumerates `CharBuilder.cc` (`src/operation/iCTS/source/module/characterization/CMakeLists.txt:1`, `src/operation/iCTS/source/module/characterization/CMakeLists.txt:5`).
- Existing header-only/source-data targets use `INTERFACE`. The database characterization target is header-only and exposes `${ICTS_DATABASE_CHARACTERIZATION}` plus `${ICTS_DATABASE}` (`src/operation/iCTS/source/database/characterization/CMakeLists.txt:1`, `src/operation/iCTS/source/database/characterization/CMakeLists.txt:8`).
- The module aggregator exposes all source modules through `icts_source_module` and includes `${ICTS_MODULE}` (`src/operation/iCTS/source/module/CMakeLists.txt:19`, `src/operation/iCTS/source/module/CMakeLists.txt:21`, `src/operation/iCTS/source/module/CMakeLists.txt:35`).
- The flow aggregator creates a concrete `icts_source_flow` library for `FlowManager.cc`, links child flow targets, and includes `${ICTS_FLOW}` (`src/operation/iCTS/source/flow/CMakeLists.txt:7`, `src/operation/iCTS/source/flow/CMakeLists.txt:9`, `src/operation/iCTS/source/flow/CMakeLists.txt:17`).
- Existing `icts_source_flow_htree` is a concrete library for `HTreeBuilder.cc`, links database/module/utils targets, and exposes `${ICTS_FLOW}` (`src/operation/iCTS/source/flow/htree/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/htree/CMakeLists.txt:6`, `src/operation/iCTS/source/flow/htree/CMakeLists.txt:14`).

Recommended new target shapes:

- `src/operation/iCTS/source/module/numerical_characterization/CMakeLists.txt`
  - Use `add_library(icts_source_module_numerical_characterization ...)` if there are `.cc` files.
  - Use `add_library(... INTERFACE)` only if the fitting/model code is genuinely header-only.
  - Link `icts_source_database_characterization` `PUBLIC` if public headers expose `SegmentChar`, `HTreeTopologyChar`, `CharCore`, `PatternId`, or related characterization types.
  - Link `icts_source_module_characterization` `PRIVATE` only if implementation code directly invokes `CharBuilder` to produce sparse samples.
  - Link `icts_source_utils_logger` / `icts_source_utils` `PRIVATE` only when logging/schema helpers are used in `.cc` files.
- `src/operation/iCTS/source/flow/numerical_htree/CMakeLists.txt`
  - Use `add_library(icts_source_flow_numerical_htree ...)`.
  - Link `icts_source_module_numerical_characterization` for numerical model inputs.
  - Link existing source targets by target name, not copied include directories.
  - Avoid linking the numerical characterization module to flow targets; flow orchestration should depend on module code, not the reverse.

### Dependency Boundaries

- Backend spec defines `source/module` as algorithm/CTS modules and `source/flow` as orchestration. Dependency direction only explicitly bans Source depending on API, but the local category split still means module code should not depend on flow code (`.trellis/spec/backend/directory-structure.md`).
- `CharBuilder` is the current enumerative characterization boundary. It exposes `init()`, `build()`, and accessors for segment chars, patterns, wire-length/slew/cap grids, and overflow metrics (`src/operation/iCTS/source/module/characterization/CharBuilder.hh:68`, `src/operation/iCTS/source/module/characterization/CharBuilder.hh:72`, `src/operation/iCTS/source/module/characterization/CharBuilder.hh:80`).
- `CharBuilder` implementation directly touches `CONFIG_INST` and `STA_ADAPTER_INST` for config, liberty limits, STA char samples, and iPA power (`src/operation/iCTS/source/module/characterization/CharBuilder.cc:164`, `src/operation/iCTS/source/module/characterization/CharBuilder.cc:189`, `src/operation/iCTS/source/module/characterization/CharBuilder.cc:568`, `src/operation/iCTS/source/module/characterization/CharBuilder.cc:956`, `src/operation/iCTS/source/module/characterization/CharBuilder.cc:1045`).
- `HTreeBuilder` is the current native flow comparison boundary. Its public result carries levels, best char/pattern, candidate/frontier entries, char grid metadata, depth candidates, and materialized CTS objects (`src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:67`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:91`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:96`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:103`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:117`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:123`).
- `HTreeBuilder::build()` calls `TopologyGen::build`, resolves wrapper DBU, builds a `CharBuilder`, runs characterization, synthesizes segment entry sets, evaluates depth candidates, filters actual-load legality, selects a best topology, materializes CTS objects, and logs a summary (`src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2310`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2318`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2363`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2373`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2437`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2456`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2559`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2582`).
- Shared characterization data already lives in `source/database/characterization`. `CharCore` stores input/output slew indices, driven/load cap indices, delay, power, pattern id, and source-boundary net switch power (`src/operation/iCTS/source/database/characterization/CharCore.hh:41`, `src/operation/iCTS/source/database/characterization/CharCore.hh:54`).
- `SegmentChar::compose()` and `HTreeTopologyChar::compose()` encode current additive delay/power semantics and binary H-tree fanout power semantics (`src/operation/iCTS/source/database/characterization/SegmentChar.hh:60`, `src/operation/iCTS/source/database/characterization/SegmentChar.hh:77`, `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh:37`, `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh:83`).

Practical boundary recommendation:

- Keep numerical fitting/model classes in `source/module/numerical_characterization`.
- Accept existing `SegmentChar` or a narrow sample DTO as input rather than reaching into iSTA/iPA from the numerical model layer.
- If sparse sampling must invoke existing `CharBuilder`, put that adapter in the numerical characterization module implementation and link `icts_source_module_characterization` privately.
- Keep H-tree load/topology orchestration and native-vs-numerical comparison in `source/flow/numerical_htree` or in tests, not inside the model-fitting module.
- For ARM9 comparison tests, prefer tests calling both `icts::HTreeBuilder::build(...)` and the numerical flow entry point instead of making production numerical flow depend on native `HTreeBuilder`, unless production fallback/comparison is a first-class API requirement.

### Test Target Registration

- The iCTS test root defines `icts_test_base`, links it to `log`, `icts_source`, `icts_api`, and `icts_test_external_libs`, and builds a shared `icts_test_main` object (`src/operation/iCTS/test/CMakeLists.txt:3`, `src/operation/iCTS/test/CMakeLists.txt:11`, `src/operation/iCTS/test/CMakeLists.txt:29`).
- `icts_add_test_executable(target_name ...)` accepts `REALTECH`, `SOURCES`, and `LIBS`, creates an executable with the shared test main, links `icts_test_base`, `icts_test_common_io`, and user libs, conditionally links `icts_test_realtech_base`, then registers the target with `add_test(NAME target COMMAND target)` (`src/operation/iCTS/test/CMakeLists.txt:34`, `src/operation/iCTS/test/CMakeLists.txt:40`, `src/operation/iCTS/test/CMakeLists.txt:43`, `src/operation/iCTS/test/CMakeLists.txt:47`).
- `REALTECH` does not itself gate build or execution; it only links extra realtech dependencies. Runtime skipping is done inside test code with `GTEST_SKIP()` when assets/env are unavailable (`src/operation/iCTS/test/CMakeLists.txt:43`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:487`).
- Slow realtech regression targets are gated at configure time by `ICTS_BUILD_SLOW_REALTECH_TESTS`, default `OFF` (`src/operation/iCTS/test/CMakeLists.txt:32`, `src/operation/iCTS/test/flow/htree/CMakeLists.txt:30`, `src/operation/iCTS/test/module/characterization/CMakeLists.txt:29`).
- Existing ARM9 full-sink H-tree matrix coverage is inside `icts_test_flow_htree_realtech` and additionally requires env var `ICTS_RUN_ARM9_HTREE_MATRIX=1`; otherwise the test skips (`src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:82`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:83`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:480`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:482`).
- Existing ARM9 matrix test records runtime, success, frontier count, selected depth, best pattern id, delay, power, char grid metadata, fallback status, and failure reason into `matrix_report.txt` via `WriteScenarioLog()` (`src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:95`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:125`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:523`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:562`).

Recommended test wiring:

- Add `src/operation/iCTS/test/module/numerical_characterization/CMakeLists.txt` and register a focused target such as `icts_test_module_numerical_characterization`.
- Add `src/operation/iCTS/test/flow/numerical_htree/CMakeLists.txt` and register a focused target such as `icts_test_flow_numerical_htree`.
- For ARM9 full-design/full-sink comparison, use `icts_add_test_executable(... REALTECH ...)` with `ICTS_RUN_ARM9_NUMERICAL_HTREE_COMPARISON=1` style runtime skip, or place the expensive target behind `ICTS_BUILD_SLOW_REALTECH_TESTS` if it should not build by default.
- Link comparison tests to `icts_test_common_realtech_support`, `icts_test_module_characterization_support`, `icts_test_common_logging_support`, and artifact helpers as needed, following the existing H-tree target's `LIBS` list (`src/operation/iCTS/test/flow/htree/CMakeLists.txt:15`).

### Test Artifacts and RealTech Helpers

- Per-test artifacts are created by the shared gtest listener under `ICTS_TEST_OUTPUT_DIR` or an executable-adjacent `icts_test_output` default (`src/operation/iCTS/test/main.cc:42`, `src/operation/iCTS/test/main.cc:44`, `src/operation/iCTS/test/main.cc:50`, `src/operation/iCTS/test/common/io/TestArtifactIO.cc:304`).
- `test/README.md` documents output roots and guaranteed per-test `cts.log` / `test.log` files (`src/operation/iCTS/test/README.md:53`, `src/operation/iCTS/test/README.md:59`, `src/operation/iCTS/test/README.md:63`).
- `WriteTextLog()` writes a standalone artifact and mirrors key/value summaries into the colocated `cts.log`; it should be used for inspectable comparison reports (`src/operation/iCTS/test/common/io/TestArtifactIO.cc:246`, `src/operation/iCTS/test/common/io/TestArtifactIO.cc:251`).
- Existing H-tree artifact paths live under `ResolveOutputDir()/flow/htree/<case>` and include `cts.log`, topology SVG, materialized SVG, pareto SVG, and `report.log` (`src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.hh:38`, `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc:958`, `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc:967`).
- Existing realtech characterization sessions capture/restore global config, apply reduced characterization settings, create scenario output dirs under `characterization/realtech/<scenario>`, and restore STA adapter state on teardown (`src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:312`, `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:372`, `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:396`, `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:413`).

### Integration Constraints Summary

- New self-contained CMakeLists in the requested source directories will not compile unless parent `add_subdirectory()` entries are added.
- New source `.cc` files must be enumerated in their local CMake target; no automatic source globbing exists in iCTS.
- CMake-only edits to the nearest parent aggregators are the minimum unavoidable existing-code changes.
- For normal production visibility, link new source targets into the nearest aggregator (`icts_source_module`, `icts_source_flow`) per the backend directory-structure spec.
- Tests also require parent test aggregator edits; a new test directory alone is not discovered.
- Use `icts_add_test_executable()` for all new iCTS tests; do not hand-roll `add_executable()` / `add_test()` unless the macro cannot express the target.
- Use direct target links and correct `PUBLIC` vs `PRIVATE` visibility instead of copied include directories. Existing CMake sometimes uses `PRIVATE` even when public headers include dependent types, but the spec says `PUBLIC` when a dependency appears in public headers.
- Do not modify existing C++ code just to compare native and numerical flows; tests can call existing public flow/model boundaries directly.

### Related Specs

- `.trellis/spec/project-constraints.md` - iCTS file naming, no-exception policy, required headers, no default `ecc_dev_tools` loop, CMake-before-implementation rule.
- `.trellis/spec/backend/directory-structure.md` - source category ownership, target naming, parent `add_subdirectory()`, aggregator linking, test mirroring.
- `.trellis/spec/backend/quality-guidelines.md` - include visibility, dependency visibility, no relative include traversal, target-link preference.
- `.trellis/spec/backend/database-guidelines.md` - singleton boundaries, external-adapter containment, ownership of CTS data.
- `.trellis/spec/backend/logging-guidelines.md` - `LOG_*` and schema/report helper expectations.
- `.trellis/spec/backend/error-handling.md` - no exceptions, safe defaults, warning/error/fatal choices.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - relevant because the task crosses module, flow, test, Wrapper, and STAAdapter boundaries.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - relevant because the task adds CMake targets/helpers and should reuse existing test artifact and realtech support targets.

### External References

- No external references were needed for this internal CMake/test integration research.
- The top-level project declares CMake minimum version 3.11 (`CMakeLists.txt:18`) and iCTS tests use repository-provided GTest linkage through `icts_test_external_libs` (`src/operation/iCTS/external_libs/icts_test_external_libs.cmake:1`, `src/operation/iCTS/external_libs/icts_test_external_libs.cmake:3`).

## Caveats / Not Found

- The requested new directories do not currently exist: `src/operation/iCTS/source/module/numerical_characterization`, `src/operation/iCTS/source/flow/numerical_htree`, `src/operation/iCTS/test/module/numerical_characterization`, and `src/operation/iCTS/test/flow/numerical_htree`.
- I did not run CMake configure/build, because this research task was read-only except for writing the research artifact.
- I did not inspect generated build files; conclusions are based on repository CMake source and `rg` searches for glob/auto-discovery patterns.
- No source files were modified.
