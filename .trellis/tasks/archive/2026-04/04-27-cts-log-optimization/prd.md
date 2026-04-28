# CTS Log / Schema Cleanup

## Goal

Simplify the iCTS log/schema architecture so `CTSAPI` is only the external API
boundary and `FlowManager` owns CTS run/report lifecycle logging. Keep the
accepted `cts.log` contract unchanged while removing redundant lifecycle state,
old static flow entry patterns, and unclear schema-writer teardown names.

## Accepted Log Contract

- Keep all structured file output routed through `SCHEMA_WRITER_INST`.
- Keep low-level table assembly near the current data owners:
  `Config`, `STAAdapter`, `Design`, `ClockTreeEvaluator`,
  `CTSStatisticsWriter`, H-tree, characterization, topology, and synthesis
  modules.
- Keep markdown-like hierarchy with content-bearing sections only.
- Do not emit `Notes` titles, prose-only note blocks, or an empty synthesis
  placeholder subsection.
- User-facing field names use business semantics and put units in values or
  shared table headers. Do not use unit-suffixed keys in main `cts.log` tables.
- Use `Macro Sinks`, not older ambiguous sink terminology.
- Do not emit `unique_clock_domains`.
- Use `wirelength` as one token across CTS config, code, tests, log fields, and
  report names.
- Statistics report outputs are exactly:
  - `wirelength.rpt`
  - `cell_stats.rpt`
  - `lib_cell_dist.rpt`
- Normal CTS runs do not generate removed debug/detail report artifacts or
  legacy statistics reports.
- Cluster distance reporting in the main log stays compact: count, min, max,
  mean, and median only.
- Runtime/stage status semantics are unified as `finished`, `failed`, or
  `skipped`; do not duplicate stage `success` or `outcome` fields.
- Do not modify `src/feature/**`.

## Evaluation Contract

Preserve the current STA evaluation semantics:

1. Write CTS clocks back into the full design.
2. Refresh the full-design STA timing context after CTS writeback.
3. Set SDC clocks propagated.
4. Build and install CTS RC trees.
5. Call `updateTiming()`.
6. Call `reportTiming()`.
7. Query timing, latency, skew, and wirelength statistics from STA-owned data.

Evaluation metrics must not invent fallback timing values when STA cannot
answer. Missing STA-owned timing data should be explicit structured status.

## P0: PRD And CTSAPI Schema Cleanup

Implementation content:

- Replace stale planning language with this explicit P0/P1/P2 scope and
  acceptance criteria.
- Reduce `CTSAPI.cc` to API boundary responsibilities:
  init path/config setup, schema file open, reset teardown, feature-summary
  mapping, and delegation to `FlowManager`.
- Keep schema lifecycle calls in `CTSAPI.cc` limited to `SCHEMA_WRITER_INST`
  open/reset behavior.
- Move run/report sectioning, runtime scopes, stage scopes, runtime summary,
  key results emission, report-mode schema rows, and report path resolution out
  of `CTSAPI.cc`.
- Make `CTSAPI::report(save_dir)` delegate to `FlowManager::report(save_dir)`.

Acceptance criteria:

- `CTSAPI.cc` has no direct calls to `SCHEMA_WRITER_INST.beginStage`,
  `beginRuntimeMetric`, `emitSection`, `emitRuntimeSummary`, or
  `emitRuntimeMetricTable`.
- `CTSAPI::runCTS()` and `CTSAPI::report()` are thin delegations to
  `FlowManager`; internal flow steps remain on `FLOW_MANAGER_INST`.
- `CTSAPI::resetAPI()` tears down external CTS state and calls schema reset.
- Feature summary compatibility remains unchanged and does not touch external
  feature files.

## P1: FlowManager Singleton And Lifecycle Ownership

Implementation content:

- Convert `FlowManager` to the established singleton style and expose
  `FLOW_MANAGER_INST`.
- Move hidden run-summary state into the `FlowManager` instance. Reuse
  `CTSFlowRunSummary`; do not add a parallel state struct or reporter class.
- Replace old static `FlowManager` call sites in CTS code/tests with
  `FLOW_MANAGER_INST` where practical.
- Add cohesive instance methods for:
  - `runCTS()`
  - `readData()`
  - `run()`
  - `evaluate()`
  - `report(const std::string&)`
  - `outputSummary()`
  - `outputRunSummary()`
  - `reset()`
- Make `FlowManager` own:
  - top-level run lifecycle sections;
  - major stage scopes;
  - runtime metric scopes;
  - runtime summary emission;
  - key results emission;
  - report-mode lifecycle and summary rows;
  - report root directory resolution.
- Keep business table construction in the existing data owners rather than
  centralizing every schema row in `FlowManager`.

Acceptance criteria:

- iCTS source/tests use `FLOW_MANAGER_INST` for flow operations except method
  definitions.
- No CTS-specific stateful reporter is introduced.
- No public standalone stage handle API is reintroduced.
- `FlowManager` state reset clears both run summary and evaluator summary.
- `report(save_dir)` reuses existing evaluation state when available and
  rebuilds minimum evaluation/statistics state otherwise.

## P2: SchemaWriter Lifecycle Naming And Redundancy Cleanup

Implementation content:

- Add a public `SchemaWriter::reset()` for API teardown semantics.
- Keep `close()` for scoped/nested writer restoration used by tests and any
  nested report redirection.
- Make reset close the active report output, discard suspended report state, and
  clear per-run runtime metric state.
- Migrate CTS/API teardown call sites from close semantics to reset semantics.
- Preserve nested scoped log/test behavior by keeping scoped helpers on
  `close()`.
- Keep stage summary schema fields status-only plus caller-provided fields;
  major elapsed/memory rows belong in the runtime summary.

Acceptance criteria:

- API lifecycle teardown calls `SCHEMA_WRITER_INST.reset()`.
- Scoped/nested test log redirection still restores the outer log destination.
- Runtime metrics do not leak between CTS runs.
- Stage summaries use `status` and do not emit duplicate `success`, `outcome`,
  or elapsed fields in schema output.
- Module internals under `src/operation/iCTS/source/module/**` do not read
  `CONFIG_INST` directly; runtime config reaches modules through explicit
  options built at API/flow/database/test boundaries.

## Verification

During implementation, do not run `.trellis/ecc_dev_tools/check.py`.
Use targeted grep/build/test checks only:

- `rg -n "SCHEMA_WRITER_INST\.(begin|emit|resetRuntime|emitRuntime)" src/operation/iCTS/api/CTSAPI.cc`
- `rg -n "FlowManager::" src/operation/iCTS/api src/operation/iCTS/test/flow src/operation/iCTS/source/flow -g '*.cc' -g '*.hh'`
- Search `src/operation/iCTS`, the ICS55 `cts.log`, and this PRD for removed
  reporter symbols, the public scoped-stage symbol, split spellings of
  `wirelength`, removed cluster-distance artifact names, and removed legacy
  statistics report names.
- `git diff -- src/feature`
- `cmake --build build --target iEDA icts_test_common_logging icts_test_flow_manager icts_test_flow_synthesis -j 8`
- Run `icts_test_common_logging`, `icts_test_flow_manager`, and
  `icts_test_flow_synthesis`.

The final unified `ecc_dev` check is intentionally deferred to the parent
finish/check pass.
