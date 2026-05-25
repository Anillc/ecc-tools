# Research: CTS singleton scan

- Query: scan `src/operation/iCTS/source`, `src/operation/iCTS/api`, and `src/operation/iCTS/test` for remaining internal singleton macros, `getInst` usages, and schema helpers that still hide the reporter singleton during the CTS desingleton refactor.
- Scope: internal
- Date: 2026-05-24

## Findings

### Baseline

Primary grep:

```bash
rg -n 'SCHEMA_WRITER_INST|SchemaWriter::getInst|CONFIG_INST|Config::getInst|DESIGN_INST|Design::getInst|WRAPPER_INST|Wrapper::getInst|STA_ADAPTER_INST|STAAdapter::getInst|FAST_STA_INST|FastSTA::getInst|CTS_API_INST|\bgetInst\s*\(' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
```

Macro / explicit class `getInst` counts from this scan:

| Pattern | Count | Classification |
| --- | ---: | --- |
| `CONFIG_INST` | 184 | config |
| `DESIGN_INST` | 167 | design |
| `WRAPPER_INST` | 55 | wrapper |
| `SCHEMA_WRITER_INST` | 52 | reporter |
| `Wrapper::getInst` | 2 | wrapper |
| `SchemaWriter::getInst` | 2 | reporter |
| `FastSTA::getInst` | 2 | fast_sta |
| `Design::getInst` | 2 | design |
| `Config::getInst` | 1 | config |
| `CTS_API_INST` | 1 | flow/API, allowed by parent acceptance |
| `CTSAPI::getInst` | 1 | flow/API, allowed by parent acceptance |

No `STA_ADAPTER_INST`, `STAAdapter::getInst`, or `FAST_STA_INST` macro was found. `FastSTA::getInst` appears only in a test, and `FastSTA` no longer declares such an API.

### Current explicit-owner pattern already present

- `src/operation/iCTS/source/flow/CTSRuntime.hh:38` owns `Config`, `Design`, `Wrapper`, `STAAdapter`, `FastSTA`, and `schema::SchemaWriter` as normal runtime state.
- `src/operation/iCTS/source/flow/CTSRuntime.hh:45` builds a `CTSContext` from explicit runtime-owned objects.
- `src/operation/iCTS/api/CTSAPI.cc:101` passes runtime-owned `config`, `design`, `wrapper`, `sta_adapter`, and `reporter` into `Setup::initializeRuntime`.
- `src/operation/iCTS/source/flow/Flow.cc:156`, `:182`, `:201`, `:221`, `:249`, and `:261` show the intended API/flow boundary: flow methods pass explicit references into setup, synthesis, optimization, instantiation, report, and runtime setup.

Recommended direction: keep ownership at `CTSAPI` / `CTSRuntime` / `Flow`; do not replace the removed singletons with a registry, service locator, or deep `CTSRuntime` pass. Deep code should receive the narrow dependency it actually needs.

### Reporter dependencies

Files found:

| File | Remaining pattern | Module / flow | Recommendation |
| --- | --- | --- | --- |
| `src/operation/iCTS/source/utils/logger/Schema.hh:136`, `:215` | `SchemaWriter::getInst`, `SCHEMA_WRITER_INST` | reporter infra | Remove singleton API/macro after all no-arg helpers and call sites are converted. Keep writer-overload helpers only. |
| `src/operation/iCTS/source/utils/logger/Schema.cc:478`, `:492`, `:504`, `:527`, `:538` | singleton-backed no-arg helper implementations | reporter infra | Delete or make private the no-arg `Emit*` overloads; callers must pass `schema::SchemaWriter&`. |
| `src/operation/iCTS/source/flow/setup/clock_data/ClockDataRead.cc:48` | `Design::getInst`, `Wrapper::getInst`, `SchemaWriter::getInst` in no-arg bridge | setup/read-data | Flow already calls `read(_runtime.design, _runtime.wrapper, _runtime.reporter)` at `Flow.cc:156`; remove the no-arg bridge. |
| `src/operation/iCTS/source/module/topology/TopologyGen.cc:197`, `:217`, `:236`, `:290`, `:362` | direct `SCHEMA_WRITER_INST` and no-arg `schema::Emit*` | topology module | Add `schema::SchemaWriter*` or `schema::SchemaWriter&` to `TopologyGen::BuildOptions`; private report helpers receive writer. |
| `src/operation/iCTS/source/module/characterization/builder/CharBuildOrchestrator.cc:68`, `:156`, `:193-195`, `:197` | direct writer singleton / no-arg diagnostic | characterization module | Add reporter to `CharBuilder::InitOptions` or bind a non-owning reporter in `CharBuilder`; `CharacterizationLibrary::buildRuntimeOptions` should populate it from HTree options. |
| `src/operation/iCTS/source/module/characterization/builder/CharSetupConfigurator.cc:337`, `:348`, `:373`, `:394`, `:470`, `:478`, `:503` | direct writer singleton / no-arg `schema::Emit*` | characterization setup | Same as above; configurator should get writer through `CharBuilder` init state. |
| `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc:235`, `:400`, `:434` | `SCHEMA_WRITER_INST` | HTree topology pruning | Pass reporter from `HTree::BuildOptions` through pruning/evaluation helpers. |
| `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlanReport.cc:70` | `SCHEMA_WRITER_INST` | HTree plan report | `DepthPlanReport` should accept writer or a narrow report sink from HTree flow. |
| `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc:329` | `SCHEMA_WRITER_INST` | HTree root-driver compensation | Thread reporter through compensation pass options/context. |
| `src/operation/iCTS/source/database/design/Design.cc:482`, `:517` | no-arg `schema::EmitTable` | design report summary | Prefer `emitClockDistributionSummary(schema::SchemaWriter&, title)` or return summary rows to flow/report. |
| `src/operation/iCTS/source/database/io/WrapperClockReader.cc:646`, `:664` and `WrapperClockWriter.cc:169`, `:186`, `:222` | no-arg schema helpers | wrapper/io | Wrapper read/write methods should accept a writer/report sink or return typed read/write summaries for flow to emit. |
| `src/operation/iCTS/source/database/adapter/sdc/SdcClockReader.cc:65`, `:70`, `:81`; `clock_trace/ClockTraceReport.cc:172`, `:215`, `:231`; `clock_trace/ClockTraceResolver.cc:120` | no-arg schema helpers | SDC adapter / clock trace | Pass reporter through read/trace options from `ClockDataRead`, or return diagnostics/summaries. |
| `src/operation/iCTS/source/database/adapter/sta/net_rc/STAAdapterWireRc.cc:94` | no-arg `schema::EmitTable` | sta_adapter report | `STAAdapter::emitUnitWireRcReport` / `emitConfiguredUnitWireRcReport` should accept `schema::SchemaWriter&`; `Setup::emitRuntimeSetup` already has one. |
| `src/operation/iCTS/source/flow/optimization/report/OptimizationReport.cc:101`, `:110`, `:116`, `:141` | no-arg schema helpers | optimization report | Optimization report helpers should accept the writer already available in `Optimization::run`. |
| `src/operation/iCTS/test/main.cc`, `test/common/io/TestArtifactIO.cc`, `test/common/logging/ScopedLogFileTest.cc`, HTree/topology smoke/artifact tests | direct writer singleton / no-arg helpers | test | Use fixture-owned `schema::SchemaWriter` and pass it to helpers. `test/main.cc` should not globally open/close the reporter after singleton removal. |

### Config dependencies

Files found:

| File | Remaining pattern | Module / flow | Recommendation |
| --- | --- | --- | --- |
| `src/operation/iCTS/source/database/config/Config.hh:34`, `:46` | `CONFIG_INST`, `Config::getInst` | config infra | Remove macro/API once tests and source call sites use runtime-owned `Config` or narrow config structs. |
| `src/operation/iCTS/source/database/config/Config.cc:316` | no-arg `emitRuntimeConfigReport` using no-arg schema helper | config report | Keep only `emitRuntimeConfigReport(schema::SchemaWriter&, title)`. |
| `src/operation/iCTS/source/database/adapter/fast_sta/clock_state/FastStaBuilder.cc:72`, `:82`, `:92`, `:135-136`; `segment_char/FastStaChar.cc:194` | routing layers, wire width, root slew, max cap from `CONFIG_INST` | fast_sta | Add `FastStaBuildOptions` / `FastStaCharOptions` carrying routing layer, wire width, root input slew, and max cap from flow/config adaptation. |
| `src/operation/iCTS/source/database/adapter/sta/cell_master/STAAdapterCellQuery.cc:163-167`, `:319`, `:325`, `:332`, `:353` | max cap / max sink transition from `CONFIG_INST` | sta_adapter | Pass explicit limit policy/options into STA adapter queries, or resolve limits at flow/config-adaptation boundary and keep low-level queries config-free. |
| `src/operation/iCTS/source/database/adapter/sta/timing_query/STAAdapterTimingQuery.cc:117`, `:267`, `:277`; `net_rc_tree/STAAdapterRcTree.cc:123`, `:131` | work dir, routing layer, wire width from `CONFIG_INST` | sta_adapter | `STAAdapter` currently static; make workspace and route-RC inputs explicit through setup/adapter initialization or query options. |
| `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTracePins.cc:325` | max fanout from `CONFIG_INST` | sdc/clock-trace adapter | Pass a narrow trace policy such as `min_clock_target_sinks/max_fanout` from setup or read-data options. |
| `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.cc:142` | buffer types from `CONFIG_INST` | instantiation | Pass `buffer_types` as an instantiation input from flow/config adaptation. |
| `src/operation/iCTS/source/flow/synthesis/htree/constraint/Constraint.cc:48` | force branch buffer from `CONFIG_INST` | HTree constraints | `HTree::BuildOptions` already has `force_branch_buffer`; ensure callers set it from a narrow HTree config and remove fallback. |
| `src/operation/iCTS/source/flow/synthesis/htree/plan/Plan.cc:154` | depth explore window from `CONFIG_INST` | HTree planning | `HTree::BuildOptions` already has `depth_explore_window`; set it at topology/HTree boundary. |
| `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc:114-115`, `:288`, `:316-317`, `:344-345` | max cap/max fanout from `CONFIG_INST` | HTree sink-load legality | Pass a narrow sink-load legality config with `max_fanout`, optional `max_cap`, and route RC. |
| `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensationLoad.cc:76`, `:84` | routing layer / wire width from `CONFIG_INST` | HTree root-driver compensation | Pass routing layer and wire width or prebuilt `ClockRouteSegmentRc`; this helper should not know `Config`. |
| `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc`, topology/HTree real-tech matrix and smoke tests, `test/module/characterization/fixture/CharacterizationRealTechFixture.cc`, `test/common/realtech/asset/RealTechAssetLoader.cc`, fast-clustering benchmark tests | fixture setup reads/writes `CONFIG_INST` | test | Introduce fixture-owned `Config` / `CTSRuntime` and explicit helper args. Do not preserve cross-test global config state. |

Pattern note: `src/operation/iCTS/source/flow/synthesis/htree/HTreeSynthesisOptions.hh:44-50` already passes dependencies explicitly, but `const Config* config` is broad. This is useful as a short migration bridge, but the task design says deep algorithms should receive narrow `{Name}Config` / input structs. Candidate narrow splits: HTree search knobs, characterization grid/limit knobs, routing RC inputs, and report sink.

### Design dependencies

Files found:

| File | Remaining pattern | Module / flow | Recommendation |
| --- | --- | --- | --- |
| `src/operation/iCTS/source/database/design/Design.hh:40`, `:48` | `DESIGN_INST`, `Design::getInst` | design infra | Remove singleton API/macro after DB/flow/test call sites receive explicit `Design&`. |
| `src/operation/iCTS/source/database/io/WrapperClockReader.cc:623`, `:624`, `:679`, `:744`, `:775`, `:873`, `:898`, `:908`, `:929`, `:934`, `:984` | wrapper mutates global design | wrapper/io | Wrapper clock read should receive `Design&` or be explicitly bound to a design at setup initialization. Prefer method args for read/write operations. |
| `src/operation/iCTS/source/database/io/WrapperClockWriter.cc:171`, `:217`, `:224`, `:231`; `Wrapper.cc:116` | wrapper reads global design | wrapper/io | Pass `Design&` into write/report functions; avoid hidden design reads. |
| `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.cc:223-501` | many global design mutations | instantiation | `Instantiation::run` already has `Design&`; pass it into lower design-conversion helpers instead of using `DESIGN_INST`. |
| `src/operation/iCTS/source/flow/optimization/clock_sizing_edit/ClockSizingAcceptedEdit.cc:79`, `:91`, `:143`, `:156` | design lookup/rename from singleton | optimization | Pass `Design&` into accepted-edit apply helpers. |
| `src/operation/iCTS/source/flow/setup/clock_data/ClockDataRead.cc:48` | no-arg bridge to `Design::getInst` | setup/read-data | Remove bridge; explicit overload already exists. |
| `src/operation/iCTS/test/database/design/ClockDAGTest.cc`, `test/flow/FlowDesignFixture.hh`, `FlowSdcTraceTest.cc`, `FlowWritebackTest.cc`, `TopologyTest.cc`, real-tech setup/tests | fixture/test global design | test | Use fixture-owned `Design` or fixture-owned `CTSRuntime`. |

### Wrapper dependencies

Files found:

| File | Remaining pattern | Module / flow | Recommendation |
| --- | --- | --- | --- |
| `src/operation/iCTS/source/database/io/Wrapper.hh:47`, `:81` | `WRAPPER_INST`, `Wrapper::getInst` | wrapper infra | Remove singleton API/macro after explicit wrapper ownership is complete. |
| `src/operation/iCTS/source/flow/setup/clock_data/ClockDataRead.cc:48` | no-arg bridge to `Wrapper::getInst` | setup/read-data | Remove bridge; flow already passes `_runtime.wrapper`. |
| `src/operation/iCTS/source/database/adapter/fast_sta/clock_state/FastStaBuilder.cc:87`; `segment_char/FastStaChar.cc:63` | DBU from `WRAPPER_INST` | fast_sta | Pass `dbu_per_um` as a narrow input rather than `Wrapper&` when only units are needed. |
| `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensationLoad.cc:90` | DBU from `WRAPPER_INST` | HTree compensation | Pass `dbu_per_um` from `HTree::build`/wrapper boundary into compensation load helpers. |
| `src/operation/iCTS/test/...` flow/writeback/sdc/real-tech tests | fixture global wrapper | test | Fixture should own `Wrapper` and initialize it explicitly. |

### STA adapter dependencies

No `STA_ADAPTER_INST` or `STAAdapter::getInst` remains, but `STAAdapter` is still a static facade and several static methods read `CONFIG_INST` or emit via no-arg schema helpers.

Files found:

| File | Remaining pattern | Module / flow | Recommendation |
| --- | --- | --- | --- |
| `src/operation/iCTS/source/flow/setup/Setup.cc:55`, `:73` | `STAAdapter& sta_adapter` is accepted but discarded; `STAAdapter::init()` is static | setup / sta_adapter | Runtime owns `STAAdapter`, but initialization does not use the instance yet. Move toward instance methods or make the static boundary explicitly documented as external iSTA facade while passing all config/report inputs as args. |
| `src/operation/iCTS/source/database/adapter/sta/*` files listed under reporter/config | static adapter methods depend on config/report globals | sta_adapter | Recommended signature direction: `STAAdapter&` or narrow query structs (`StaWorkspaceConfig`, `ClockRouteRcConfig`, limit policies), not hidden `CONFIG_INST` or `SCHEMA_WRITER_INST`. |

### FastSTA dependencies

Files found:

| File | Remaining pattern | Module / flow | Recommendation |
| --- | --- | --- | --- |
| `src/operation/iCTS/source/flow/CTSRuntime.hh:42` | runtime-owned `FastSTA fast_sta` | flow boundary | This is the intended owner. |
| `src/operation/iCTS/source/flow/synthesis/Synthesis.hh:45`, `Synthesis.cc:94`, `:157`, `:193`; `Optimization.hh:53`, `Optimization.cc:52` | explicit `FastSTA&` | synthesis/optimization | Keep this direction. |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc:208-209`, `:219`, `:434`, `:458-459`, `:513`, `:525`, `:534`, `:552`, `:561`, `:567` | test still calls `FastSTA::getInst()` or non-static methods as static | test / fast_sta | Create a fixture-owned `FastSTA fast_sta;`; call instance methods. The current test likely no longer compiles because `FastSTA` has no `getInst` and its APIs are non-static. |

### Flow/API dependency

Files found:

| File | Remaining pattern | Module / flow | Recommendation |
| --- | --- | --- | --- |
| `src/operation/iCTS/api/CTSAPI.hh:34`, `:42`; `CTSAPI.cc:81`, `:86`, `:91`, `:99`, `:113` | `CTS_API_INST` and `CTSAPI::getInst` / unqualified API `getInst()` | API | Parent acceptance explicitly allows only `CTS_API_INST`; no action for this child unless unqualified `getInst()` grep noise needs documentation. |

## Code patterns

- Good pattern: API creates runtime state and flow passes explicit references (`CTSRuntime.hh:38-45`, `CTSAPI.cc:101`, `Flow.cc:156-261`).
- Bad reporter pattern: no-arg schema helpers still resolve `SCHEMA_WRITER_INST` internally (`Schema.cc:492`, `:504`, `:527`, `:538`), so call sites can hide the singleton even when they do not spell the macro.
- Bad config pattern: low-level algorithms and adapters read `CONFIG_INST` directly for behavior-changing knobs (`Constraint.cc:48`, `Plan.cc:154`, `SinkLoadRegion.cc:114`, `STAAdapterTimingQuery.cc:267`) instead of receiving narrow config.
- Partial migration pattern: several APIs now have explicit overloads but old no-arg bridges remain (`ClockDataRead.hh:39-40`, `ClockDataRead.cc:45-49`; `Config.hh:173-174`, `Config.cc:316-323`).
- Broad-dependency bridge: `HTreeSynthesisDependencies` explicitly passes `Config*`, `Design*`, `Wrapper*`, `FastSTA*`, and `SchemaWriter*` (`HTreeSynthesisOptions.hh:44-50`). This avoids globals but is still broader than the task's desired deep-algorithm boundary.

## Compile-Risk Hotspots

- `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc:208-219` is likely already broken after the FastSTA desingleton move: it calls `FastSTA::getInst()` and static `FastSTA::registerClockContext`, but `FastSTA.hh:207-273` declares normal instance methods and no `getInst`.
- `src/operation/iCTS/source/flow/setup/clock_data/ClockDataRead.cc:48` will fail as soon as `Design::getInst`, `Wrapper::getInst`, or `SchemaWriter::getInst` is removed, even though the flow path already uses the explicit overload.
- `src/operation/iCTS/source/utils/logger/Schema.hh:136`, `:215` plus `Schema.cc:492-538` are the central reporter removal blockers. Removing `SchemaWriter::getInst` before removing no-arg `schema::Emit*` overloads will cascade compile failures through source and tests.
- `src/operation/iCTS/source/database/config/Config.hh:34`, `:46` and `Config.cc:316` are the central config removal blockers. `TopologyTest.cc:312-313` still uses the old no-arg config report path.
- HTree/topology compile path is partially converted: `HTree::build` requires explicit dependencies (`HTree.cc:85-94`), but subhelpers still read globals (`Constraint.cc:48`, `Plan.cc:154`, `SinkLoadRegion.cc:114-115`, `TopologyPruning.cc:235`, `DepthPlanReport.cc:70`, `RootDriverCompensationLoad.cc:76-90`, `RootDriverCompensation.cc:329`).
- `TopologyGen::BuildOptions` lacks a reporter even though `TopologyGen.cc:197`, `:217`, `:236`, `:290`, and `:362` emit reports. Adding the reporter there will touch both the public build overloads and private helper signatures.
- `CharBuilder::InitOptions` lacks a reporter while `CharSetupConfigurator` and `CharBuildOrchestrator` emit heavily. Adding it will require updates through `CharacterizationLibrary::buildRuntimeOptions`, `RunCharacterizationFlow`, and direct characterization tests.
- `Setup::emitRuntimeSetup` has an explicit reporter but calls `STAAdapter::emitConfiguredUnitWireRcReport("Runtime Routing / Wire RC")`, which currently emits through no-arg schema helpers (`STAAdapterWireRc.cc:90-99`).

## External References

No external references were needed. This was an internal code scan only.

## Related Specs

- `.trellis/tasks/05-24-cts-reporter-config-explicit/prd.md`: child task goal and acceptance for removing `SCHEMA_WRITER_INST`, `SchemaWriter::getInst`, `CONFIG_INST`, and `Config::getInst`.
- `.trellis/tasks/05-24-cts-reporter-config-explicit/design.md`: explicit reporter/config boundary rules and config/input/summary classification.
- `.trellis/tasks/05-24-cts-reporter-config-explicit/implement.md`: grep baseline, risk files, and staged migration notes.
- `.trellis/spec/project-constraints.md`: iCTS hard constraints.
- `.trellis/spec/backend/database-guidelines.md`: current singleton/ownership rules; note this spec still documents old singleton boundaries and is partially superseded by the active task.
- `.trellis/spec/backend/directory-structure.md`: layer ownership and flow order; already says module code should receive explicit options instead of reading `CONFIG_INST`.
- `.trellis/spec/backend/logging-guidelines.md`: current report/logging rules; note this spec still references `SCHEMA_WRITER_INST` and conflicts with the active desingleton task.
- `.trellis/spec/backend/quality-guidelines.md`: dependency visibility and include guidance.

## Caveats / Not Found

- `python3 ./.trellis/scripts/task.py current --source` returned no active task, so this note uses the task directory explicitly supplied by the user: `.trellis/tasks/05-24-cts-reporter-config-explicit/research/`.
- This scan was textual and did not run a build. Compile-risk notes are inferred from current declarations and call sites.
- No `STA_ADAPTER_INST`, `STAAdapter::getInst`, or `FAST_STA_INST` macro was found in `source`, `api`, or `test`.
- The active task supersedes parts of the current backend/database and logging specs that still endorse `CONFIG_INST` / `SCHEMA_WRITER_INST`.
