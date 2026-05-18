# Implementation Plan: CTS FastSTA and STAAdapter boundary refactor

## Phase 1: Planning and Evidence

- [x] Create the Trellis task.
- [x] Inspect FastSTA, STAAdapter char APIs, optimization legality, root slew handling, and `module/timing` users.
- [x] Update PRD/design based on revised scope:
  - keep FastSTA location;
  - keep STAAdapter dependency;
  - make dependency explicit;
  - cap legality is net cap;
  - slew legality is buffer/sink receiving-pin transition;
  - leave `module/timing` unchanged.

## Phase 2: Mechanical FastSTA Rename

- [x] Rename files:
  - `FastStaAdapter.hh` -> `FastSta.hh`;
  - `FastStaAdapter.cc` -> `FastSta.cc`.
- [x] Rename facade type:
  - `FastStaAdapter` -> `FastSTA`.
- [x] Rename macro:
  - `FAST_STA_ADAPTER_INST` -> `FAST_STA_INST`.
- [x] Update all includes in characterization and optimization.
- [x] Update CMake source list in `database/adapter/fast_sta/CMakeLists.txt`.
- [x] Update log messages from `FastStaAdapter` to `FastSTA`.
- [x] Build `icts_source_database_adapter_fast_sta` after the mechanical rename.

方案：纯机械改名，不搬目录，不改 CMake target 名，不改 FastSTA 内部算法。先把语义名称修正，降低后续行为变更和 rename 混在一起的排查成本。

## Phase 3: Make STAAdapter Dependency Explicit

- [x] Audit all FastSTA calls to `STA_ADAPTER_INST` and `sta_adapter_internal::*`.
- [x] Group FastSTA's STA-backed data access into clearly named local helpers or narrow STAAdapter query APIs:
  - Liberty snapshot and unit conversion;
  - wire RC query;
  - pin cap query;
  - source cap limit query;
  - cell cap/slew limit and table-axis fallback;
  - cell area/ports/power metadata.
- [x] Remove use of any stale char-runtime STAAdapter API from FastSTA if present.
- [x] Keep direct STAAdapter dependency where it reflects real data ownership.

方案：不新增“假解耦”的 provider 层。代码层面让调用名表达“这是 STA-backed tech/liberty data query”，避免 FastSTA 看起来依赖一个完整 char runtime 或另一个 timing engine。

## Phase 4: Remove Legacy STAAdapter Char Runtime Surface

- [x] Run full-repository search for each char-only STAAdapter API.
- [x] Remove unused public declarations from `STAAdapter.hh`.
- [x] Remove unused implementations from:
  - `STAAdapter.cc`;
  - `STAAdapterCharCircuit.cc`;
  - `STAAdapterCharTiming.cc`;
  - `STAAdapterCharPower.cc`;
  - related char-only state in `STAAdapter`.
- [x] Keep technology query APIs still used by CTS.
- [x] Rebuild STAAdapter and affected iCTS targets.

方案：删除的是旧 runtime surface，不删除真实数据查询能力。这样避免未来出现“CharBuilder 一半走 FastSTA、一半又回到 STA char circuit”的双路径。

## Phase 5: Fix Net-Cap Semantics Including Source Net

- [x] Preserve one cap status type: `FastStaCapStatus`.
- [x] Ensure source clock net exists in `context.nets` and participates in normal cap baseline/check traversal.
- [x] Initialize source net `max_cap_pf` through `STA_ADAPTER_INST.queryClockSourceDriveCapLimit(clock.get_clock_source())` when source-specific cap limit is available.
- [x] Keep buffer output net `max_cap_pf` initialized from driver cell output cap limit/table-axis fallback.
- [x] Keep runtime `max_cap` policy consistent with existing behavior.
- [x] Do not add a source-drive-cap-specific violation enum, status type, report table, or solver branch.

方案：把 source drive cap 落到 source boundary net 的 `max_cap_pf`，让现有 `queryCapStatus -> CollectCapBaseline -> CheckCapLegality` 自然覆盖。这样 cap 语义对 STA 和优化器都是同一套：net load cap 是否超过该 net 的 max cap。

## Phase 6: Split Slew Status by Receiving-Pin Role

- [x] Extend `FastStaSlewStatus` or associated data with a role field:
  - buffer input;
  - sink.
- [x] Keep buffer input max slew from Liberty input slew limit.
- [x] Initialize sink node max slew from a clearly named STA-backed pin/cell query or documented fallback.
- [x] Include sink slew statuses in baseline capture and legality checking.
- [x] Update reports/logs only enough to make buffer/sink slew violations readable.

方案：slew 还是同一个公式 `node.slew <= node.max_slew`，但状态要告诉人这是 buffer input transition 还是 sink pin transition。这样保留算法统一性，同时满足诊断可读性。

## Phase 7: Make Root Slew Explicit

- [x] Add root slew field to FastSTA context or char sample request.
- [x] Initialize normal clock contexts from `CONFIG_INST.get_root_input_slew()` at build boundary.
- [x] Change `FastStaTiming::update(...)` to read root slew from context/request data.
- [x] Change `FastStaChar::runSample(...)` so it no longer writes `CONFIG_INST.root_input_slew`.
- [x] Validate repeated char samples with different slews do not rely on global config mutation.

方案：root slew 是 sample/context 输入，不是全局 mutable state。普通 flow 的默认值仍来自 config，但只在 context 构建边界读取。

## Phase 8: Explicitly Leave `module/timing` Untouched

- [x] Do not rename `TimingEngine`.
- [x] Do not move `module/timing`.
- [x] Do not change cluster constraint or QoR TimingEngine users unless a build failure is directly caused by other edits.

方案：本 task 只处理 FastSTA/STAAdapter 边界和 legality 语义，避免把独立的 RCTree estimator 改名混进来。

## Phase 9: Validation

- [x] Build affected direct targets:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization
```

- [x] Build broader touched modules:

```bash
ninja -C build icts_source_module_characterization icts_source_flow_synthesis icts_source_flow_evaluation
```

- [x] Build `iEDA`:

```bash
ninja -C build iEDA
```

- [x] Run representative iCTS smoke that covers synthesis, characterization, optimization, final STA evaluation, and report:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- [x] Run final iCTS checker:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Review Checklist Before `task.py start`

- [x] Scope keeps FastSTA in place.
- [x] Scope keeps STAAdapter dependency but makes it explicit.
- [x] Scope removes stale STAAdapter char runtime APIs.
- [x] Cap status remains net-based and covers source net.
- [x] Slew status distinguishes buffer and sink receiving pins.
- [x] Root slew is explicit.
- [x] `module/timing` is untouched.
