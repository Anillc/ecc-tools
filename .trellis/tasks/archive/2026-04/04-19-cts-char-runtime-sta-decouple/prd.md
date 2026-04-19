# Optimize CTS Characterization Runtime by Decoupling STA DB Conversion

## Goal

Reduce iCTS characterization runtime sensitivity to design size by removing the full-design `convertDBToTimingNetlist()` dependency from normal `STAAdapter` initialization, migrating affected CTS queries to liberty/iDB-backed lookups where possible, resetting STA design/graph state around characterization without losing technology setup, and then cleaning the resulting `STAAdapter` implementation so it is CTS-friendly, cohesive, and free of stale sandbox-era semantics.

## Requirements

- `STAAdapter::init()` must keep liberty / process / SDC setup but must not eagerly convert the full iDB design into the STA netlist/graph.
- Queries that currently rely on a converted STA design for CTS-side metadata must be audited and updated:
  - `queryPinCapacitance()` must no longer depend on a converted STA pin instance.
  - `queryInstType()` and related instance-classification logic must not require the full STA design.
  - Full-design timing queries such as clock-net collection must still work through an explicit or lazy full-design preparation path.
- Characterization must stop using the current STA timing sandbox path.
- Before characterization starts, STA design/graph related runtime data must be reset so characterization runs on a small char-only timing context.
- After characterization finishes, STA design/graph related runtime data must be reset again, while liberty / process / SDC setup remains intact.
- If current iSTA semantics still do not allow safe pre-convert SDC loading, do not keep a misleading partial-SDC hook in `STAAdapter::init()`; remove `ValidateConfiguredSdc()` and keep the behavior explicit.
- `STAAdapter` should be cleaned up after the functional refactor:
  - remove stale sandbox-era naming where the concept no longer exists
  - improve helper factoring so char-only and full-design responsibilities are easier to follow
  - keep naming and structure CTS-friendly, with higher cohesion and lower incidental coupling
- If characterization power still requires existing iPA helper code, keep the diff minimal and scoped. If some prior iSTA/iPA characterization-only additions become unused after the refactor, remove them only when the cleanup is low-risk and clearly isolated.
- Investigate, with agent support, whether common SDC clock-period fields such as
  - `set clk_expect_freq_mhz 100`
  - `set clk_period [expr 1000.0 / $clk_expect_freq_mhz]`
  can affect current iCTS characterization results (`delay`, `power`, `slew`), and include the conclusion in the final report.
- Avoid unnecessary edits in external modules. Do not run `ecc_dev_tools` during the edit loop. Run the full `src/operation/iCTS` check only after targeted validation is complete.

## Acceptance Criteria

- [ ] Running characterization on the same H-tree real-tech characterization target shows comparable runtime behavior across the small and large design inputs described by the user (`iPL_result*` vs `arm9_place.*`), with the prior large-design blow-up removed or materially reduced.
- [ ] iCTS characterization output remains functionally valid:
  - [ ] real-tech characterization tests still pass
  - [ ] CTS / H-tree flows that depend on buffer-limit/liberty queries still pass
  - [ ] linear clustering electrical queries still return sensible pin caps without a converted STA design
    - rerun only if the final cleanup changes paths not already shown equivalent
- [ ] Full-design clock-net discovery and timing update behavior still works when CTS flow requests it.
- [ ] `STAAdapter` no longer contains misleading SDC-init or sandbox-era naming/logic that does not match the final architecture.
- [ ] A final report includes the SDC-field impact investigation result, clearly separating direct char impact from full-design timing impact.
- [ ] No in-scope findings remain after the final full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` pass.

## Research Summary

### Relevant Specs

- `.trellis/spec/project-constraints.md`
  Repository-wide iCTS constraints, including external-module diff minimization and final full-module `ecc_dev_tools` validation.
- `.trellis/spec/backend/directory-structure.md`
  Confirms the work belongs in `source/database/adapter/sta/`, `source/module/characterization/`, and mirrored tests.
- `.trellis/spec/backend/quality-guidelines.md`
  Governs naming, include discipline, and the final validation boundary.
- `.trellis/spec/backend/database-guidelines.md`
  Important because the change crosses `Wrapper`, `Design`, `STAAdapter`, and module code; external-tool access must stay inside the adapter layer.
- `.trellis/spec/backend/error-handling.md`
  Needed for degraded lookup behavior and safe fallback returns in query paths.
- `.trellis/spec/guides/cross-layer-thinking-guide.md`
  Applies because the change crosses iDB -> Wrapper/Design -> STAAdapter -> characterization / clustering modules.
- `.trellis/spec/guides/code-reuse-thinking-guide.md`
  Applies because repeated liberty / iDB lookup logic should live in one adapter helper instead of spreading through callers.

### Code Patterns Found

- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
  Current home of lazy full-design preparation, liberty-cell queries, characterization runtime orchestration, and the remaining cleanup target for naming / helper factoring.
- `src/operation/iCTS/source/module/characterization/CharBuilder.cc`
  Characterization lifecycle currently depends on `initCharOnly()`, explicit char clock setup, and per-topology char-state reset.
- `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc`
  Pin-cap query is the main non-characterization CTS path currently depending on a converted STA pin instance.
- `src/operation/iCTS/source/database/io/Wrapper.cc`
  Instance typing currently calls `STAAdapter::queryInstType()`, which currently tries STA netlist first and then falls back to iDB heuristics.
- `src/operation/iSTA/api/TimingEngine.cc`
  Existing `prepareCharTiming()` / `updateCharTiming()` already support char-only propagation on the main graph, which may allow sandbox removal with minimal extra iSTA changes.
- `scripts/design/ics55_dev/default.sdc`
  Representative SDC content used to verify whether `$clk_expect_freq_mhz` / `$clk_period` can affect char or only full-design timing paths.

### Files Likely To Modify

- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`
  Add/refine adapter state and helper declarations for lazy full-design preparation and char-state reset.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
  Main refactor target: remove eager convert-on-init, move queries to liberty/iDB, replace sandbox-based char flow with direct STA reset/build flow.
- `src/operation/iCTS/source/module/characterization/CharBuilder.cc`
  Adjust lifecycle usage if char reset / sample preparation semantics change.
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc`
  Update real-tech char session assumptions if char-only init / restore behavior changes.
- `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc`
  Verify setup still works with lazy full-design STA preparation.
- `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc`
  Validate degraded-mode / lookup assumptions if pin-cap query behavior changes.

## Technical Notes

- Current eager full-design conversion lives in `STAAdapter::init()`:
- The current refactor already moved full-design timing preparation behind an explicit/lazy path rather than doing it in `STAAdapter::init()`.
- Current characterization runtime now uses the main STA engine with explicit graph/design reset around char-only setup, rather than the old iSTA sandbox path.
- iSTA also already exposes `TimingEngine::prepareCharTiming()` and `TimingEngine::updateCharTiming()` on the main graph, which is a likely migration target for sandbox removal.
- `queryPinCapacitance()` is currently implemented through full-design STA netlist pin lookup. This is the key path to replace with liberty-cell / port lookup from the CTS pin and owning inst metadata.
- Sequential-cell input-pin cap is the motivating case, but the replacement must work for generic cell input / inout pins too. Output pins should not be accidentally counted as load capacitance.
- The final implementation should prefer keeping all iDB/iSTA boundary logic inside `STAAdapter`, not in `ConstraintEvaluator` or `CharBuilder`.
- Current iSTA `readSdc()` semantics execute the SDC immediately against the currently available STA design objects. If there is no converted STA netlist yet, a true "read now, bind later" behavior is not available without changing iSTA semantics.

## Final Validation Plan

1. Finish `STAAdapter` cleanup and remove any misleading `ValidateConfiguredSdc()` path if true pre-convert SDC loading is not implemented.
2. Rebuild the affected iCTS targets.
3. Re-run the corrected new-vs-old real-tech comparisons for `small` and `large` characterization / H-tree smoke / clustered synthesis smoke.
4. Reconfirm result consistency and runtime improvement.
5. Re-run only the necessary linear-clustering validation if the final cleanup touches equivalent paths materially.
6. Run the final full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` pass.
7. Report the code changes, runtime/quality comparison, and the SDC-field impact investigation together.

## Out Of Scope

- Broad refactors of unrelated iSTA or iPA code paths.
- Unrelated formatting / checker cleanup in external modules.
- Reworking CTS algorithms unrelated to the STA-conversion / characterization-runtime issue.
