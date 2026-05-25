# Research: current singleton acceptance scan

- Query: scan current dirty tree under `src/operation/iCTS/source`, `src/operation/iCTS/api`, and `src/operation/iCTS/test` for remaining singleton macro/getInst acceptance gaps after the CTS desingleton refactor.
- Scope: internal
- Date: 2026-05-24

## Findings

### Scan basis

Current task discovery:

- `python3 ./.trellis/scripts/task.py current --source` returned no active task (`Current task: (none)`), so this record uses the task directory explicitly supplied by the user.
- No build was run. This is a textual acceptance scan against the current dirty tree.

Primary commands used:

- `rg -n '\b[A-Z][A-Z0-9_]*_INST\b' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test`
- `rg -n '\b[A-Za-z_][A-Za-z0-9_:<>]*::getInst\b|\bgetInst\s*\(' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test`
- `rg -n 'CONFIG_INST|SCHEMA_WRITER_INST|DESIGN_INST|WRAPPER_INST|FAST_STA_INST|STA_ADAPTER_INST' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test`
- `rg -n 'STAAdapter::(init|refreshFullDesignTimingContext|queryConfiguredClockRouteSegmentRc|queryClockSourceDriveCapLimit|queryPinSlewLimit|installClockNetRcTree|emitConfiguredUnitWireRcReport)|FastSTA::buildClockContext|CharacterizationLibrary::buildRuntimeOptions' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test`

### Acceptance classification summary

Direct `*_INST` macro matches:

| Classification | Pattern | Count | Notes |
| --- | --- | ---: | --- |
| allowed | `CTS_API_INST` | 1 | Allowed API singleton boundary at `src/operation/iCTS/api/CTSAPI.hh:34`. |
| must remove | `DESIGN_INST` | 167 | Internal design singleton still used in source and tests. |
| must remove | `CONFIG_INST` | 151 | Internal config singleton still used in source and tests. |
| must remove | `WRAPPER_INST` | 52 | Internal wrapper singleton still used mostly in tests; macro definition remains. |
| must remove | `SCHEMA_WRITER_INST` | 37 | Internal reporter singleton remains in logger infrastructure and tests. |
| not found | `FAST_STA_INST` | 0 | No macro definition or use found in iCTS. |
| not found | `STA_ADAPTER_INST` | 0 | No macro definition or use found in iCTS. |

`getInst` matches:

| Classification | Occurrences | Lines |
| --- | ---: | --- |
| allowed CTS API | 7 | `src/operation/iCTS/api/CTSAPI.hh:34`, `src/operation/iCTS/api/CTSAPI.hh:42`, `src/operation/iCTS/api/CTSAPI.cc:81`, `:86`, `:91`, `:99`, `:113` |
| must remove internal | 13 | `SchemaWriter` at `Schema.hh:136`, `Schema.hh:215`, `ClockDataRead.cc:48`; `Config` at `Config.hh:34`, `Config.hh:46`; `Design` at `Design.hh:40`, `Design.hh:48`, `ClockDataRead.cc:48`; `Wrapper` at `Wrapper.hh:47`, `Wrapper.hh:81`, `ClockDataRead.cc:48`; stale `FastSTA::getInst` calls at `FastSTATest.cc:208`, `:209` |

### Files found

Core explicit-owner pattern now present:

| File | Description |
| --- | --- |
| `src/operation/iCTS/source/flow/CTSRuntime.hh:38` | Runtime-owned `Config`, `Design`, `Wrapper`, `STAAdapter`, `FastSTA`, and `schema::SchemaWriter`; this is the desired dependency owner. |
| `src/operation/iCTS/source/flow/CTSRuntime.hh:45` | Builds `CTSContext` from explicit runtime-owned objects. |
| `src/operation/iCTS/source/flow/Flow.cc:156` | Read-data path passes `_runtime.design`, `_runtime.wrapper`, and `_runtime.reporter`. |
| `src/operation/iCTS/source/flow/Flow.cc:182` | Synthesis path passes `_runtime.config`, `_runtime.design`, `_runtime.wrapper`, `_runtime.fast_sta`, and `_runtime.reporter`. |
| `src/operation/iCTS/source/flow/Flow.cc:201` | Optimization path passes explicit runtime-owned dependencies. |
| `src/operation/iCTS/source/flow/Flow.cc:222` | Instantiation path passes explicit design/wrapper/reporter but not config. |
| `src/operation/iCTS/source/flow/Flow.cc:251` | Report path passes explicit runtime-owned dependencies. |
| `src/operation/iCTS/source/flow/setup/Setup.cc:55` | Runtime setup signature accepts explicit `Config&`, `Design&`, `Wrapper&`, `STAAdapter&`, and reporter. |

Macro/getInst definition files:

| File | Description |
| --- | --- |
| `src/operation/iCTS/api/CTSAPI.hh:34` | Defines allowed `CTS_API_INST`. |
| `src/operation/iCTS/source/database/config/Config.hh:34`, `:46` | Defines internal `CONFIG_INST` and `Config::getInst`; must remove. |
| `src/operation/iCTS/source/database/design/Design.hh:40`, `:48` | Defines internal `DESIGN_INST` and `Design::getInst`; must remove. |
| `src/operation/iCTS/source/database/io/Wrapper.hh:47`, `:81` | Defines internal `WRAPPER_INST` and `Wrapper::getInst`; must remove. |
| `src/operation/iCTS/source/utils/logger/Schema.hh:136`, `:215` | Defines internal `SchemaWriter::getInst` and `SCHEMA_WRITER_INST`; must remove. |
| `src/operation/iCTS/source/flow/setup/clock_data/ClockDataRead.cc:46`, `:48` | Old no-arg bridge still calls `Design::getInst`, `Wrapper::getInst`, and `schema::SchemaWriter::getInst`. |
| `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc:208`, `:209` | Stale `FastSTA::getInst()` test calls remain; `FastSTA` is now instance-owned. |

### All direct `*_INST` macro use inventory

Allowed:

| Count | Macro | Lines |
| ---: | --- | --- |
| 1 | `CTS_API_INST` | `src/operation/iCTS/api/CTSAPI.hh:34` |

Internal, must remove:

| Count | File / macro | Lines |
| ---: | --- | --- |
| 1 | `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTracePins.cc:CONFIG_INST` | 325 |
| 1 | `src/operation/iCTS/source/database/config/Config.hh:CONFIG_INST` | 34 |
| 1 | `src/operation/iCTS/source/database/design/Design.hh:DESIGN_INST` | 40 |
| 1 | `src/operation/iCTS/source/database/io/Wrapper.cc:DESIGN_INST` | 116 |
| 1 | `src/operation/iCTS/source/database/io/Wrapper.hh:WRAPPER_INST` | 47 |
| 11 | `src/operation/iCTS/source/database/io/WrapperClockReader.cc:DESIGN_INST` | 623,624,679,744,775,873,898,908,929,934,984 |
| 4 | `src/operation/iCTS/source/database/io/WrapperClockWriter.cc:DESIGN_INST` | 171,217,224,231 |
| 1 | `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.cc:CONFIG_INST` | 142 |
| 14 | `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.cc:DESIGN_INST` | 223,228,239,251,255,267,281,286,428,449,466,477,490,501 |
| 4 | `src/operation/iCTS/source/flow/optimization/clock_sizing_edit/ClockSizingAcceptedEdit.cc:DESIGN_INST` | 79,91,143,156 |
| 5 | `src/operation/iCTS/source/utils/logger/Schema.cc:SCHEMA_WRITER_INST` | 478,492,504,527,538 |
| 1 | `src/operation/iCTS/source/utils/logger/Schema.hh:SCHEMA_WRITER_INST` | 215 |
| 8 | `src/operation/iCTS/test/common/io/TestArtifactIO.cc:SCHEMA_WRITER_INST` | 190,192,196,202,210,219,288,296 |
| 5 | `src/operation/iCTS/test/common/logging/ScopedLogFileTest.cc:SCHEMA_WRITER_INST` | 94,117,118,122,128 |
| 2 | `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:CONFIG_INST` | 383,446 |
| 2 | `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:WRAPPER_INST` | 455,456 |
| 3 | `src/operation/iCTS/test/common/realtech/setup/RealTechDesignSetup.cc:DESIGN_INST` | 165,166,173 |
| 1 | `src/operation/iCTS/test/common/realtech/setup/RealTechDesignSetup.cc:WRAPPER_INST` | 171 |
| 4 | `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc:CONFIG_INST` | 189,191,194,409 |
| 23 | `src/operation/iCTS/test/database/design/ClockDAGTest.cc:DESIGN_INST` | 48,53,73,83,96,102,120,171,172,190,191,209,210,233,234,252,253,272,273,274,303,304,309 |
| 2 | `src/operation/iCTS/test/database/design/ClockDAGTest.cc:WRAPPER_INST` | 47,52 |
| 4 | `src/operation/iCTS/test/flow/FlowDesignFixture.hh:CONFIG_INST` | 81,89,106,116 |
| 17 | `src/operation/iCTS/test/flow/FlowDesignFixture.hh:DESIGN_INST` | 82,90,107,117,182,195,210,216,219,222,229,255,433,435,441,445,453 |
| 2 | `src/operation/iCTS/test/flow/FlowDesignFixture.hh:SCHEMA_WRITER_INST` | 105,111 |
| 4 | `src/operation/iCTS/test/flow/FlowDesignFixture.hh:WRAPPER_INST` | 83,91,108,118 |
| 1 | `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc:CONFIG_INST` | 325 |
| 26 | `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc:DESIGN_INST` | 179,184,185,197,202,259,260,329,330,378,381,431,432,437,496,499,540,543,591,595,641,642,693,744,783,786 |
| 1 | `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc:SCHEMA_WRITER_INST` | 612 |
| 25 | `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc:WRAPPER_INST` | 174,175,216,217,276,277,345,346,397,398,453,454,495,507,508,561,562,617,618,670,671,704,705,755,756 |
| 8 | `src/operation/iCTS/test/flow/FlowTest.cc:DESIGN_INST` | 190,258,259,260,263,264,265,313 |
| 2 | `src/operation/iCTS/test/flow/FlowTest.cc:SCHEMA_WRITER_INST` | 77,180 |
| 24 | `src/operation/iCTS/test/flow/FlowWritebackTest.cc:DESIGN_INST` | 56,105,106,115,116,140,142,143,146,152,157,164,165,173,219,273,275,276,279,285,289,297,299,307 |
| 14 | `src/operation/iCTS/test/flow/FlowWritebackTest.cc:WRAPPER_INST` | 54,61,76,77,104,124,125,195,216,217,232,245,246,319 |
| 5 | `src/operation/iCTS/test/flow/synthesis/TopologyNonClusteredRealTechSmokeTest.cc:CONFIG_INST` | 75,76,77,78,115 |
| 1 | `src/operation/iCTS/test/flow/synthesis/TopologyNonClusteredRealTechSmokeTest.cc:SCHEMA_WRITER_INST` | 83 |
| 1 | `src/operation/iCTS/test/flow/synthesis/TopologyRealTechExperimentReport.cc:CONFIG_INST` | 160 |
| 1 | `src/operation/iCTS/test/flow/synthesis/TopologyRealTechHTreeAssertions.cc:CONFIG_INST` | 115 |
| 7 | `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc:CONFIG_INST` | 441,442,443,512,513,514,517 |
| 14 | `src/operation/iCTS/test/flow/synthesis/TopologyRealTechSmokeTest.cc:CONFIG_INST` | 76,77,78,79,80,81,129,168,169,170,171,172,173,174 |
| 2 | `src/operation/iCTS/test/flow/synthesis/TopologyRealTechSmokeTest.cc:SCHEMA_WRITER_INST` | 86,179 |
| 24 | `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc:CONFIG_INST` | 55,60,291,292,293,305,306,307,308,309,310,313,345,347,348,364,366,367,368,384,386,387,388,440 |
| 24 | `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc:DESIGN_INST` | 56,61,81,94,107,113,198,199,218,219,220,222,223,224,227,228,229,254,255,256,263,267,268,269 |
| 2 | `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc:SCHEMA_WRITER_INST` | 312,314 |
| 4 | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:CONFIG_INST` | 74,75,76,131 |
| 1 | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:SCHEMA_WRITER_INST` | 81 |
| 3 | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechMatrixRunner.cc:CONFIG_INST` | 186,187,188 |
| 1 | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechScenario.cc:CONFIG_INST` | 223 |
| 2 | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechScenario.cc:DESIGN_INST` | 115,122 |
| 7 | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc:CONFIG_INST` | 77,78,79,80,81,82,83 |
| 1 | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc:SCHEMA_WRITER_INST` | 94 |
| 4 | `src/operation/iCTS/test/main.cc:SCHEMA_WRITER_INST` | 50,54,72,78 |
| 3 | `src/operation/iCTS/test/module/characterization/CharacterizationRealTechLimitResolutionTest.cc:DESIGN_INST` | 167,187,420 |
| 1 | `src/operation/iCTS/test/module/characterization/CharacterizationRealTechSmokeTest.cc:CONFIG_INST` | 175 |
| 64 | `src/operation/iCTS/test/module/characterization/fixture/CharacterizationRealTechFixture.cc:CONFIG_INST` | 75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,104,105,107,109,110,112,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,136,136,137,139,139,140,142,143,145,146,147,148,150,152,156,157,273 |
| 1 | `src/operation/iCTS/test/module/characterization/fixture/CharacterizationRealTechFixture.cc:SCHEMA_WRITER_INST` | 223 |
| 2 | `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkMetrics.cc:CONFIG_INST` | 176,176 |
| 3 | `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:CONFIG_INST` | 168,169,170 |
| 2 | `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:DESIGN_INST` | 196,221 |
| 3 | `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:WRAPPER_INST` | 207,208,214 |
| 1 | `src/operation/iCTS/test/module/topology/topology_gen/fixture/core/TopologyGenScenarioRunner.cc:SCHEMA_WRITER_INST` | 58 |

### Requested singleton definitions and call points

| Singleton | Definition | Current call/use points |
| --- | --- | --- |
| `CONFIG_INST` | `src/operation/iCTS/source/database/config/Config.hh:34`; `Config::getInst` at `:46` | Source: `ClockTracePins.cc:325`, `DesignConversion.cc:142`; tests: see inventory above, especially `CharacterizationRealTechFixture.cc:75-157`, `TopologyTest.cc:55-440`, real-tech fixtures. |
| `SCHEMA_WRITER_INST` | `src/operation/iCTS/source/utils/logger/Schema.hh:215`; `SchemaWriter::getInst` at `:136` | Source: `Schema.cc:478`, `:492`, `:504`, `:527`, `:538`; tests: `test/main.cc`, `TestArtifactIO.cc`, `ScopedLogFileTest.cc`, flow/topology/HTree/characterization fixture files. |
| `DESIGN_INST` | `src/operation/iCTS/source/database/design/Design.hh:40`; `Design::getInst` at `:48` | Source: `Wrapper.cc:116`, `WrapperClockReader.cc:623-984`, `WrapperClockWriter.cc:171-231`, `DesignConversion.cc:223-501`, `ClockSizingAcceptedEdit.cc:79-156`; tests: flow fixtures, SDC/writeback/topology/real-tech setup. |
| `WRAPPER_INST` | `src/operation/iCTS/source/database/io/Wrapper.hh:47`; `Wrapper::getInst` at `:81` | Mostly tests: `ClockDAGTest.cc:47`, `FlowDesignFixture.hh:83`, `FlowSdcTraceTest.cc:174-756`, `FlowWritebackTest.cc:54-319`, real-tech setup/asset/benchmark helpers. No non-definition source macro use remains, but `ClockDataRead.cc:48` still calls `Wrapper::getInst()`. |
| `FAST_STA_INST` | Not found | Not found in `src/operation/iCTS`. Stale `FastSTA::getInst()` still appears in `FastSTATest.cc:208`, `:209`. |
| `STA_ADAPTER_INST` | Not found | Not found in `src/operation/iCTS`. `STAAdapter` remains a static facade and has stale no-arg call sites listed below. |

### Hidden reporter singleton through no-arg schema helpers

The direct `SCHEMA_WRITER_INST` count understates reporter acceptance gaps because several no-arg schema helpers still resolve the writer internally:

| File | Lines | Dependency gap | Suggested explicit dependency |
| --- | --- | --- | --- |
| `src/operation/iCTS/source/utils/logger/Schema.cc` | 490-538 | No-arg `EmitTable`, `EmitKeyValueTable`, `EmitDiagnostic`, `EmitArtifact` forward to `SCHEMA_WRITER_INST`. | Delete no-arg overloads after call sites pass `schema::SchemaWriter&`; keep writer-taking overloads. |
| `src/operation/iCTS/source/utils/logger/Schema.cc` | 476-480 | `SchemaWriter::appendStandaloneBlock` locks `SCHEMA_WRITER_INST` only to protect static append. | Use a file-local static mutex or caller-supplied writer/append context, not a singleton writer. |
| `src/operation/iCTS/source/database/config/Config.cc` | 316-318 | No-arg `Config::emitRuntimeConfigReport(title)` emits through no-arg `schema::EmitTable`. | Remove no-arg overload; callers use `emitRuntimeConfigReport(reporter, title)`. |
| `src/operation/iCTS/source/database/design/Design.cc` | 482, 517 | `Design::emitClockDistributionSummary(title)` emits through no-arg `schema::EmitTable`. | Change to `emitClockDistributionSummary(schema::SchemaWriter&, title)` or return rows for flow to emit. |
| `src/operation/iCTS/source/database/io/WrapperClockReader.cc` | 646, 664 | Reader emits no-arg schema table/diagnostic. | Pass `schema::SchemaWriter&` into `Wrapper::readClocks` / `readTraceClockTargets` and into `CtsClockReader`. |
| `src/operation/iCTS/source/database/io/WrapperClockWriter.cc` | 169, 186, 222 | Writer emits no-arg diagnostics. | Pass `schema::SchemaWriter&` into `Wrapper::writeClocksDetailed` / `CtsClockIdbWriter`. |
| `src/operation/iCTS/source/database/adapter/sdc/SdcClockReader.cc` | 65, 70, 81 | SDC reader emits no-arg diagnostics. | Pass reporter through `ClockDataRead::read` into `SdcClockReader`. |
| `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTraceResolver.cc` | 120 | Clock trace resolver emits no-arg diagnostic. | Pass reporter through `Wrapper::traceSdcClocks` / resolver options. |
| `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTraceReport.cc` | 172, 215, 231 | Clock trace report emits no-arg tables. | Pass reporter through clock-trace report functions from `ClockDataRead`. |
| `src/operation/iCTS/source/flow/optimization/report/OptimizationReport.cc` | 101, 110, 116, 141 | Optimization report helpers emit no-arg tables. | Add `schema::SchemaWriter&` to `EmitClockSummary` and `EmitClockProfile`; `Optimization::run` already has `reporter`. |
| `src/operation/iCTS/test/...` | multiple | Test artifact/report helpers use no-arg schema helpers or `SCHEMA_WRITER_INST`. | Fixture/listener-owned `schema::SchemaWriter`; pass writer to artifact helpers. |

### Signature drift and stale old call points

Current declarations have explicit config/reporter dependencies:

| API | Explicit current signature | Status |
| --- | --- | --- |
| `STAAdapter::init` | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:99`; definition `STAAdapter.cc:36` takes `const Config&`. | Stale no-arg test/helper calls remain. |
| `STAAdapter::refreshFullDesignTimingContext` | `STAAdapter.hh:117`; definition `STAAdapterTimingUpdate.cc:220` takes `const Config&`. | Stale no-arg test calls remain. |
| `STAAdapter::queryConfiguredClockRouteSegmentRc` | `STAAdapter.hh:106`; definition `STAAdapterRcTree.cc:160` takes `const Config&`. | Stale no-arg test/helper calls remain. |
| `STAAdapter::queryClockSourceDriveCapLimit` | `STAAdapter.hh:109`; definition `STAAdapterCellQuery.cc:208` takes `const Config&` plus `Pin*`. | One stale no-config test call remains. |
| `STAAdapter::queryPinSlewLimit` | `STAAdapter.hh:124`; definition `STAAdapterCellQuery.cc:296` takes `const Config&` plus `Pin*`. | No stale no-config call found; `FastStaBuilder.cc:102` is explicit. |
| `STAAdapter::installClockNetRcTree` | `STAAdapter.hh:121`; definition `STAAdapterRcTree.cc:172` takes `const Config&`. | No stale no-config call found; `QorEvaluationMetrics.cc:339` is explicit. |
| `STAAdapter::emitConfiguredUnitWireRcReport` | `STAAdapter.hh:131`; definition `STAAdapterWireRc.cc:99` takes reporter and config. | No stale no-reporter/no-config call found; `Setup.cc:117` is explicit. |
| `FastSTA::buildClockContext` | `FastSta.hh:219-220`; definitions `FastSta.cc:155`, `:183` take `Config&` and `Wrapper&`. | Current flow call `Optimization.cc:105` is explicit. No stale buildClockContext call found. Separate stale `FastSTA::getInst` / static method tests remain. |
| `CharacterizationLibrary::buildRuntimeOptions` | `CharacterizationLibrary.hh:63`; definition `CharacterizationLibrary.cc:107` takes config, wrapper, fast_sta, reporter. | Current callers `HTree.cc:166` and `SourceTrunkSegment.cc:380` are explicit. |

Stale call sites likely to fail after current signature changes:

| File | Line | Old call | Minimal fix |
| --- | ---: | --- | --- |
| `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc` | 461 | `STAAdapter::init()` | Use a fixture/local `Config&` loaded at lines 383/446; call `STAAdapter::init(config)`. |
| `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc` | 194 | `STAAdapter::init()` | Use the benchmark case `Config` loaded at lines 168-170; call `STAAdapter::init(config)`. |
| `src/operation/iCTS/test/module/characterization/fixture/CharacterizationRealTechFixture.cc` | 239 | `STAAdapter::init()` | Restore/apply a fixture-owned `ConfigState` into a `Config` object and pass it to `STAAdapter::init(config)`. |
| `src/operation/iCTS/test/module/characterization/CharacterizationRealTechLimitResolutionTest.cc` | 151 | `STAAdapter::init()` | Use `setup_state` / fixture-owned `Config` from real-tech setup; call `STAAdapter::init(config)`. |
| `src/operation/iCTS/test/module/characterization/CharacterizationRealTechLimitResolutionTest.cc` | 169, 418 | `STAAdapter::refreshFullDesignTimingContext()` | Pass the same explicit real-tech `Config&`: `refreshFullDesignTimingContext(config)`. |
| `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc` | 175 | `STAAdapter::queryConfiguredClockRouteSegmentRc()` | Pass the active runtime/fixture `Config&`; this helper likely needs a `const Config&` parameter. |
| `src/operation/iCTS/test/module/characterization/fixture/CharacterizationRealTechFixture.cc` | 159 | `STAAdapter::queryConfiguredClockRouteSegmentRc()` | Pass fixture `Config&` into `MakeRuntimeCharBuilderInitOptions` or its builder helper. |
| `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkMetrics.cc` | 177 | `STAAdapter::queryConfiguredClockRouteSegmentRc()` | Add `const Config& config` to `BuildBenchmarkConfig`; use it for both `buildFastClusteringElectricalConfig` and RC query. |
| `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc` | 444 | `STAAdapter::queryClockSourceDriveCapLimit(&source)` | Replace `CONFIG_INST.set_max_cap` with local `Config config; config.set_max_cap(0.23);` then call `queryClockSourceDriveCapLimit(config, &source)`. |

### Code patterns

Good patterns already available:

- Runtime object ownership is explicit in `CTSRuntime` (`CTSRuntime.hh:38-45`).
- Flow already threads runtime dependencies through major boundaries (`Flow.cc:156`, `:182`, `:201`, `:222`, `:251`, `:263`).
- Setup runtime initialization already owns config parsing and reporter opening (`Setup.cc:55-99`).
- HTree characterization now has explicit config/wrapper/fast_sta/reporter wiring (`CharacterizationLibrary.hh:63`, `CharacterizationLibrary.cc:107-141`, `HTree.cc:166`, `SourceTrunkSegment.cc:380`).

Remaining bad patterns:

- Central no-arg report helpers hide `SCHEMA_WRITER_INST` (`Schema.cc:490-538`).
- Old no-arg flow bridge hides `Design::getInst`, `Wrapper::getInst`, and `SchemaWriter::getInst` (`ClockDataRead.cc:46-48`).
- DB/adapter helpers still mutate/read global design through `DESIGN_INST` (`WrapperClockReader.cc:623-984`, `WrapperClockWriter.cc:171-231`, `DesignConversion.cc:223-501`, `ClockSizingAcceptedEdit.cc:79-156`).
- Config is still read from `CONFIG_INST` in source at `ClockTracePins.cc:325` and `DesignConversion.cc:142`.
- Tests still preserve/reset global config/design/wrapper/writer state heavily, especially `FlowDesignFixture.hh:81-118`, `CharacterizationRealTechFixture.cc:75-157`, and `FlowSdcTraceTest.cc`.

### Minimum executable fix list

1. **Remove the reporter singleton root.**
   Files: `Schema.hh:136`, `Schema.hh:215`, `Schema.cc:478`, `:492`, `:504`, `:527`, `:538`.
   Delete `SchemaWriter::getInst`, `SCHEMA_WRITER_INST`, and no-arg `schema::Emit*` overloads. Convert `appendStandaloneBlock` to use a file-local static mutex instead of locking the singleton writer. All report call sites should pass `schema::SchemaWriter&`.

2. **Delete old no-arg `ClockDataRead::read()` and thread reporter deeper.**
   Files: `ClockDataRead.cc:46-48`, `RealTechAssetLoader.cc:457`, `FlowSdcTraceTest.cc:178-782`, `HTreeRealTechScenario.cc:116`, `FastClusteringRealTechBenchmarkSetup.cc:209`.
   Existing flow already calls `ClockDataRead::read(design, wrapper, reporter)` at `Flow.cc:156`; update tests/helpers to pass fixture-owned `Design&`, `Wrapper&`, and `SchemaWriter&`.

3. **Make SDC/clock-trace reporting explicit.**
   Files: `SdcClockReader.cc:65`, `:70`, `:81`; `ClockTraceResolver.cc:120`; `ClockTraceReport.cc:172`, `:215`, `:231`; `WrapperClockReader.cc:646`, `:664`.
   Add reporter to `SdcClockReader`, `ClockTraceResolver`, clock-trace report functions, and `Wrapper::traceSdcClocks` / read options. Callers should pass the reporter from `ClockDataRead::read(...)`.

4. **Remove config singleton root and no-arg config report.**
   Files: `Config.hh:34`, `Config.hh:46`, `Config.hh:165`, `Config.cc:316-318`, `TopologyTest.cc:313`.
   Delete `CONFIG_INST` and `Config::getInst`; keep `Config` as runtime/fixture-owned value. Remove `emitRuntimeConfigReport(title)` and require `emitRuntimeConfigReport(reporter, title)`.

5. **Replace source `CONFIG_INST` reads with narrow parameters.**
   Files: `ClockTracePins.cc:325`, `DesignConversion.cc:142`.
   For clock trace, pass a narrow trace policy such as `{min_clock_target_sinks, max_fanout}` from setup/config adaptation. For design conversion/root-buffer insertion, pass configured `buffer_types` or a narrow `ClockDistributionConfig` from `Synthesis::run(config, ...)` through `ClockDistribution::prepare` into `DesignConversion::addRootBufferForSinkDomain`.

6. **Make wrapper read/write operate on explicit design and reporter.**
   Files: `Wrapper.cc:116`, `WrapperClockReader.cc:623-984`, `WrapperClockWriter.cc:169-231`.
   Pass `Design&` and `schema::SchemaWriter&` into `Wrapper::readClocks`, `Wrapper::readTraceClockTargets`, and `Wrapper::writeClocksDetailed` or bind them in operation-specific reader/writer constructors. Avoid replacing this with a wrapper-owned global design.

7. **Make design conversion and clock-sizing edit operate on explicit `Design&`.**
   Files: `DesignConversion.cc:223-501`, `ClockSizingAcceptedEdit.cc:79-156`.
   Add `Design&` to design-conversion helpers that allocate/index/commit CTS objects, and pass `Design&` from `Synthesis::run` / `Topology` / `ClockDistribution`. Add `Design&` to accepted-edit apply helpers from `Optimization::run`.

8. **Make design summary reporting explicit.**
   Files: `Design.cc:482`, `Design.cc:517`, `ClockDataRead.cc:122`, `FlowTest.cc:190`.
   Change to `Design::emitClockDistributionSummary(schema::SchemaWriter&, title)` or return table rows so `ClockDataRead`/tests emit through their reporter.

9. **Fix optimization report helper reporter plumbing.**
   Files: `OptimizationReport.hh:41-42`, `OptimizationReport.cc:69`, `:101`, `:110`, `:114`, `:116`, `:141`, caller lines `Optimization.cc:116`, `:129`, `:153`, `:168`, `:169`.
   Add `schema::SchemaWriter& reporter` to `EmitClockSummary` and `EmitClockProfile`; `Optimization::run` already has `reporter`.

10. **Convert tests/fixtures from singleton state to owned runtime state.**
    High-density files: `test/main.cc:50-78`, `TestArtifactIO.cc:190-296`, `FlowDesignFixture.hh:81-453`, `CharacterizationRealTechFixture.cc:75-273`, `FlowSdcTraceTest.cc`, `FlowWritebackTest.cc`, `ClockDAGTest.cc`, `TopologyTest.cc`, and `FastSTATest.cc:208-567`.
    Use fixture-owned `CTSRuntime` where flow-level state is needed; use local `Config`, `Design`, `Wrapper`, `FastSTA`, and `SchemaWriter` values for narrow unit tests. For stale STA adapter calls, pass the fixture/local `Config&`. For `FastSTATest`, replace `FastSTA::getInst()` and static `FastSTA::...` calls with a fixture-owned `FastSTA fast_sta` instance.

## External References

No external references were needed. This was an internal source scan only.

## Related Specs

- `.trellis/tasks/05-24-cts-reporter-config-explicit/prd.md`: active task goal and acceptance for removing `SCHEMA_WRITER_INST`, `SchemaWriter::getInst`, `CONFIG_INST`, and `Config::getInst`.
- `.trellis/tasks/05-24-cts-reporter-config-explicit/design.md`: explicit reporter/config boundary rules and config/input/summary classification.
- `.trellis/tasks/05-24-cts-reporter-config-explicit/implement.md`: staged migration notes and validation grep.
- `.trellis/tasks/05-24-cts-reporter-config-explicit/research/cts-singleton-scan.md`: earlier baseline scan; current dirty tree has fewer source-level reporter/config gaps but more signature-drift test blockers are now visible.
- `.trellis/spec/project-constraints.md`: iCTS hard constraints.
- `.trellis/spec/backend/database-guidelines.md`: current singleton/ownership rules; note this spec still documents older singleton boundaries and is partially superseded by the active desingleton acceptance.
- `.trellis/spec/backend/quality-guidelines.md`: dependency visibility and validation guidance.

## Caveats / Not Found

- The active task pointer is not set in Trellis runtime; this file was written only because the user supplied the exact task/research path.
- No build or long validation was run by request. Signature-drift findings are inferred from declarations and call-site text.
- No `FAST_STA_INST` or `STA_ADAPTER_INST` macro definition/use was found under `src/operation/iCTS`.
- Current backend database spec still permits several singleton roles, but the active task/user acceptance says only `CTSAPI` may remain.
