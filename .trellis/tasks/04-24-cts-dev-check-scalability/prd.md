# brainstorm: optimize CTS dev checks

## Goal

Analyze and optimize the iCTS code and build/check structure so `ecc_dev_tools` can complete reliably in a reasonable time without changing CTS behavior. The immediate pain point is that several single translation units are too large for `clang-tidy` to finish promptly, and many test/source targets are linked through heavy aggregator targets, causing broad dependency exposure during checks.

## What I Already Know

* The user wants a task created first, then an analysis of CTS/iCTS code where single files and heavy targets make `ecc_dev_tools` unable to finish in time.
* The optimization must preserve functionality and should avoid behavior changes.
* The user has confirmed that the refactor scope should cover all overlarge CTS/iCTS files, not only newly added numerical files.
* The refactor style must fit the EDA/CTS business domain: split along real algorithm/flow responsibilities, avoid artificial abstraction layers, and keep code logic unchanged.
* Recent no-timeout full check was stopped manually after roughly two hours because four `clang-tidy` processes kept running at close to 100% CPU.
* Confirmed slow `clang-tidy` translation units from the no-timeout run:
  * `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`
  * `src/operation/iCTS/source/flow/numerical_htree/NumericalHTreeBuilder.cc`
  * `src/operation/iCTS/source/module/characterization/CharBuilder.cc`
  * `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc`
* Top large iCTS files by line count:
  * `HTreeBuilder.cc`: 2658 lines
  * `BoundSkewTree.cc`: 2296 lines
  * `LinearClusteringRealTechExperimentScenario.cc`: 1840 lines
  * `STAAdapter.cc`: 1571 lines
  * `CharBuilder.cc`: 1178 lines
  * `HTreeVisualizationSupport.cc`: 1003 lines
  * `NumericalHTreeBuilder.cc`: 998 lines
  * `ClockSynthesisRealTechSmokeTest.cc`: 811 lines
* CMake file-api scan shows many test targets have high dependency fan-in because `icts_test_base` links `icts_source` and `icts_api`, and `icts_source` links broad aggregators such as `icts_source_database`, `icts_source_flow`, and `icts_source_module`.
* Several single-source tests still carry many transitive dependencies. Examples from the current build metadata:
  * `icts_test_flow_synthesis_realtech`: 2 sources, 17 dependencies
  * `icts_test_flow_numerical_htree_arm9_comparison`: 2 sources, 17 dependencies
  * `icts_test_flow_htree_realtech`: 2 sources, 20 dependencies
  * `icts_test_module_topology_linear_clustering_realtech`: 1 source, 23 dependencies
  * `icts_test_common_realtech_support`: 1 source, 13 dependencies
* The target model encourages broad compile/check scope because test executables always link `icts_test_base`, and `icts_test_base` always exposes `icts_source` and `icts_api`.

## Assumptions

* The main blocker is `clang-tidy` cost over large iCTS translation units and broad transitive compile commands, not runtime tests.
* We should optimize source structure and target dependencies before relying on permanently raising checker timeouts.
* Low-risk changes should be preferred: move cohesive helper types/functions into smaller `.cc` files, narrow target dependencies, and split realtech support code where possible.
* Behavior-preserving refactors should keep public APIs stable unless a narrow API extraction is required.
* "Code量过多" will be treated as either a known checker-blocking TU or a source/test file above roughly 800 lines, then prioritized by checker impact and business risk.

## Requirements

* Identify the iCTS source/test translation units that dominate `ecc_dev_tools` runtime.
* Identify heavy CMake target dependency paths that cause tests to inherit unnecessary modules.
* Propose and implement behavior-preserving decomposition of all overlarge offenders, prioritized by checker blockage and CTS business criticality.
* Split files by CTS/EDA responsibilities rather than generic utility buckets. Examples:
  * H-tree topology/level planning
  * segment frontier synthesis
  * actual-load legality and cap coverage
  * materialization of inserted insts/nets
  * characterization configuration and buffer discovery
  * characterization topology enumeration
  * iSTA/iPA sampling and power collection
  * realtech setup/report/visualization support
* Keep iCTS layer boundaries:
  * `flow/` for orchestration
  * `module/` for reusable algorithms
  * `database/` for DB/config/adapter/shared data
  * `test/` mirroring source where practical
* Preserve existing public behavior and test semantics.
* Avoid over-design:
  * no new framework-style abstractions unless they remove real duplication
  * no large class hierarchy for simple helper movement
  * no behavioral rewrites hidden inside refactor commits
  * no broad formatting churn outside moved code
* Avoid external-module cleanup or broad formatting churn.
* Keep final validation meaningful even if some native large files remain slow during transition.
* After pulling fast-clustering changes, split newly introduced iCTS files above the overlarge threshold without changing clustering semantics:
  * `source/module/topology/fast_clustering/FastClustering.cc`
  * `test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkTest.cc`
* Do not run asset-dependent fast-clustering real-tech benchmark tests; the required runtime assets are not available in this environment. Validation for that path is compile/link only.
* Do not use `NOLINTNEXTLINE` or similar local checker bypasses as a substitute for fixing in-scope `ecc_dev_tools` findings.

## Acceptance Criteria

* [x] A baseline report identifies the slowest `ecc_dev_tools` translation units and heavy CMake dependency paths.
* [x] All known checker-blocking TUs are either decomposed or have a documented reason for being deferred.
* [x] All iCTS files above the agreed overlarge threshold are triaged, with an implementation plan or explicit defer decision.
* [x] The first optimization pass reduces at least the known slow TU pressure and documents any remaining native slow files.
* [x] CMake target dependencies are narrowed where safe, especially test targets that do not need all of `icts_source` / `icts_api`.
* [x] Focused build and tests pass for touched modules.
* [x] A full `src/operation/iCTS` check is attempted after changes.
* [x] Full check convergence status is documented; if it had not converged, remaining blocking TUs and process state would be recorded.
* [x] Newly pulled fast-clustering source/test files above 800 lines are split by EDA/CTS responsibility.
* [x] Fast-clustering benchmark path compiles without requiring missing runtime assets.
* [x] No new `NOLINTNEXTLINE` checker bypasses are introduced.

## Definition of Done

* Task research and baseline metrics are persisted under this task directory.
* Implementation is behavior-preserving and scoped to iCTS/test/check tooling as agreed.
* Refactor boundaries are named after CTS responsibilities, not generic "helper" or "manager" abstractions.
* Focused build/tests cover touched source modules.
* Final checker result or non-convergence evidence is recorded.
* Any changes to `.trellis/ecc_dev_tools` timeout/check behavior are explicitly called out.

## Out of Scope

* Changing CTS algorithms or QoR behavior.
* Rewriting H-tree, characterization, routing, or synthesis logic wholesale.
* Editing external modules such as iSTA, iPA, or iDB to satisfy iCTS checker findings.
* Hiding real findings by blanket suppressions without clear justification.
* Making checker timeout increases the primary fix.
* Introducing new optimization algorithms or QoR changes while splitting files.

## Technical Notes

### Relevant Specs

* `.trellis/spec/project-constraints.md`
* `.trellis/spec/backend/directory-structure.md`
* `.trellis/spec/backend/quality-guidelines.md`
* `.trellis/spec/guides/code-reuse-thinking-guide.md`
* `.trellis/spec/guides/cross-layer-thinking-guide.md`

### Initial File-Size Findings

Command used:

```bash
rg --files src/operation/iCTS | rg '\\.(cc|hh)$' | xargs wc -l | sort -nr | sed -n '1,40p'
```

Initial high-risk files:

* `source/flow/htree/HTreeBuilder.cc`
* `source/module/routing/bound_skew_tree/BoundSkewTree.cc`
* `source/database/adapter/sta/STAAdapter.cc`
* `source/module/characterization/CharBuilder.cc`
* `source/flow/numerical_htree/NumericalHTreeBuilder.cc`
* `test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc`

### Initial Target-Weight Findings

The target graph scan used CMake file-api replies under `build/.cmake/api/v1/reply`.

Key target pattern:

```text
icts_test_base -> icts_source + icts_api + icts_test_external_libs
icts_source -> icts_source_database + icts_source_flow + icts_source_module + icts_source_utils
```

This means even narrow tests can inherit a broad compile/check dependency context.

## Feasible Approaches

### Approach A: Source/TU Decomposition First

Split large `.cc` files by internal responsibilities while preserving public headers and target names.

Likely first cuts:

* `NumericalHTreeBuilder.cc`
  * response-surface evaluation
  * model conversion from numerical characterization
  * level scoring/search
  * load/topology adapter
* `HTreeBuilder.cc`
  * depth candidate resolution and level planning
  * segment frontier synthesis
  * actual-load legality
  * materialization
  * reporting/logging helpers
* `CharBuilder.cc`
  * buffer discovery/config resolution
  * topology enumeration
  * iSTA/iPA sample loop
  * sample storage/overflow diagnostics

Pros:

* Directly addresses `clang-tidy` slow TUs.
* Low behavioral risk when helpers remain in the same namespace and public API is unchanged.

Cons:

* Can be mechanical but still touches sensitive production flow files.
* Needs careful review to avoid moving static/internal helpers into headers unnecessarily.

### Approach B: Target Graph Narrowing First

Reduce broad target fan-in by replacing `icts_test_base` or aggregator dependencies in selected tests with narrower libraries.

Likely targets:

* Split test base into a minimal gtest/log base and opt-in source/api base.
* Let pure module tests link only the module libraries they use.
* Keep realtech tests on heavier base where needed.

Pros:

* Reduces compile/check pressure across many tests.
* Can improve normal build iteration as well.

Cons:

* Requires careful CMake graph validation.
* May expose missing target-level dependencies that broad aggregators currently hide.

### Approach C: Checker Scheduling/Timeout Policy

Keep source mostly unchanged, but adjust checker behavior for known pathological TUs.

Possible tactics:

* Use targeted skip/suppression for known non-convergent `clang-tidy` TUs.
* Keep analyzer or compiler fallback for those TUs.
* Separate "fast required" and "deep optional" presets.

Pros:

* Fastest route to usable checks.
* Avoids production refactor risk.

Cons:

* Does not solve root cause.
* Can mask real issues in the most complex files.

## Recommended MVP

Use a hybrid of Approach A and Approach B, expanded to all overlarge CTS/iCTS files:

1. Establish a reproducible baseline report for file size, target fan-in, and slow checker TUs.
2. Decompose checker-blocking production TUs by CTS business boundary:
   * `HTreeBuilder.cc`
   * `NumericalHTreeBuilder.cc`
   * `CharBuilder.cc`
3. Decompose checker-blocking test/support TUs:
   * `ClockSynthesisRealTechSmokeTest.cc`
   * nearby realtech support files if they remain above the threshold or keep heavy dependencies alive
4. Triage other very large files:
   * `BoundSkewTree.cc`
   * `LinearClusteringRealTechExperimentScenario.cc`
   * `STAAdapter.cc`
   * `HTreeVisualizationSupport.cc`
5. Narrow CMake target dependencies after each split, not before understanding ownership. Keep aggregator targets where they are useful for external compatibility, but avoid forcing narrow tests through them.
6. Validate each phase with focused builds/tests before attempting full `src/operation/iCTS` check.

## Refactor Style Rules

* Keep public behavior stable.
* Prefer "move existing cohesive block into a named file" over rewriting logic.
* Use domain file names, for example:
  * `HTreeLevelPlan.cc`
  * `HTreeSegmentFrontier.cc`
  * `HTreeActualLoad.cc`
  * `HTreeMaterialization.cc`
  * `CharBufferDiscovery.cc`
  * `CharTopologyEnumeration.cc`
  * `CharSampling.cc`
* Keep internal helper structs close to their domain unless multiple files genuinely need them.
* Use small internal headers only when a moved helper must be shared across `.cc` files.
* Do not introduce generic service/facade/manager layers unless there is a concrete dependency or duplication problem.
* Preserve logs, failure reasons, selected pattern semantics, QoR calculation, and test report fields.

## Implementation Plan

### Phase 1: Baseline and Guard Rails

* Produce a baseline artifact under this task directory with:
  * largest iCTS source/test files by line count
  * known no-timeout `clang-tidy` blocking TUs
  * target fan-in and broad aggregator paths
  * focused tests that must remain green after each split
* Keep the baseline mechanical and reproducible.

### Phase 2: Numerical H-tree Split

Split `source/flow/numerical_htree/NumericalHTreeBuilder.cc` first because it is both in-scope from recent work and one of the checker-blocking files.

Expected business slices:

* response-surface evaluation
* model conversion from `NumericalCharLibrary`
* level candidate scoring/search
* topology/load adapter
* result comparison/report helper logic

Validation:

* `icts_test_flow_numerical_htree`
* `icts_test_flow_numerical_htree_arm9_comparison` default skip path

### Phase 3: Native H-tree Split

Split `source/flow/htree/HTreeBuilder.cc` by H-tree flow responsibility.

Expected business slices:

* level length planning and depth candidate resolution
* segment frontier synthesis
* H-tree topology composition and selection helpers
* actual-load legality and boundary coverage
* materialization of inserted insts/nets
* logging/reporting support

Validation:

* `icts_test_flow_htree`
* default non-realtech test path
* realtech smoke only when explicitly enabled and assets are available

### Phase 4: Characterization Split

Split `source/module/characterization/CharBuilder.cc` by characterization workflow.

Expected business slices:

* buffer discovery and config limit resolution
* wire/slew/cap lattice setup
* segment topology enumeration
* char-only circuit construction and parasitic setup
* timing/power sample collection
* sample storage and overflow diagnostics

Validation:

* `icts_test_module_characterization`
* `icts_test_module_numerical_characterization`
* focused characterization realtech tests only when environment supports them

### Phase 5: Test/Realtech Support Split

Split checker-blocking or overlarge test files without changing test assertions.

Expected targets:

* `test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc`
* `test/flow/htree/HTreeVisualizationSupport.cc`
* selected linear-clustering realtech scenario/support files if still above threshold

Validation:

* impacted test binaries should still compile and preserve default skip behavior.

### Phase 6: Target Graph Narrowing

After source responsibilities are clearer, narrow CMake dependencies.

Candidate changes:

* separate minimal test base from source/API-heavy test base
* link pure module tests directly to required source targets
* keep realtech-heavy tests on opt-in heavy base
* avoid PUBLIC dependency visibility unless public headers require it

Validation:

* focused target builds
* CMake visibility check
* IWYU/header self-check on touched modules

### Phase 7: Final Check

Run a final full iCTS check.

If the full check still cannot converge:

* record remaining blocking TUs
* record elapsed time and process state
* distinguish in-scope findings from tool scalability blockers
* propose the next decomposition pass rather than masking the problem with timeout-only changes

Final result on 2026-04-24: after the fast-clustering split follow-up, the default full check converged in `489.608s` with `0`
in-scope findings across `format`, `tidy`, `headers`, `cmake`, and `iwyu`. No remaining blocking translation units were observed. The
only diagnostics left were `3674` out-of-scope findings in external `src/database/...` and related non-iCTS headers triggered by in-scope
iCTS TUs, which are outside this task's scope.

## Open Questions

* None at the current scope level. Implementation should proceed with behavior-preserving decomposition across all overlarge files, prioritizing checker-blocking TUs first.
