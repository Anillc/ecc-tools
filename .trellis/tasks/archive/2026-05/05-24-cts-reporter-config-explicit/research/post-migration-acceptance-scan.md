# Research: post-migration acceptance scan

- Query: scan the current dirty tree under `src/operation/iCTS/source`, `src/operation/iCTS/api`, and `src/operation/iCTS/test` for remaining singleton-migration acceptance blockers after the CTS desingleton refactor.
- Scope: internal
- Date: 2026-05-24

## Findings

### Scan Basis

The scan is a point-in-time textual scan taken at `2026-05-24 16:24:57 +0800`. The main implementation agent may have edited the dirty tree before, during, or after these commands.

Task discovery returned no session-active task:

```bash
python3 ./.trellis/scripts/task.py current --source
# Current task: (none)
# Source: none
```

Because the prompt supplied the explicit task/output path, this note was written under `.trellis/tasks/05-24-cts-reporter-config-explicit/research/`.

Primary commands used:

```bash
date '+%Y-%m-%d %H:%M:%S %z'
rg -n '\b[A-Z][A-Z0-9_]*_INST\b' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n '\b[A-Za-z_][A-Za-z0-9_:<>]*::getInst\b|\bgetInst\s*\(' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n 'CONFIG_INST|SCHEMA_WRITER_INST|DESIGN_INST|WRAPPER_INST|FAST_STA_INST|STA_ADAPTER_INST|Config::getInst|SchemaWriter::getInst|Design::getInst|Wrapper::getInst|FastSTA::getInst|STAAdapter::getInst' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n 'schema::Emit(Table|KeyValueTable|Diagnostic|Artifact)\s*\(' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n 'schema::Emit(Table|KeyValueTable|Diagnostic|Artifact)\s*\(\s*"|schema::EmitDiagnostic\s*\(\s*icts::schema::DiagnosticLevel' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n 'ClockDataRead::read|\.read\s*\(\s*\)|emitRuntimeConfigReport\s*\(|schema::Emit(Table|KeyValueTable|Diagnostic|Artifact)\s*\(|Wrapper::(read|readClocks|writeClocksDetailed|writeClocks)\b|\.(readClocks|writeClocksDetailed|writeClocks)\s*\(|SdcClockReader::(readClockData|readDeclarationsOnly)|\.(readClockData|readDeclarationsOnly)\s*\(|STAAdapter::(init|refreshFullDesignTimingContext|queryConfiguredClockRouteSegmentRc|queryClockSourceDriveCapLimit|queryPinSlewLimit|installClockNetRcTree|emitConfiguredUnitWireRcReport)\s*\(|FastSTA::getInst\s*\(' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n 'STAAdapter::(init|refreshFullDesignTimingContext|queryConfiguredClockRouteSegmentRc|queryClockSourceDriveCapLimit|queryPinSlewLimit|installClockNetRcTree|emitConfiguredUnitWireRcReport)\s*\(' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n 'FastSTA::getInst\s*\(|FastSTA::' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
rg -n 'CTS_API_INST|CONFIG_INST|SCHEMA_WRITER_INST|DESIGN_INST|WRAPPER_INST|STA_ADAPTER_INST|FAST_STA_INST|getInst|singleton|SchemaWriter|Config&|runtime-owned|current reporter|writer singleton' .trellis/spec .trellis/tasks/05-24-cts-reporter-config-explicit/prd.md .trellis/tasks/05-24-cts-reporter-config-explicit/design.md .trellis/tasks/05-24-cts-reporter-config-explicit/implement.md
```

### Acceptance Summary

Direct `*_INST` macro scan:

| Classification | Pattern | Lines |
| --- | --- | --- |
| allowed | `CTS_API_INST` | `src/operation/iCTS/api/CTSAPI.hh:34` |
| blocker | any other `*_INST` macro | not found |

Direct `getInst` scan:

| Classification | Lines |
| --- | --- |
| allowed CTS API | `src/operation/iCTS/api/CTSAPI.hh:34`, `src/operation/iCTS/api/CTSAPI.hh:42`, `src/operation/iCTS/api/CTSAPI.cc:81`, `src/operation/iCTS/api/CTSAPI.cc:86`, `src/operation/iCTS/api/CTSAPI.cc:91`, `src/operation/iCTS/api/CTSAPI.cc:99`, `src/operation/iCTS/api/CTSAPI.cc:113` |
| blocker internal `getInst` | not found |

The internal singleton roots named in the prompt are gone from the scanned directories: `CONFIG_INST`, `SCHEMA_WRITER_INST`, `DESIGN_INST`, `WRAPPER_INST`, `FAST_STA_INST`, `STA_ADAPTER_INST`, `Config::getInst`, `SchemaWriter::getInst`, `Design::getInst`, `Wrapper::getInst`, `FastSTA::getInst`, and `STAAdapter::getInst` had no matches.

### Files Found

| File | Description |
| --- | --- |
| `src/operation/iCTS/api/CTSAPI.hh:34`, `src/operation/iCTS/api/CTSAPI.hh:42` | Allowed external API singleton macro and API `getInst()` root. |
| `src/operation/iCTS/api/CTSAPI.cc:81`, `:86`, `:91`, `:99`, `:113` | Allowed API wrapper functions dispatch through `CTSAPI::getInst()`. |
| `src/operation/iCTS/source/flow/CTSRuntime.hh:38-43` | Runtime-owned `Config`, `Design`, `Wrapper`, `STAAdapter`, `FastSTA`, and `schema::SchemaWriter`; this is the desired source owner pattern. |
| `src/operation/iCTS/source/flow/Flow.cc:156`, `:182`, `:201`, `:222`, `:251` | Flow now passes runtime-owned dependencies explicitly to read-data, synthesis, optimization, instantiation, and report paths. |
| `src/operation/iCTS/source/utils/logger/Schema.hh:209-213` | `schema::EmitTable`, `EmitKeyValueTable`, `EmitDiagnostic`, and `EmitArtifact` declarations all require `SchemaWriter&`; no no-writer overload declaration remains. |
| `src/operation/iCTS/source/utils/logger/Schema.cc:483`, `:490`, `:497`, `:515` | Helper definitions all require `SchemaWriter&`. |
| `src/operation/iCTS/source/utils/logger/Schema.cc:476-480` | `appendStandaloneBlock` uses a file-local static mutex, not a writer singleton. |
| `src/operation/iCTS/source/flow/setup/clock_data/ClockDataRead.hh:40`, `ClockDataRead.cc:46` | `ClockDataRead::read` requires `const Config&`, `Design&`, `Wrapper&`, and `schema::SchemaWriter&`; no no-arg bridge remains. |
| `src/operation/iCTS/source/database/config/Config.hh:156`, `Config.cc:316` | `Config::emitRuntimeConfigReport` requires `schema::SchemaWriter&`; no title-only overload remains. |
| `src/operation/iCTS/source/database/io/Wrapper.hh:116-124` | Public `Wrapper::read`, `readClocks`, `writeClocksDetailed`, and `writeClocks` all require explicit `Design&` and `schema::SchemaWriter&`. |
| `src/operation/iCTS/source/database/adapter/sdc/SdcClockReader.hh:137-138`, `SdcClockReader.cc:61`, `:89` | `SdcClockReader::readClockData` and `readDeclarationsOnly` require `schema::SchemaWriter&`; no no-writer SDC reader call was found. |
| `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:99-131` | Config-bearing STA adapter methods are declared with `const Config&` and/or explicit reporter where required. |
| `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh:219-258` | `FastSTA` query/context APIs are instance methods, not static singleton methods. |
| `src/operation/iCTS/test/common/CTSTestRuntime.cc:28-31` | Test helper returns a process-wide static `CTSRuntime`; this is not a `*_INST`/`getInst` match but is still singleton-shaped test state. |

### Remaining Blockers

#### No-writer `schema::Emit*` test calls

These are current compile blockers because `Schema.hh:209-213` only declares writer-taking helper overloads. They are also acceptance blockers for explicit reporter plumbing if a stale no-writer overload is reintroduced to make them compile.

| File | Lines | Stale call shape |
| --- | --- | --- |
| `src/operation/iCTS/test/common/logging/ScopedLogFileTest.cc` | `61`, `66`, `70` | `icts::schema::EmitKeyValueTable("...", fields)` has no `SchemaWriter&`. |
| `src/operation/iCTS/test/common/logging/ScopedLogFileTest.cc` | `119` | `icts::schema::EmitDiagnostic(level, owner, summary, fields)` has no `SchemaWriter&`. |
| `src/operation/iCTS/test/flow/synthesis/TopologyArtifactWriter.cc` | `258`, `261` | `icts::schema::EmitArtifact(label, path)` has no `SchemaWriter&`. |
| `src/operation/iCTS/test/flow/synthesis/htree/HTreeArtifactWriter.cc` | `68`, `71`, `74`, `77` | `icts::schema::EmitArtifact(label, path)` has no `SchemaWriter&`. |
| `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkTest.cc` | `96` | `icts::schema::EmitArtifact(label, path)` has no `SchemaWriter&`. |

Suggested acceptance direction: pass a fixture-owned or runtime-owned `schema::SchemaWriter&` into these test helpers, or call `icts_test::runtime::SharedRuntime().reporter` only if the shared test runtime remains accepted.

#### Stale static `FastSTA` calls in tests

No `FastSTA::getInst()` calls remain, but `FastSTATest` still calls non-static `FastSTA` APIs as if they were static. This is likely the current FastSTA compile blocker after the singleton API removal.

`FastSta.hh:219-258` declares these APIs as instance methods. Current stale call sites:

| File | Lines | Stale static call |
| --- | --- | --- |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `219` | `icts::FastSTA::registerClockContext(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `434` | `icts::FastSTA::queryCapStatus(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `458`, `459` | `icts::FastSTA::querySlewStatus(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `513` | `icts::FastSTA::queryClockGraphProfile(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `525` | `icts::FastSTA::queryClockAnalysisStatus(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `534` | `icts::FastSTA::queryClockTreeTopology(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `552` | `icts::FastSTA::collectClockSizingBuffers(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `561` | `icts::FastSTA::collectClockSinkArrivals(...)` |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc` | `567` | `icts::FastSTA::queryClockNodeArrival(...)` |

Suggested acceptance direction: route these through `icts_test::runtime::SharedRuntime().fast_sta` or a local fixture-owned `FastSTA` instance.

#### Test shared runtime is singleton-shaped

`src/operation/iCTS/test/common/CTSTestRuntime.cc:28-31` defines:

```cpp
auto SharedRuntime() -> icts::CTSRuntime&
{
  static icts::CTSRuntime runtime;
  return runtime;
}
```

This does not violate the literal `*_INST`/`getInst` searches, but it is a process-wide test singleton. If the parent acceptance criterion is read as "no internal CTS singletons in source/api/test except API", this should be treated as an acceptance blocker or explicitly documented as a short-lived test migration bridge. High-density users include `src/operation/iCTS/test/main.cc:50-78`, `src/operation/iCTS/test/flow/FlowDesignFixture.hh:79-107`, `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc:55-444`, and the real-tech helpers/tests.

### Recently Explicitized APIs

| API family | Current status |
| --- | --- |
| `ClockDataRead::read()` | No no-arg declaration or call found. Current signature is explicit at `ClockDataRead.hh:40` and `ClockDataRead.cc:46`; flow call is explicit at `Flow.cc:156`. |
| `Config::emitRuntimeConfigReport("...")` | No title-only call found. Current calls pass a reporter at `Setup.cc:115` and `TopologyTest.cc:313`. |
| `schema::EmitTable/EmitKeyValueTable/EmitDiagnostic/EmitArtifact` | Source call sites are explicit. Test no-writer blockers remain at `ScopedLogFileTest.cc:61`, `:66`, `:70`, `:119`, `TopologyArtifactWriter.cc:258`, `:261`, `HTreeArtifactWriter.cc:68`, `:71`, `:74`, `:77`, and `FastClusteringRealTechBenchmarkTest.cc:96`. |
| `Wrapper::read/readClocks/writeClocksDetailed/writeClocks` | Public wrapper signatures are explicit at `Wrapper.hh:116-124`. No stale public no-arg call was found. Internal helper methods such as `CtsClockReader::readClocks(...)` and `CtsClockIdbWriter::writeClocksDetailed(...)` have already captured design/reporter through their constructors, so they are not public wrapper blockers. |
| `SdcClockReader::readClockData/readDeclarationsOnly` | Both signatures require `schema::SchemaWriter&` at `SdcClockReader.hh:137-138`; no stale no-writer SDC reader call found. |
| `STAAdapter` methods requiring `Config&` | No stale no-config call found for `init`, `refreshFullDesignTimingContext`, `queryConfiguredClockRouteSegmentRc`, `queryClockSourceDriveCapLimit`, `queryPinSlewLimit`, `installClockNetRcTree`, or `emitConfiguredUnitWireRcReport`. Current calls pass config, for example `Setup.cc:98`, `Setup.cc:117`, `QorEvaluation.cc:82`, `QorEvaluationMetrics.cc:339`, `QorEvaluationMetrics.cc:343`, `TopologyTest.cc:444`, and real-tech tests. |
| `FastSTA::getInst()` | Not found. Stale static `FastSTA::...` calls remain in `FastSTATest.cc` as listed above. |

### Code Patterns

Good patterns now present:

- Runtime state is centralized as normal fields in `CTSRuntime` (`src/operation/iCTS/source/flow/CTSRuntime.hh:38-43`).
- `Flow` passes explicit dependencies at major stage boundaries (`src/operation/iCTS/source/flow/Flow.cc:156`, `:182`, `:201`, `:222`, `:251`).
- Schema helpers no longer have no-writer overloads or a singleton writer root (`src/operation/iCTS/source/utils/logger/Schema.hh:209-213`, `Schema.cc:483-519`).
- Standalone schema output no longer locks the former singleton writer; it uses a file-local append mutex (`src/operation/iCTS/source/utils/logger/Schema.cc:476-480`).

Bad or risky patterns still present:

- Tests still contain no-writer `schema::Emit*` calls, which will not compile against the current writer-only helper declarations.
- `FastSTATest` still uses instance methods as static `FastSTA::...` calls after `FastSTA` became runtime-owned.
- `SharedRuntime()` is a process-wide static test runtime. It may be acceptable as a temporary migration helper, but it is not fixture-local explicit state.
- Broad `const Config&` still flows through many synthesis/report/evaluation paths, for example `Synthesis.hh:45`, `Topology.hh:151`, `SourceTrunk.hh:45`, `SinkBranch.hh:42`, `CharacterizationLibrary.hh:63`, `QorEvaluation.hh:101`, and `Report.hh:51`. This is no longer hidden singleton access, but it is still broader than the task design's narrow `{Name}Config` goal.

### High-Risk Spec / Task Drift For Finish

These are not source blockers, but they should be cleaned up before or during finish so future agents do not reintroduce internal singleton patterns.

| File | Lines | Drift |
| --- | --- | --- |
| `.trellis/spec/backend/error-handling.md` | `43-53`, `64` | Examples still use `WRAPPER_INST` and `STA_ADAPTER_INST`, and text still says "required singletons". Replace with explicit `Wrapper&` / STA facade examples and "required runtime state". |
| `.trellis/spec/guides/cross-layer-thinking-guide.md` | `38` | Boundary checklist still asks whether required singletons are initialized. Update to runtime-owned dependencies or explicit initialization state. |
| `.trellis/spec/backend/database-guidelines.md` | `64`, `100-106`, `120` | The top of the file now says only `CTS_API_INST` remains, but later wording still says "critical singleton state" and keeps a generic singleton implementation checklist. Narrow this to the external API singleton or explicit runtime state. |
| `.trellis/spec/backend/directory-structure.md` | `65` | Still names `CONFIG_INST` in the module-boundary rule. This is directionally correct as a prohibition, but finish should prefer "global config access" or "runtime-owned Config" wording now that the macro is gone. |
| `.trellis/tasks/05-24-cts-reporter-config-explicit/prd.md` | `39` | The child task says deletion of `DESIGN_INST`, `WRAPPER_INST`, `STA_ADAPTER_INST`, and `FAST_STA_INST` is out of scope, but the parent/current acceptance now says all internal CTS singletons are removed except API. Update or supersede this task note at finish so acceptance history is coherent. |

## External References

No external references were needed. This was an internal repository scan only.

## Related Specs

- `.trellis/tasks/05-24-cts-reporter-config-explicit/prd.md`: reporter/config desingleton goal, acceptance criteria, and currently stale out-of-scope note.
- `.trellis/tasks/05-24-cts-reporter-config-explicit/design.md`: explicit reporter/config boundary design.
- `.trellis/tasks/05-24-cts-reporter-config-explicit/implement.md`: validation grep baseline and migration checklist.
- `.trellis/spec/backend/database-guidelines.md`: singleton/runtime ownership and dependency boundaries.
- `.trellis/spec/backend/logging-guidelines.md`: explicit runtime-owned `SchemaWriter` guidance.
- `.trellis/spec/backend/error-handling.md`: stale singleton examples that should be updated.
- `.trellis/spec/guides/cross-layer-thinking-guide.md`: stale singleton checklist wording.

## Caveats / Not Found

- This was a textual scan only. No build or tests were run because this sidecar is read-only and build/test commands can write outside the task research directory.
- The tree is dirty and the main agent is working in parallel, so all line numbers are point-in-time findings from the scan timestamp above.
- `python3 ./.trellis/scripts/task.py current --source` reported no active task; the output path came from the user's explicit instruction.
- No internal `*_INST` macro remains in `source`, `api`, or `test`; only allowed `CTS_API_INST` remains.
- No internal `getInst` call remains in `source`, `api`, or `test`; only allowed API `getInst()` uses remain.
- No stale no-arg `ClockDataRead::read`, title-only `Config::emitRuntimeConfigReport`, public no-arg `Wrapper` read/write call, no-writer `SdcClockReader` call, no-config `STAAdapter` call, or `FastSTA::getInst()` call was found.
