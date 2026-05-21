# M2 · CharBuilder Pimpl 拆分设计

> Status: in-progress (2026-05-20)
> Scope: PRD §3 M2 only.

## 1. 起点 baseline (origin/cts_refactor + M4 + M1)

```
src/operation/iCTS/source/module/characterization/
├── buffer_cell/
│   └── CharacterizationBufferCell.hh                  (origin POD, 不动)
├── builder/
│   ├── CharBuilder.hh                190 lines (mega: 5 nested + 32 data + 16 priv methods + 23 inline get_*)
│   ├── CharBuilder.cc                28  lines (TU anchor only)
│   ├── CharBuilderConfig.cc          520 lines  → init()
│   ├── CharBuilderBuild.cc           212 lines  → build()
│   ├── CharBuilderFeasibility.cc     100 lines  → findCharacterizationBufferCell / calcClockRouteWireCapPf / analyzePatternFeasibility
│   ├── CharBuilderTopology.cc        81  lines  → buildTopologyDesc
│   └── CharBuilderSweepState.hh      80  lines  → BuildProgress / TopologyBits / TopologyDesc / StoredSampleIndices / PatternFeasibility
├── circuit/
│   └── CharBuilderCircuit.cc         124 lines  → createCharCircuit / setCharParasitics / destroyCharCircuit
├── pattern/
│   ├── PatternCombiner.hh                       (不动)
│   ├── CharBuilderPatternEnumeration.cc 172 lines → calcTopologySlotCount / countSelectedSlots / estimatePatternCountPerWirelength / enumerateWirelength / enumerateTopology / getMonotonicComboCount / advanceToNextMonotonic
│   └── CharBuilderPatternStorage.cc  65  lines  → storeBufferingPattern
├── pruning/
│   ├── Frontier.hh                              (origin 保留命名, 不动)
│   ├── HashJoinEngine.hh                        (origin 保留 shim, 不动)
│   ├── HTreeTraits.hh                           (不动)
│   └── SegmentTraits.hh                         (不动)
├── sampling/
│   ├── CharBuilderSampleStorage.cc   65  lines  → tryMakeStoredSampleIndices
│   ├── CharBuilderSampling.cc        79  lines  → characterizeTopology
│   ├── CharBuilderSlewSampling.cc    78  lines  → sampleLoadSlews
│   └── CharBuilderStaSampling.cc     103 lines  → sampleFeasibleTopology
└── table/                                       (不动)
```

11 个 chapter slicing .cc 文件分散在 builder/circuit/pattern/sampling 4 个子目录。CharBuilder.hh 含 5 个 private nested types (`BuildProgress`, `TopologyBits`, `TopologyDesc`, `StoredSampleIndices`, `PatternFeasibility` — 都在 `CharBuilderSweepState.hh` 内 friend-attach) + 32+ private data members + 16 private methods + 23 inline `get_*()` accessors.

## 2. 终点目标 (保留 origin 子目录拓扑)

```
src/operation/iCTS/source/module/characterization/
├── buffer_cell/                                (不动)
├── builder/
│   ├── CharBuilder.hh                ≤ 120 lines (Pimpl 公开门面)
│   ├── CharBuilder.cc                ~200 lines (薄转发: ctor/dtor/move + init/build + 23 accessor)
│   ├── CharBuilderSweepState.hh                (删除——nested types 迁到 CharBuilderImpl.hh)
│   ├── CharBuilderImpl.hh            Pimpl 聚合 (data + nested types + 9 components + accessors)
│   ├── CharBuilderImpl.cc            ctor (wire 9 components) / dtor
│   ├── CharSetupConfigurator.hh/.cc  替代 CharBuilderConfig.cc
│   ├── CharBuildOrchestrator.hh/.cc  替代 CharBuilderBuild.cc
│   ├── CharFeasibilityChecker.hh/.cc 替代 CharBuilderFeasibility.cc
│   └── CharTopologyPlanner.hh/.cc    替代 CharBuilderTopology.cc
├── circuit/
│   └── CharCircuitBuilder.hh/.cc     替代 CharBuilderCircuit.cc
├── pattern/
│   ├── PatternCombiner.hh                       (不动)
│   ├── CharPatternEnumerator.hh/.cc  替代 CharBuilderPatternEnumeration.cc
│   └── CharPatternStorage.hh/.cc     替代 CharBuilderPatternStorage.cc
├── pruning/                                    (不动: Frontier.hh / HashJoinEngine.hh / HTreeTraits.hh / SegmentTraits.hh)
├── sampling/
│   ├── CharStaSampler.hh/.cc         合并 4 chapter (Sampling + StaSampling + SlewSampling + SampleStorage)
└── table/                                      (不动)
```

**关键差异 vs. archived HEAD `915468e2a` 的 W3b**：
- HEAD 把所有 8 组件 + Impl 挤进 `detail/` 单一子目录。本任务**保留 origin 子目录拓扑**：CharBuilderImpl + 4 builder/* 组件放 builder/，CharCircuitBuilder 放 circuit/，CharPatternEnumerator + CharPatternStorage 放 pattern/，CharStaSampler 放 sampling/。
- HEAD 把 `CharacterizationBufferCell` 重命名成内部 `CharBufferInfo`。origin 保留 `CharacterizationBufferCell`（公开 buffer_cell/ 头）+ 嵌入 `InitOptions.characterization_buffer_cells`（由调用方 `CharacterizationLibrary::buildRuntimeOptions` 注入）。本任务**沿用 origin 数据流**——不动 InitOptions 字段、不引入 STAAdapter 直接查询。
- HEAD 把 `TopologyDesc` 重命名 → `BufferingPlan`。origin 保留 `TopologyDesc`。本任务沿用 `TopologyDesc`。
- HEAD 用 `_clock_route_segment_rc = STA_ADAPTER_INST.queryRequiredWireCapacitance(...)` 实时查询。origin 把 `ClockRouteSegmentRc` 嵌入 `InitOptions` + 内部 `calcClockRouteWireCapPf()` 用乘法。本任务沿用 origin。

## 3. 组件类设计

### 3.1 `CharBuilderImpl` (Pimpl 内核 in `icts::char_builder::detail`)

- Namespace: `icts::char_builder::detail`
- 持有 (lifted verbatim from CharBuilder.hh + CharBuilderSweepState.hh):
  - 5 nested types: `BuildProgress` / `TopologyBits` / `TopologyDesc` / `StoredSampleIndices` / `PatternFeasibility`
  - 1 constexpr: `kCapFeasibilityEpsilonPf`
  - 32+ data members: `_sorted_buffers` / `_wirelength_indices` / `_wirelengths_um` / `_slews_to_test` / `_loads_to_test` / `_routing_layer` / `_wire_width` / `_clock_route_segment_rc` / `_max_slew` / `_max_cap` / `_max_length` / `_length_unit_um` / `_wirelength_unit_source` / `_wirelength_unit_detail` / `_slew_steps` / `_cap_steps` / `_wirelength_iterations` / `_source_inst_name` / `_source_in_pin` / `_sink_inst_name` / `_source_out_pin` / `_sink_in_pin` / `_timing_observation_pin` / `_sink_input_cap_pf` / `_temp_inst_names` / `_temp_net_names` / `_char_clock_name` / `_char_circuit_id` / `_fast_sta_char_context_id` / `_segment_chars` / `_buffering_patterns` / `_next_pattern_id` / `_executed_sta_samples` / `_skipped_sta_samples` / `_output_slew_overflow_samples` / `_driven_cap_overflow_samples` / `_driven_cap_overflow_load_points` / `_max_observed_output_slew_ns` / `_max_observed_output_slew_idx` / `_max_observed_driven_cap_pf` / `_max_observed_driven_cap_idx`
  - 9 `std::unique_ptr<CharXxx>` (components)
  - 9 `friend class CharXxx;` declarations
- 公开方法：
  - ctor / dtor (out-of-line for unique_ptr<incomplete>)
  - 9 component accessors (返回 `CharXxx&`)
  - State accessors (called from CharBuilder.cc 薄转发): segmentChars / bufferingPatterns / wirelengthsUm / wirelengthIndices / wirelengthUnitUm / wirelengthUnitSource / wirelengthUnitDetail / wirelengthIterations / maxSlew / maxCap / slewSteps / capSteps / routingLayer / wireWidth / clockRouteSegmentRc / characterizationBufferCells / executedStaSamples / skippedStaSamples / outputSlewOverflowSamples / drivenCapOverflowSamples / drivenCapOverflowLoadPoints / maxObservedOutputSlewNs / maxObservedOutputSlewIdx / maxObservedDrivenCapPf / maxObservedDrivenCapIdx

### 3.2 `CharSetupConfigurator` (builder/, 替代 CharBuilderConfig.cc)

- `init(const ::icts::CharBuilder::InitOptions& options) -> void`
- Owns 所有 namespace-local helpers: `collectSortedBuffers` / `resolveMax{Slew,Cap}` / `resolveWirelengthUnitUm` / `resolveRoutingLayer` / `resolveWireWidth` / `resolveClockRouteSegmentRc` / `makeDenseWirelengthIndices` / `normalizeWirelengthIndices` / `clampWirelengthIndices` / `makeWirelengths` / `ResolvedValue` / `ResolutionSource` enum
- 写入 `_impl._sorted_buffers` / `_impl._wirelengths_um` / `_impl._wirelength_indices` / `_impl._max_slew` / `_impl._max_cap` / `_impl._length_unit_um` / `_impl._routing_layer` / `_impl._wire_width` / `_impl._clock_route_segment_rc` / `_impl._slews_to_test` / `_impl._loads_to_test` / 各种 limit/unit fields

### 3.3 `CharBuildOrchestrator` (builder/, 替代 CharBuilderBuild.cc)

- `build() -> void`
- 调用：`_impl.patternEnumerator().estimatePatternCountPerWirelength()` / `enumerateWirelength()`
- 写入：sweep statistics / progress rows / schema tables

### 3.4 `CharPatternEnumerator` (pattern/, 替代 CharBuilderPatternEnumeration.cc)

- 公开：`calcTopologySlotCount` / `static countSelectedSlots` / `estimatePatternCountPerWirelength` / `enumerateWirelength`
- private：`enumerateTopology` / `static getMonotonicComboCount` / `static advanceToNextMonotonic`
- 调用：`_impl.topologyPlanner().buildTopologyDesc()` / `_impl.staSampler().characterizeTopology()`

### 3.5 `CharTopologyPlanner` (builder/, 替代 CharBuilderTopology.cc)

- `buildTopologyDesc(double wirelength_um, unsigned num_slots, TopologyBits topology_bits) const -> TopologyDesc`
- Owns namespace-local helper `hasTerminalLatticeBuffer`
- 读取：`_impl._length_unit_um`

### 3.6 `CharFeasibilityChecker` (builder/, 替代 CharBuilderFeasibility.cc)

- 公开：`findCharacterizationBufferCell(const std::string& cell_master) const -> const CharacterizationBufferCell*` / `calcClockRouteWireCapPf(double wirelength_um) const -> double` / `analyzePatternFeasibility(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) const -> PatternFeasibility`
- 读取：`_impl._sorted_buffers` / `_impl._clock_route_segment_rc`

### 3.7 `CharPatternStorage` (pattern/, 替代 CharBuilderPatternStorage.cc)

- `storeBufferingPattern(unsigned length_idx, const TopologyDesc& topo, const std::vector<std::string>& buf_masters, double total_length_um) -> PatternId`
- 写入：`_impl._buffering_patterns` / `_impl._next_pattern_id`

### 3.8 `CharCircuitBuilder` (circuit/, 替代 CharBuilderCircuit.cc)

- 公开：`createCharCircuit` / `setCharParasitics` / `destroyCharCircuit`
- 读写：`_impl._temp_inst_names` / `_impl._temp_net_names` / `_impl._fast_sta_char_context_id` / `_impl._char_circuit_id` / `_impl._source_inst_name` 等 + FastSTA::buildCharContext / setCharLoad / eraseCharContext
- 调用：`_impl.feasibilityChecker().findCharacterizationBufferCell(...)`

### 3.9 `CharStaSampler` (sampling/, 合并 4 chapter)

- 公开：`characterizeTopology(unsigned length_idx, const TopologyDesc& topo, const std::vector<std::string>& buf_masters, BuildProgress& build_progress) -> void`
- private 内部 pipeline (was 3 chapter .cc):
  - `sampleFeasibleTopology(...)` (was StaSampling.cc)
  - `sampleLoadSlews(...)` (was SlewSampling.cc)
  - `tryMakeStoredSampleIndices(...) const` (was SampleStorage.cc)
- 调用：`_impl.patternStorage().storeBufferingPattern()` / `_impl.feasibilityChecker().analyzePatternFeasibility() / calcClockRouteWireCapPf() / findCharacterizationBufferCell()` / `_impl.circuitBuilder().createCharCircuit() / setCharParasitics() / destroyCharCircuit()` + `FastSTA::runCharSample()`
- 决策：**合并为 1 个 CharStaSampler**（与 archived W3b 一致）——4 chapter 是同一 STA sampling pipeline 的连续阶段，行数预估 ~280（约束 < 600 OK）

### 3.10 `CharBuilder` (公开门面, builder/CharBuilder.hh)

- 公开 API 保持完全不变（InitOptions struct + 23 inline accessors）
- 仅持有 `std::unique_ptr<char_builder::detail::CharBuilderImpl> _impl`
- ctor/dtor/move-ctor/move-assign 全部 out-of-line for unique_ptr<incomplete>
- inline `get_*()` 改为 out-of-line 薄转发到 `_impl->...`
- **CharBuilder 不再可 copy**（因 unique_ptr）；CharacterizationLibrary 用 `_char_builder = CharacterizationLibrary{}` reassign 是 move-assign，需要给 CharBuilder 显式 `= default` move-ops

## 4. 跨组件协作依赖图

```
CharBuilder (facade)
    └─> CharBuilderImpl
            ├─> CharSetupConfigurator (init)
            │       └─> writes impl state
            ├─> CharBuildOrchestrator (build)
            │       ├─> CharPatternEnumerator
            │       │       ├─> CharTopologyPlanner.buildTopologyDesc
            │       │       └─> CharStaSampler.characterizeTopology
            │       │               ├─> CharPatternStorage.storeBufferingPattern
            │       │               ├─> CharFeasibilityChecker.analyzePatternFeasibility/calcClockRouteWireCapPf
            │       │               └─> CharCircuitBuilder.createCharCircuit/setCharParasitics/destroyCharCircuit
            │       │                       └─> CharFeasibilityChecker.findCharacterizationBufferCell
            │       │                       └─> FastSTA::buildCharContext/setCharLoad/eraseCharContext
            │       │                       └─> FastSTA::runCharSample
            │       │
            │       └─> writes sweep statistics
            └─> 9 state accessors → CharBuilder out-of-line getters
```

## 5. 命名空间与 include 策略

- Namespace: `icts::char_builder::detail`（参考 M1 的 `icts::bst::detail`）
- CharBuilder.hh include 收敛: 仅 `<memory>` + `<cstddef>` + `<optional>` + `<string>` + `<vector>` + `"BufferingPattern.hh"` + `"PatternId.hh"` + `"SegmentChar.hh"` + `"ValueLattice.hh"` + `"characterization/buffer_cell/CharacterizationBufferCell.hh"` + `"routing/ClockRouteSegmentRc.hh"` + 前向声明 `class CharBuilderImpl;`
- CharBuilderImpl.hh include 全部需要的 (CharBuilder.hh, FastSta.hh 等)
- 每个 component .hh 仅前向声明 + 必要 `<vector>` / `<string>` / `<optional>`
- 每个 component .cc 包含本 component .hh + CharBuilderImpl.hh + 其他需要的

## 6. 实施顺序

1. (a) builder/CharBuilderImpl.{hh,cc} 落地 + private nested 迁移 → build
   - 把 CharBuilderSweepState.hh 内的 5 nested types 迁移到 CharBuilderImpl.hh
   - 把 32+ data members 从 CharBuilder.hh 迁移到 CharBuilderImpl
   - CharBuilder.hh 暂时保留 public API 不变，但内部用 `_impl` 转发
   - 删除 CharBuilderSweepState.hh
2. (b) 逐个迁移 chapter → component（每步 build）
   - CharSetupConfigurator (builder/) ← CharBuilderConfig.cc
   - CharBuildOrchestrator (builder/) ← CharBuilderBuild.cc
   - CharFeasibilityChecker (builder/) ← CharBuilderFeasibility.cc
   - CharTopologyPlanner (builder/) ← CharBuilderTopology.cc
   - CharCircuitBuilder (circuit/) ← CharBuilderCircuit.cc
   - CharPatternEnumerator (pattern/) ← CharBuilderPatternEnumeration.cc
   - CharPatternStorage (pattern/) ← CharBuilderPatternStorage.cc
   - CharStaSampler (sampling/) ← 4 chapter merged
3. (c) CharBuilder.hh / CharBuilder.cc 收敛 → build
4. (d) 更新 CMakeLists.txt → build
5. CharacterizationLibrary `_char_builder = CharacterizationLibrary{}` 处补 explicit move ops（如需要）

## 7. 完成后

(实际数字)

### 文件清单

| 文件 | 行数 | 角色 |
|---|---:|---|
| `builder/CharBuilder.hh` | **120** | Pimpl 公开门面 (target ≤ 120 OK) |
| `builder/CharBuilder.cc` | 171 | 薄转发 (4 lifecycle + 2 main + 25 accessor) |
| `builder/CharBuilderImpl.hh` | 233 | Pimpl 聚合 (5 nested + 32 data + 8 components + 25 accessor) |
| `builder/CharBuilderImpl.cc` | 95 | 9-component wiring + 8 accessor |
| `builder/CharSetupConfigurator.hh / .cc` | 50 / 524 | init() (sweep-grid setup) |
| `builder/CharBuildOrchestrator.hh / .cc` | 47 / 214 | build() (wirelength sweep) |
| `builder/CharFeasibilityChecker.hh / .cc` | 59 / 102 | findCharacterizationBufferCell + calcClockRouteWireCapPf + analyzePatternFeasibility |
| `builder/CharTopologyPlanner.hh / .cc` | 48 / 82 | buildTopologyDesc |
| `circuit/CharCircuitBuilder.hh / .cc` | 53 / 127 | createCharCircuit + setCharParasitics + destroyCharCircuit |
| `pattern/CharPatternEnumerator.hh / .cc` | 61 / 176 | 4 公开 + 3 私有 enumeration |
| `pattern/CharPatternStorage.hh / .cc` | 56 / 66 | storeBufferingPattern |
| `sampling/CharStaSampler.hh / .cc` | 68 / 223 | characterizeTopology + sampleFeasibleTopology + sampleLoadSlews + tryMakeStoredSampleIndices (4 chapter 合并为 1) |

### Chapter slicing 残留

`find ... -name 'CharBuilder*{Build,Config,Feasibility,Topology,Circuit,Pattern*,Sampling,SampleStorage,StaSampling,SlewSampling}.cc'` = **0** ✓

`grep -c 'auto CharBuilder::' *.cc` in builder/circuit/pattern/sampling subdirs = **0** (除 builder/CharBuilder.cc 的 31 个薄转发) ✓

### Sampling 合并方案

合并为 **1 个 `CharStaSampler`**（4 chapter `Sampling.cc / SlewSampling.cc / StaSampling.cc / SampleStorage.cc` 合并）—— 224 行 (.cc) 满足 < 600 约束，且 4 个方法是同一线性 STA sampling pipeline (characterizeTopology → sampleFeasibleTopology → sampleLoadSlews → tryMakeStoredSampleIndices) 的连续阶段。

### Build 状态

- `bash build.sh` exit 0 ✓
- iEDA binary linked PASS（52 MB at `bin/iEDA`） ✓
- M4 `SingletonRegistryTest` 5/5 PASS ✓
- M1 `icts_source_module_routing_bst` 不动 ✓

### origin vs archived HEAD W3b 差异点

1. **目录拓扑**：本任务保留 origin 的 `builder/circuit/pattern/sampling/pruning/table/buffer_cell` 7 子目录拓扑，新组件按业务语义入位（HEAD 把全部组件挤进 `detail/` 单一子目录是错误的；本任务尊重 origin）。
2. **公开数据流**：本任务保留 origin 的 `CharacterizationBufferCell`（公开在 `buffer_cell/`）+ `InitOptions.characterization_buffer_cells` + `InitOptions.clock_route_segment_rc`（HEAD 用内部 `CharBufferInfo` + 实时 STA_ADAPTER_INST 查询）。
3. **数据类型命名**：本任务保留 origin 的 `TopologyDesc` 命名（HEAD 改名为 `BufferingPlan`）；保留 origin 的 `findCharacterizationBufferCell` / `calcClockRouteWireCapPf` 方法名（HEAD 改名为 `findBufferInfo` / 走 STAAdapter）。
4. **Pruning 子目录**：保留 origin 的 `pruning/Frontier.hh` / `pruning/HashJoinEngine.hh`（HEAD 改名为 `ParetoFront.hh` / 删除 shim）—— 不触碰。
5. **Namespace**：与 archived W3b / M1 一致用 `icts::char_builder::detail`（M1 用 `icts::bst::detail`）。
6. **CharBuilder.hh 公开 API**：保留 origin 已有的 `get_clock_route_segment_rc()` 和 `get_characterization_buffer_cells()` getters（HEAD 没有）—— 没影响其他模块。

