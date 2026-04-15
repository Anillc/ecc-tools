# brainstorm: analyze icts runtime distribution

## Goal

Add runtime distribution instrumentation around iCTS timing characterization and H-tree build, then use an existing executable path to summarize concrete runtime breakdown and bottlenecks.

The current implementation goal is to replace the characterization-time external-module reuse path with a full B-scheme sandbox design:

* Reuse the real-tech/full-design initialization that already happened before `flow/htree`.
* Keep the original full-design iSTA/iPA state alive and avoid clearing/rebuilding it for characterization.
* Run characterization on an isolated CTS-only timing/power sandbox context that reuses already loaded technology/liberty data.
* Clean the sandbox temporary objects after characterization so the original full-design STA state is restored without rebuild.

## What I already know

* User wants analysis focused on iCTS timing characterization and H-tree construction runtime.
* `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` is the end-to-end H-tree synthesis entry and currently exposes only aggregate synthesis info plus characterization-grid metadata.
* `src/operation/iCTS/source/module/characterization/CharBuilder.hh` already records per-wire-length runtime and sample counts in `WireLengthBuildStat`.
* `src/operation/iCTS/test/module/characterization/CharacterizationRealTechSmokeTest.cc` already emits characterization reports with runtime and wire-length stats.
* `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` is the most direct existing real-tech executable for H-tree build validation.
* `src/operation/iCTS/api/CTSAPI.cc` only reports coarse flow/read-data elapsed time and does not currently exercise a full H-tree flow.

## Assumptions (temporary)

* The most efficient first slice is to instrument `HTreeBuilder::build()` and reuse the existing real-tech smoke tests for runtime collection.
* The user mainly needs phase distribution and bottleneck identification for the characterization plus H-tree path, not a full-chip production CTS flow profile.
* Real-tech assets or the repo's synthetic fallback paths are available well enough to run at least one profiling-oriented test path locally.

## Open Questions

* Should the first deliverable target the existing real-tech test path (`icts_test_module_characterization_realtech` + `icts_test_flow_htree_realtech`) or a broader API/full-flow entrypoint?

## Requirements (evolving)

* Add runtime breakdown statistics for H-tree build stages.
* Preserve existing iCTS logging/error-handling conventions.
* Make the stats inspectable from an existing test or report path.
* Produce a concrete runtime-distribution summary and identify the dominant blocking stages.
* Treat all runtime-analysis instrumentation as temporary probing only.
* Before `$finish-work`, explicitly check and remove the temporary runtime probes so they do not remain in the long-term production path.
* Avoid invasive production-code changes; prefer wiring the temporary statistics through existing test/report paths.
* Optimize the external `iPA` and `iSTA` runtime paths without changing characterization or H-tree outputs.
* Prefer non-invasive changes to external-module operators: keep existing operator behavior intact and only add the minimum new interfaces needed for reuse or incremental execution.
* During development, `iCTS` must introduce new interfaces first and keep the old interfaces alive in parallel.
* Add dedicated old/new comparison tests. The only allowed behavioral difference during development is which `iPA`/`iSTA` interface path is exercised; all other outputs must remain identical.
* Do not remove the old interfaces, temporary compatibility code, or transition naming until the human explicitly accepts the new path as a replacement.
* After human acceptance, perform one cleanup pass to remove the old interfaces and redundant code, then normalize the new-interface naming.
* New and old test outputs must remain exactly identical throughout development.
* Skip `ecc_dev_tools` during the development iterations for this task. Final validation will be driven by human acceptance first; project-wide cleanup and formal checks happen only after the replacement is approved.
* Treat the currently staged repository state as the baseline control version for this phase of work.
* Before any sandbox changes, capture baseline outputs/artifacts from the staged version and compare the new implementation against that baseline.
* New sandbox code must preserve characterization and H-tree observable results exactly relative to the staged baseline.
* Prefer the minimum visual intrusion in external modules:
  * keep legacy logic flow and semantics unchanged unless an additive interface is strictly required
  * add new CTS-only code under explicit `icts_char`-style names or similarly isolated scopes
  * when practical, place additive helper code near the bottom of touched external files instead of front-loading large blocks at file top
  * do not run formatting-only edits, whitespace cleanups, or `ecc_dev_tools` checks on external-module changes during development
* The B-scheme target is an isolated characterization sandbox, not a shared in-place overlay:
  * isolate char-only graph/context from the full-design graph/context
  * isolate char-only temporary clocks and runtime state
  * allow additive interfaces in iSTA/iPA to support the sandbox, but avoid changing legacy operator semantics

## Acceptance Criteria (evolving)

* [ ] H-tree build reports phase-level runtime distribution with stable field names.
* [ ] Characterization runtime data and H-tree runtime data can be read together for one reproducible scenario.
* [ ] At least one executable path is run locally to collect runtime evidence.
* [ ] Final summary identifies dominant runtime contributors and likely bottlenecks.
* [ ] A new `iPA` optimization path exists beside the legacy path and is selectable from `iCTS`.
* [ ] A new `iSTA` optimization path exists beside the legacy path and is selectable from `iCTS`.
* [ ] Dedicated comparison tests exercise old/new `iPA` and `iSTA` paths and prove exact result parity.
* [ ] Legacy interfaces and compatibility code remain in place until the human accepts the replacement path.
* [ ] Cleanup, old-path removal, and naming normalization are deferred until explicit follow-up approval.
* [ ] Current staged-version artifacts are captured as the parity baseline before the B-scheme implementation changes behavior.
* [ ] A CTS-only iSTA sandbox context exists and can run characterization without destroying/rebuilding the full-design STA engine state.
* [ ] A CTS-only iPA sandbox context exists and can run characterization power evaluation without mutating the full-design power graph state.
* [ ] The sandbox implementation is visually isolated in external modules via additive CTS-only interfaces/scopes rather than broad edits to legacy logic.
* [ ] Existing real-tech characterization/HTree tests produce exactly matching outputs relative to the staged baseline.

## Definition of Done (team quality bar)

* Tests added or updated where appropriate
* Lint and path-scoped checks pass
* Runtime report output is human-readable
* Analysis notes distinguish measurement evidence from inference

## Out of Scope (explicit)

* Full production CTS flow optimization beyond instrumentation and analysis
* Broad API redesign
* Non-H-tree routing/runtime analysis outside the requested characterization path

## Technical Notes

* Relevant flow entry: `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`
* Relevant characterization entry: `src/operation/iCTS/source/module/characterization/CharBuilder.cc`
* Existing real-tech report helpers: `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.hh`
* Existing artifact/log helpers: `src/operation/iCTS/test/common/io/TestArtifactIO.hh`
* Current runtime analysis is a temporary instrumentation task. The added probes must be reviewed and removed before invoking `$finish-work`.
* External-module blocking analysis should focus on `CharBuilder -> STAAdapter -> iSTA/iPA`, especially repeated timing propagation and any per-sample power-context rebuilds.
* Delivery is phased:
  * Phase 1: keep the legacy path unchanged, add new optimized interfaces in parallel, and add old/new parity tests.
  * Phase 2: review results with the human and wait for explicit acceptance.
  * Phase 3: only after acceptance, remove legacy interfaces, delete redundant transition code, normalize naming, and run the final cleanup/quality pass.
* B-scheme implementation notes:
  * The new target architecture is a detached CTS characterization sandbox rather than rebuilding the global STA singleton.
  * The sandbox should reuse already loaded liberty/technology data whenever possible.
  * Full-design initialization that happened before `flow/htree` remains valid and should not be torn down for characterization.
  * Baseline comparison must use the currently staged version of the repository, not HEAD~1 or an older historical snapshot.
  * External-module file hygiene matters: avoid formatting noise, empty-line cleanup, or broad visual churn in iSTA/iPA files.
