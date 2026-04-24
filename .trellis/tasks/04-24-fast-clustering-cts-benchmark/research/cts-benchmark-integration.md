# Research: CTS clustering benchmark integration

- Query: Research CTS clustering benchmark integration for iCTS real-tech tests, focusing on 20 placement-stage DEF/Verilog cases, allowed ICS55 PDK/config inputs, test-target placement, `cts.log`/CSV artifacts, and avoiding non-placement configs.
- Scope: mixed
- Date: 2026-04-24

## Findings

### Files Found

- `.trellis/tasks/04-24-04-24-fast-clustering-cts-benchmark/prd.md` - task requirements and benchmark acceptance criteria.
- `.trellis/spec/project-constraints.md` - repository-wide iCTS constraints for files, logging, CMake, validation, and terminology.
- `.trellis/spec/backend/directory-structure.md` - iCTS source/test placement and CMake target naming rules.
- `.trellis/spec/backend/logging-guidelines.md` - runtime `LOG_*` and structured `cts.log` rules.
- `.trellis/spec/backend/quality-guidelines.md` - include, dependency visibility, and validation rules.
- `.trellis/spec/backend/database-guidelines.md` - singleton, ownership, and adapter-boundary rules.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - prompt for Wrapper/STAAdapter and unit/ownership boundary checks.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - prompt for reusing helpers and CMake patterns before adding new helpers.
- `src/operation/iCTS/test/CMakeLists.txt` - shared iCTS test target helper and real-tech base wiring.
- `src/operation/iCTS/test/module/topology/CMakeLists.txt` - topology test aggregation point.
- `src/operation/iCTS/test/module/topology/linear_clustering/realtech/CMakeLists.txt` - existing linear real-tech smoke/regression target pattern.
- `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc` - current ICS55 real-tech asset resolver and load path.
- `src/operation/iCTS/test/common/realtech/support/RealTechSetupSupport.cc` - static facade for existing single-design real-tech setup.
- `src/operation/iCTS/test/common/realtech/load/RealTechLoadFactory.cc` - helper that extracts load pins from the largest clock/regular net.
- `src/operation/iCTS/test/module/topology/linear_clustering/realtech/support/runtime/LinearClusteringRealTechRuntime.cc` - current real-tech output-dir and largest-clock-load extraction pattern.
- `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc` - existing `cts.log`, `report.log`, and CSV benchmark artifact pattern.
- `src/operation/iCTS/test/common/io/TestArtifactIO.cc` - common artifact output, mirroring, and output-root conventions.
- `src/operation/iCTS/test/common/logging/ScopedLogFile.cc` - scoped schema writer for `cts.log`.
- `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh` - shared `LinearClusteringConfig` constraints and algorithm knobs.
- `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.hh` - linear clustering public facade to mirror for fast clustering.
- `src/operation/iCTS/source/module/topology/clustering/Clustering.hh` - shared `ClusterResult` and electrical-summary output contract.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh` - allowed ICS55 flow source for PDK-derived LEF/Lib/SDC inputs.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json` - allowed CTS constraints for the benchmark.
- `/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008` - external benchmark tree inspected for placement-stage cases.

### Existing Test CMake Patterns

- `src/operation/iCTS/test/CMakeLists.txt:20` defines `icts_test_realtech_base`, and `src/operation/iCTS/test/CMakeLists.txt:22` links it to `icts_test_base` plus `idm`. Use the `REALTECH` option for any benchmark executable that reads real DEF/Verilog.
- `src/operation/iCTS/test/CMakeLists.txt:34` defines `icts_add_test_executable`; it creates the executable at `src/operation/iCTS/test/CMakeLists.txt:40`, links `icts_test_base` and `icts_test_common_io` at `src/operation/iCTS/test/CMakeLists.txt:41`, adds `icts_test_realtech_base` when `REALTECH` is passed at `src/operation/iCTS/test/CMakeLists.txt:43`, and registers the CTest at `src/operation/iCTS/test/CMakeLists.txt:47`.
- `src/operation/iCTS/test/common/CMakeLists.txt:3` through `src/operation/iCTS/test/common/CMakeLists.txt:8` already expose common `io`, `logging`, `linear_clustering`, and `realtech` helper subtrees.
- `src/operation/iCTS/test/module/topology/CMakeLists.txt:1` and `src/operation/iCTS/test/module/topology/CMakeLists.txt:2` currently add only `topology_gen` and `linear_clustering`.
- Existing linear real-tech tests live under `src/operation/iCTS/test/module/topology/linear_clustering/realtech`, with support/scenario subdirectories added at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/CMakeLists.txt:1` and `src/operation/iCTS/test/module/topology/linear_clustering/realtech/CMakeLists.txt:2`.
- The existing smoke target `icts_test_module_topology_linear_clustering_realtech` is defined at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/CMakeLists.txt:9` and links sweep scenario support at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/CMakeLists.txt:15`.
- Slow linear real-tech regression targets are gated by `ICTS_BUILD_SLOW_REALTECH_TESTS`, declared at `src/operation/iCTS/test/CMakeLists.txt:32` and used at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/CMakeLists.txt:24`.

Recommendation: add a new mirrored test subtree for the new subject module, not into the existing linear-only target:

- Add `add_subdirectory(fast_clustering)` to `src/operation/iCTS/test/module/topology/CMakeLists.txt`.
- Add benchmark code under `src/operation/iCTS/test/module/topology/fast_clustering/realtech` or `src/operation/iCTS/test/module/topology/fast_clustering/benchmark`.
- Add an executable such as `icts_test_module_topology_fast_clustering_realtech_benchmark` with `icts_add_test_executable(... REALTECH ...)`.
- Link benchmark-specific support plus `icts_test_common_logging_support`, `icts_test_common_linear_clustering_support`, `icts_test_common_realtech_support`, the existing linear clustering source target, and the new fast clustering source target.
- Because this benchmark reads 20 real designs and writes large artifacts, gate it with `ICTS_BUILD_SLOW_REALTECH_TESTS` unless the implementation adds a separate benchmark option.

### Existing Real-Tech Helpers and Reuse

- `RealTechAssetLoader.cc` recognizes `ICTS_REALTECH_WORKSPACE`, `ICTS_REALTECH_PDK_DIR`, and legacy `PDK_DIR` at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:80` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:82`.
- Default real-tech workspaces are `scripts/design/ics55_dev` and `scripts/design/ics55_gcd` at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:83` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:86`.
- Existing flow-script candidates are `script/iCTS_script/run_iCTS_dev.tcl` and `script/iCTS_script/run_iCTS.tcl` at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:87` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:90`.
- Existing config resolution uses `iEDA_config/cts_default_config.json` at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:91` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:93`.
- Existing ARM9 DEF/Verilog candidates are hardcoded at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:93` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:103`; this helper is not a multi-design benchmark selector.
- `BuildAssetsFromWorkspace` constructs PDK tech LEF, standard-cell LEFs, DEF, Verilog, Liberty, SDC, and output-dir paths at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:322` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:349`.
- `ValidateAssets` checks workspace, PDK root, flow script, CTS config, tech LEF, DEF, optional Verilog, SDC, LEFs, and Liberty files at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:382` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:430`.
- `LoadRealTechAssets` initializes `CONFIG_INST` from the CTS config at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:432` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:439`, reads tech/cell LEFs at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:440` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:454`, reads Verilog/DEF at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:456` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:469`, sets data-manager paths at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:472` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:492`, and initializes Wrapper/STA at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:504` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:514`.
- `EnsureRealTechSetup` returns a function-local static setup state at `src/operation/iCTS/test/common/realtech/support/RealTechSetupSupport.cc:40` through `src/operation/iCTS/test/common/realtech/support/RealTechSetupSupport.cc:44`; do not use it directly for a 20-design loop because it is intentionally single-shot.
- `EnsureLargestRealClockLoads` shows the clock extraction sequence: `DESIGN_INST.reset()`, `STA_ADAPTER_INST.updateTiming()`, collect clock/net pairs, add clocks, `WRAPPER_INST.read()`, query DBU, and choose the clock with the most loads at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/support/runtime/LinearClusteringRealTechRuntime.cc:90` through `src/operation/iCTS/test/module/topology/linear_clustering/realtech/support/runtime/LinearClusteringRealTechRuntime.cc:122`.
- `Wrapper::reset` clears borrowed iDB references and owned CTS-side pins/insts/nets at `src/operation/iCTS/source/database/io/Wrapper.hh:68` through `src/operation/iCTS/source/database/io/Wrapper.hh:83`.
- `STAAdapter::init` resets transient timing state and configures the STA workspace at `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:822` through `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:840`; `resetStaTransientState` resets RC, SDC, netlist, and graph state at `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:1522` through `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:1536`.
- `DataManager::initDef` replaces `_idb_def_service` via `_idb_builder->buildDef(def_path)` at `src/platform/data_manager/idm_init.cpp:41` through `src/platform/data_manager/idm_init.cpp:52`, and `DataManager::initVerilog` replaces `_idb_def_service` via `rustBuildVerilog` at `src/platform/data_manager/idm_init.cpp:55` through `src/platform/data_manager/idm_init.cpp:62`.

Implementation implication: build a benchmark-specific loader that reuses the existing path resolution and load sequence but accepts per-case placement DEF/Verilog. It should reset `DESIGN_INST` and `WRAPPER_INST`, initialize `CONFIG_INST` from the allowed ICS55 config, then load the case's placement Verilog and DEF, initialize Wrapper/STA, collect clocks, and log per-case unit/count stats before running either clustering algorithm.

### Allowed ICS55 Technology and Config Inputs

- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh:12` through `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh:21` documents the expected `PDK_DIR`, `TECH_LEF`, `LEF_STDCELL`, and `LIB_STDCELL` inputs.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh:29` through `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh:34` documents the workspace result/config/TCL/DEF/SDC paths.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh:45` selects the workspace netlist path, but the benchmark should use the external placement-stage Verilog for each selected case instead.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json:5` sets `max_cap` to `0.05`.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json:6` sets `max_fanout` to `32`.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json:7` through `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json:10` set routing layers `[4, 5]`.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json:11` through `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json:16` set buffer types `BUFX8H7L`, `BUFX12H7L`, `BUFX16H7L`, and `BUFX20H7L`.
- Current real-tech helper code expects `*_ecos.lef` files under the PDK root at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:333` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:337`, and the two Liberty files at `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:343` through `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:346`.
- On this machine, the following allowed PDK files exist under `/home/liweiguo/pdk/icsprout55-pdk`: `prtech/techLEF/N551P6M_ecos.lef`, `IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/lef/ics55_LLSC_H7CR_ecos.lef`, `IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/lef/ics55_LLSC_H7CL_ecos.lef`, and the two `*_ss_rcworst_1p08_125_nldm.lib` Liberty files.

Use only these allowed non-design inputs:

- CTS config: `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json`.
- SDC if STA needs one: `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/default.sdc`.
- PDK root: `ICTS_REALTECH_PDK_DIR`, `PDK_DIR`, or `/home/liweiguo/pdk/icsprout55-pdk`.
- PDK tech/cell LEF and Liberty files under that PDK root.

Do not use per-case benchmark configs such as `/nfs/.../place_dreamplace/config/cts_default_config.json`, even though they exist in the external tree.

### External Benchmark Tree and 20-Case Selection

Root inspected: `/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008`.

Observed shape:

- 45 direct `ics55_*` design directories.
- 37 valid placement-stage pairs where both `place_dreamplace/output/*_place.def` or `*_place.def.gz` and `place_dreamplace/output/*_place.v` exist.
- 8 directories without a complete placement pair: `ics55_00011`, `ics55_00016`, `ics55_00017`, `ics55_00102`, `ics55_00106`, `ics55_00107`, `ics55_00108`, and `ics55_00109`.
- All 37 valid pairs checked had matching DEF `DESIGN` and Verilog first `module` names.

Stable selector recommendation:

1. Iterate direct child directories of the benchmark root in lexicographic order.
2. Accept only paths whose parent is exactly `<case>/place_dreamplace/output`.
3. Prefer `*_place.def.gz` or `*_place.def` and require a matching `*_place.v` in the same output directory.
4. Verify the DEF design name and Verilog top module can be parsed and match.
5. Take the first 20 accepted cases and require exactly 20 when the external root is present.

Selected 20 cases from the inspected tree:

| Index | Case | DEF Design / Verilog Top | DEF | Verilog |
|---:|---|---|---|---|
| 1 | `ics55_00003` | `BM64` | `place_dreamplace/output/BM64_place.def.gz` | `place_dreamplace/output/BM64_place.v` |
| 2 | `ics55_00004` | `PPU` | `place_dreamplace/output/PPU_place.def.gz` | `place_dreamplace/output/PPU_place.v` |
| 3 | `ics55_00006` | `aes_cipher_top` | `place_dreamplace/output/aes_place.def.gz` | `place_dreamplace/output/aes_place.v` |
| 4 | `ics55_00008` | `arm9_compatiable_code` | `place_dreamplace/output/arm9_place.def.gz` | `place_dreamplace/output/arm9_place.v` |
| 5 | `ics55_00012` | `blake2s` | `place_dreamplace/output/blake2s_place.def.gz` | `place_dreamplace/output/blake2s_place.v` |
| 6 | `ics55_00014` | `bp_be_top` | `place_dreamplace/output/bp_be_top_place.def.gz` | `place_dreamplace/output/bp_be_top_place.v` |
| 7 | `ics55_00015` | `bp_fe_top` | `place_dreamplace/output/bp_fe_top_place.def.gz` | `place_dreamplace/output/bp_fe_top_place.v` |
| 8 | `ics55_00022` | `fpu` | `place_dreamplace/output/double_fpu_place.def.gz` | `place_dreamplace/output/double_fpu_place.v` |
| 9 | `ics55_00023` | `FFT` | `place_dreamplace/output/fft_place.def.gz` | `place_dreamplace/output/fft_place.v` |
| 10 | `ics55_00035` | `picorv32a` | `place_dreamplace/output/picorv32a_place.def.gz` | `place_dreamplace/output/picorv32a_place.v` |
| 11 | `ics55_00036` | `poly1305` | `place_dreamplace/output/poly1305_place.def.gz` | `place_dreamplace/output/poly1305_place.v` |
| 12 | `ics55_00051` | `salsa20` | `place_dreamplace/output/salsa20_place.def.gz` | `place_dreamplace/output/salsa20_place.v` |
| 13 | `ics55_00054` | `sha256` | `place_dreamplace/output/sha256_place.def.gz` | `place_dreamplace/output/sha256_place.v` |
| 14 | `ics55_00056` | `sm4_top` | `place_dreamplace/output/sm4_place.def.gz` | `place_dreamplace/output/sm4_place.v` |
| 15 | `ics55_00059` | `ysyx_23060170` | `place_dreamplace/output/ysyx_23060170_place.def.gz` | `place_dreamplace/output/ysyx_23060170_place.v` |
| 16 | `ics55_00060` | `ysyx_23060203` | `place_dreamplace/output/ysyx_23060203_place.def.gz` | `place_dreamplace/output/ysyx_23060203_place.v` |
| 17 | `ics55_00061` | `ysyx_23060229` | `place_dreamplace/output/ysyx_23060229_place.def.gz` | `place_dreamplace/output/ysyx_23060229_place.v` |
| 18 | `ics55_00062` | `ysyx_23060246` | `place_dreamplace/output/ysyx_23060246_place.def.gz` | `place_dreamplace/output/ysyx_23060246_place.v` |
| 19 | `ics55_00063` | `ysyx_24070003` | `place_dreamplace/output/ysyx_24070003_place.def.gz` | `place_dreamplace/output/ysyx_24070003_place.v` |
| 20 | `ics55_00065` | `ysyx_24100012` | `place_dreamplace/output/ysyx_24100012_place.def.gz` | `place_dreamplace/output/ysyx_24100012_place.v` |

### Benchmark Config and Algorithm Input Contract

- `LinearClusteringConfig` carries fanout, diameter, capacitance, ordering, splitting, router, root policy, scoring, exact-cap behavior, routing layer, and wire width at `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh:101` through `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh:130`.
- `ClusterResult` contains `clusters`, `centers`, and `electrical_summaries` at `src/operation/iCTS/source/module/topology/clustering/Clustering.hh:66` through `src/operation/iCTS/source/module/topology/clustering/Clustering.hh:71`.
- `LinearClustering` exposes `buildElectricalBaseConfig`, `runDefault`, and `run` at `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.hh:35` through `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.hh:44`.
- `LinearClustering::buildElectricalBaseConfig` assigns max fanout and cap at `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:356` through `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:363`.
- `LinearClustering::run` returns an empty result on empty inputs or illegal partition and logs outcome at `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:411` through `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:440`.

For the benchmark, build one shared base config per case from allowed CTS config:

- `max_fanout = CONFIG_INST.get_max_fanout()`; expected `32`.
- `max_cap = CONFIG_INST.get_max_cap()`; expected `0.05`.
- `routing_layer = CONFIG_INST.get_routing_layers().back()` or the same effective layer policy used by existing experiment code.
- `wire_width = CONFIG_INST.get_wire_width()`; if absent, leave `0.0` and let STA use tech default.
- Keep root policy, router kind, scoring strategy, and exact-cap behavior identical between linear and fast runs.

### Per-Case Unit/Count Stats to Log First

Existing real-tech reports already log actual load count, DBU per micron, bounding-box diameter, fanout sweep, and sweep candidates before sweep details at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/sweep/LinearClusteringRealTechSweepScenario.cc:341` through `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/sweep/LinearClusteringRealTechSweepScenario.cc:349`.

For the new 20-case benchmark, log a case inventory section to `cts.log` before any linear/fast comparisons. Recommended fields:

- case index, case directory, design/top module, DEF path, Verilog path.
- DEF is compressed flag and file size.
- `dbu_per_micron` from `WRAPPER_INST.queryDbUnit()`.
- iDB design counts: instances, nets, clocks collected, largest clock name/net, largest clock load count.
- largest clock bounding-box diameter in DBU, and optionally width/height/span.
- config snapshot: max fanout, max cap, routing layers, routing layer used, wire width, buffer type count/names.
- skip reason when a case has no clocks or load count is too small.

### Artifact and `cts.log` Conventions

- `ResolveOutputDir` uses `ICTS_TEST_OUTPUT_DIR` when present, otherwise an executable-adjacent `icts_test_output` directory at `src/operation/iCTS/test/common/io/TestArtifactIO.cc:304` through `src/operation/iCTS/test/common/io/TestArtifactIO.cc:319`.
- `ResolveLinearClusteringOutputDir` appends `linear_clustering` at `src/operation/iCTS/test/common/io/TestArtifactIO.cc:326` through `src/operation/iCTS/test/common/io/TestArtifactIO.cc:329`. For fast clustering, use a new benchmark-local root under `ResolveOutputDir() / "fast_clustering" / "realtech_benchmark"` or add a matching fast-clustering resolver if the implementation generalizes common IO.
- `SanitizeOutputName` normalizes artifact directory names at `src/operation/iCTS/test/common/io/TestArtifactIO.cc:262` through `src/operation/iCTS/test/common/io/TestArtifactIO.cc:286`.
- `PrepareCleanOutputDir` removes and recreates an output directory at `src/operation/iCTS/test/common/io/TestArtifactIO.cc:288` through `src/operation/iCTS/test/common/io/TestArtifactIO.cc:302`.
- `WriteRawTextLog` creates parent directories and writes raw text at `src/operation/iCTS/test/common/io/TestArtifactIO.cc:227` through `src/operation/iCTS/test/common/io/TestArtifactIO.cc:244`.
- `WriteTextLog` mirrors non-`cts.log` text artifacts into a sibling `cts.log` at `src/operation/iCTS/test/common/io/TestArtifactIO.cc:200` through `src/operation/iCTS/test/common/io/TestArtifactIO.cc:223` and `src/operation/iCTS/test/common/io/TestArtifactIO.cc:246` through `src/operation/iCTS/test/common/io/TestArtifactIO.cc:252`.
- `ScopedLogFile` opens the schema writer with `cts_log` metadata at `src/operation/iCTS/test/common/logging/ScopedLogFile.cc:34` through `src/operation/iCTS/test/common/logging/ScopedLogFile.cc:40` and closes it at `src/operation/iCTS/test/common/logging/ScopedLogFile.cc:42` through `src/operation/iCTS/test/common/logging/ScopedLogFile.cc:45`.
- The existing experiment names artifacts explicitly, including `cts.log`, `report.log`, and CSV files at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc:1813` through `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc:1821`.
- Existing experiment writes CSVs and `report.log` with `WriteExperimentArtifact` at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc:1828` through `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc:1833`, then opens `cts.log` and emits the summary report at `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc:1836` through `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc:1837`.

Recommended artifact set:

- `cts.log` - schema-formatted benchmark report; starts with case inventory/unit stats, then per-case comparison summaries, then aggregate runtime/quality analysis.
- `report.log` - plain text copy of the final benchmark summary, matching existing real-tech experiments.
- `cts_clustering_cases.csv` - one row per selected case with paths, top/design names, DBU, clock/load counts, and config summary.
- `cts_clustering_comparison.csv` - one row per case per algorithm, including runtime, cluster count, singleton count, max fanout observed, max diameter observed, cap/routing violations, route failures, total wirelength/score, and legality.
- `cts_clustering_ranking.csv` or `cts_clustering_aggregate.csv` - aggregate linear vs fast ranking and acceptance metrics.

### Avoiding Non-Placement Configs and Outputs

The external tree contains stage-local configs under paths such as `<case>/place_dreamplace/config/cts_default_config.json`, plus non-placement outputs under `CTS_ecc`, `legalization_dreamplace`, `route_ecc`, `filler_ecc`, `fixFanout_ecc`, `drc_ecc`, and `origin`. The benchmark should not read any of those for configuration or design input.

Guardrails:

- Treat the benchmark root as design-input-only.
- Accept only `case/place_dreamplace/output/*_place.def`, `case/place_dreamplace/output/*_place.def.gz`, and `case/place_dreamplace/output/*_place.v`.
- Require the output basename suffix `_place` and exact parent `place_dreamplace/output`.
- Reject any discovered path containing `/config/`, `/CTS_ecc/`, `/legalization_dreamplace/`, `/route_ecc/`, `/filler_ecc/`, `/fixFanout_ecc/`, `/drc_ecc/`, or `/origin/`.
- Initialize `CONFIG_INST` only from `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json`.
- Set `dmInst->get_config()` LEF/Lib/SDC paths from the allowed ICS55 PDK/workspace, not from benchmark-stage `config/*.json`.
- Log the allowed config path and the selected placement DEF/Verilog paths to `cts.log` so accidental non-placement usage is visible.

### Related Specs

- `.trellis/spec/project-constraints.md` - forbids `std::cout`/`printf` in iCTS, requires schema/report helpers for `cts.log`, and says CMake should be updated before new implementation files.
- `.trellis/spec/backend/directory-structure.md` - tests should mirror source structure; new modules need a local `CMakeLists.txt`, parent `add_subdirectory`, and target links.
- `.trellis/spec/backend/logging-guidelines.md` - use `LOG_*` for runtime logging and schema/report helpers for structured `cts.log`; dense summaries should use titled tables/detail blocks.
- `.trellis/spec/backend/database-guidelines.md` - keep iDB access inside Wrapper, iSTA access inside STAAdapter, and reset/borrowed-pointer boundaries clear.
- `.trellis/spec/backend/quality-guidelines.md` - dependencies should be target links, headers should be self-contained, and iCTS includes should not use relative traversal.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - this benchmark crosses iDB, Wrapper, Design, module, and STAAdapter boundaries; map types, units, ownership, validation, and logging before coding.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - reuse existing output, logging, artifact, and real-tech helpers where they fit; add benchmark-specific helpers only for multi-design selection/loading.

## Caveats / Not Found

- The external benchmark root is available on this machine now, but it is under `/nfs`. If unavailable in another environment, the benchmark should create `cts.log` with an explicit skipped/unavailable reason and call `GTEST_SKIP()`.
- Current `RealTechAssetLoader` is single-design and ARM9-oriented. Its static `EnsureRealTechSetup` facade should not be the 20-case benchmark loader.
- There is no obvious public `dmInst` reset API. Repeated `readVerilog`/`readDef` calls appear to replace the DEF service, but the implementation should validate that per-case loading does not leak stale iDB/STA state. If that is not reliable, run cases with process isolation or add a narrowly scoped reset path.
- `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh` names `*_ieda.lef`, while the current real-tech helper and the local PDK files found on this machine use `*_ecos.lef`. Resolve this explicitly in implementation instead of silently falling back to non-allowed paths.
- I did not find an existing fast-clustering test subtree because the fast module has not been added yet.
- I did not find a generic common CSV writer; existing tests build CSV with `std::ostringstream` and write it with artifact IO helpers.
