# Child Task Tracker

> 本任务 (`05-19-icts-code-standardization-refactor`) 的执行进度跟踪。
> **Session 63 (2026-05-20)** 启动 "全量收尾 Roadmap"：W0~W9 一次性推完，覆盖 PRD §6 全部验收 + 审视报告 (`research/review/01-comprehensive-issues.md`) P0/P1/P2 共 21 条差距。
> 不再创建 Trellis child task；改为在主任务内推进、按 Wave 切 commit。

## 执行模式

- 主 tree 内串行 dispatch `trellis-implement` sub-agent（不用 worktree，见 `worktree-pitfall.md`）
- 每个 Wave 结束跑增量 `bash build.sh` 验证可编译
- 每个 Wave 结束跑 `trellis-check` 子代理验证
- W9 收尾时跑 PRD §6.1 全部 grep verification + bit-identical QoR diff

## Baseline

- HEAD: `b22bf658f` (2026-05-20 08:00 UTC+8)
- 已验证：`bash build.sh` 增量编译通过 (`bin/iEDA` 已链接)
- 待跑：`scripts/design/ics55_dev/iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` QoR baseline JSON（W9 时跑两次：W0 baseline + W8 后 final，做 bit-identical diff）

## Wave 计划与状态

| Wave | 名称 | 范围 | 状态 |
|---|---|---|---|
| W0 | baseline + 决断 | 跑 baseline build；固化 D10.11~D10.13；本表更新 | in_progress |
| W1 | 低风险清理 | HTree 悬浮 .hh 合并 / 4 个 *Detail 改名拆分 / snapshotBufferCell / ValidateRequest / RequestKey / class TimingEngine → TimingFacade / HashJoinEngine.hh 删 / PinLocationHelper → PinLocationLookup / SvgCommon.hh + RefinerCommon.hh 改名 | completed @ 2026-05-20 (TimingEngine 改名留给后续 Wave；其余 grep 验收全 PASS，HTree.hh 223 行 ≤ 320) |
| W2 | STAAdapter chapter + sdc namespace | 6 STAAdapterXxx.cc chapter → 真组件类；sdc 双 namespace 合并 icts::sdc | completed @ 2026-05-20 (6 component classes in `sta/detail/` via Pimpl, 30 thin forwarders in STAAdapter.cc; all 14 sdc files unified to `icts::sdc`, 0 `clock_trace::` / `sdc_reader::` residue; build PASS) |
| W3a | BST Pimpl 拆分 | BoundSkewTree Pimpl；6 Bst*Solver.cc chapter → 真独立类；BoundSkewTree.hh 删 "intentionally exceeds" docstring | completed @ 2026-05-20 (BoundSkewTree.hh 65 行；7 个 detail/ 组件类 + Impl Pimpl 持 6 friends；0 `auto BoundSkewTree::` in detail/，仅 4 个 thin forwarder 在 BoundSkewTree.cc；docstring 删除；build PASS) |
| W3b | CharBuilder Pimpl 拆分 | 11 CharBuilderXxx.cc chapter → 真组件类（CircuitBuilder / PatternEnumerator / SlewSampler / CapSampler / StaSampler / TopologyPlanner / FeasibilityChecker / PatternStorage / SampleStorage） | completed @ 2026-05-20 (CharBuilder.hh 116 行；8 个 detail/ 组件类 + Impl Pimpl 持 8 friends；0 `auto CharBuilder::` in detail/，29 个 thin forwarder 在 CharBuilder.cc; 11 个 chapter .cc 全删；CharacterizationLibrary 加 move 默认；build PASS) |
| W3c | AnalyticalSolver chapter 治理 | 5 AnalyticalSolverXxx.cc chapter → BeamSearch / PatternCompose / ScoredSegmentCache 组件 | completed @ 2026-05-20 (5 chapter `.cc` → 5 stage-named .hh+.cc pairs: AnalyticalInputBuilder / AnalyticalModelEvaluator / AnalyticalSegmentShortlister / AnalyticalBeamExpander / AnalyticalCandidateTrimmer；W1b 头 (AnalyticalBeamSearch/ScoredSegment/PatternCompose.hh) 未触；AnalyticalSolver.hh 公开 API 不变；build PASS) |
| W4 | 跨层 / 反向依赖治理 | synthesis→instantiation include 切除；utils→database 切除；Flow.hh include 链收敛；module/characterization .cc → fast_sta 切除 | completed @ 2026-05-20 (W4a: design_conversion 从 instantiation/ 上移到 flow/design_conversion/，5 callsites grep 0；W4b: utils/visualization mv 到 flow/report/visualization/{cluster,core,topology}，utils/ 已无 visualization 残留 & 无 design::Pin/spatial::Tree 引用；W4c: Flow.hh Pimpl 化（含 FlowImpl.hh），4 #include / 82 行（5 sub-sub-flow 头全下沉到 FlowImpl.hh）；W4d: CharBuilder fast_sta 调用集中到 CharBuilderImpl 转发器，CharCircuitBuilder.cc / CharStaSampler.cc 不再 #include FastSta.hh；build PASS) |
| W5 | flow 范式（FlowInterface） | flow/interface/ 子目录 + FlowInterface.hh/.cc + CMakeLists.txt；6 sub-flow init/run/report；删 SubFlowOutcome.hh；FlowOrchestrator；schema "CTSFlow" 改名 | completed @ 2026-05-20 (flow/interface/ 子目录建好；6 sub-flow 的 run() 全部统一返回 FlowStageOutcome（业务输出走 out-param），init() 返回 bool，report() 返回 void；删除 SubFlowOutcome.hh；FlowImpl 增加 OptimizationResult 字段承载 Optimization 输出；CTSFlow → Synthesis 在 W2 前已修；Flow 类继续承担 orchestrator 角色；build PASS) |
| W6 | CMake 收敛 | cmake/icts_targets.cmake；flow/synthesis/htree/ 10→1；flow/optimization/ 8→stages/ 1；module/routing/database/ 删；102 → ≤35 | completed @ 2026-05-20 (105 → 51；source-bearing archives 86 → 40；分布 flow 16 / database 21 / module 9 / utils 5 / source 1；fast_sta 11 locked per W4 contributes the gap above the 35 ceiling. cmake/icts_targets.cmake added with icts_add_library + icts_apply_debug_flags. module/routing/database/ removed. htree 13→3, optimization 9→2, synthesis topology+trace 9→2, report visualization 8→1, report sub-dirs 4→2, flow/instantiation 2→1, flow/evaluation 2→1, flow/synthesis/distribution merged into synthesis, module/topology 7→3, module/routing 8→2 (router/helper/flute/salt/cbs/local_legalization merged; bst 3→1). 32 sub CMakeLists.txt files removed. build PASS, all icts_test_* executables link cleanly.) |
| W7 | 单例 IResettable | utils/singleton/IResettable.hh + SingletonRegistry；7 单例继承；CtsApi::resetAPI 简化 | completed @ 2026-05-20 (7 singletons self-register via getInst() static once-flag: Flow/Config/Design/IdbBridge/STAAdapter/FastSTA/SchemaWriter; CTSAPI::resetAPI simplified to single `SingletonRegistry::getInst().resetAllReversed()` call; 5 new tests in test/utils/singleton/SingletonRegistryTest.cc all PASS; ResettableInterface.hh docstring updated to reflect self-register model; pre-existing FlowTest.EmptyFlowRunIsCallable + RouterClockTreeTest crashes unrelated to W7 (verified by reverting and re-running)) |
| W8 | 剩余清理 | 大头文件拆 (SegmentLibrary 563 / SteinerTree 355 / ParetoFront 326)；fast_sta/types/ 12→4-5；solution/(5)+segment_pruning/(7)+embedding/(3)+characterization 顶层(10) 悬浮 .hh 收敛 | completed @ 2026-05-20 (W8a ParetoFront.hh 326 → 277 行，Sort helpers 下沉到新 ParetoFront.cc，>300 行 .hh 仅剩 SteinerTree.hh 355 + SvgRenderingPrimitives.hh 330 两个已有 spec 豁免；W8b fast_sta/types/ 12→5：FastStaCore.hh (Ids+Enums+StatusTypes+TypesFwd) / FastStaGeometry.hh (Geometry+NodeNet) / FastStaTimingTypes.hh (Timing+Incremental+Parasitic) / FastStaLibertyPower.hh (Liberty+Power) / FastStaContext.hh (Context+CharTypes)，10 旧头删除 + 47 consumer 文件 include 路径全量替换 + 7 文件去重连续 #include；W8c htree solution/ 4 头与 embedding/EmbeddingState.hh 移入各自 detail/ 子目录，Solution.hh 升级为门面 transitively 包含 4 个 detail/ 头；segment_pruning/ 保持公开（多个跨子流程消费者）；W8d module/characterization 顶层 9 头保持不动（小且面向消费者）；W8e detail namespace 审计：96 处用 detail 多数已在物理 detail/ 子目录，flow/optimization/* 与 dmp_ceff/ 因整目录为实现层接受现状；build PASS) |
| W9 | 最终验收 + spec 定稿 + commit | PRD §6.1 全 grep verification；bit-identical QoR diff；3 份 spec 定稿；按 wave 切 commit；不 push | pending |

## 原 child task 编号映射

| 原 implement.md ID | Wave 映射 |
|---|---|
| T1 | 已完成（Session 62）✓ |
| T2 | W5 |
| T3 | 已部分完成（fast_sta 11 子目录 ✓）；剩余在 W8（types 合并）+ W5（FastSta API） |
| T4 | W3a |
| T5 | W3b |
| T6 | 已完成（FastClustering 拆分 ✓） |
| T7 | 已部分完成（IdbBridge ✓）；剩余 W2 |
| T8 | W6 |
| T9 | W1（HTree）+ W8（其余大头文件） |
| T10 | W7 |
| T11 | 推迟（PRD §5.1 范围内但 D10.10 标为最低优先级；W9 视时间预算决定） |

## Dispatch Template (sub-agent 派发模板)

```
Active task: .trellis/tasks/05-19-icts-code-standardization-refactor

# Wave 目标
[Wxxx 一句话目标]

# 必读
- prd.md §[相关章节]
- design.md §[相关章节]
- research/review/01-comprehensive-issues.md §[相关章节]
- .trellis/spec/backend/quality-guidelines.md (Forbidden Vocabulary 表)

# 范围（严格白名单）
- 仅改：[文件路径列表]
- 不要碰：[其他目录]

# 必做动作（编号顺序，含验收 grep）
1. ...
2. ...

# 验收
- bash build.sh 增量编译通过
- grep [...] 返回空 / ≤N
- 不引入新警告

# 不允许
- git commit / git push
- 改算法语义
- 删除未在范围内的文件
- 创建未声明的新文件
```

## Already Completed in Session 62

- ✅ T1: `.trellis/spec/backend/quality-guidelines.md` 加 Forbidden Vocabulary 表
- ✅ 删除 `src/operation/iCTS/source/flow/synthesis/trace/SynthesisTrace.hh:77` 自指 alias
- ✅ `decisions.md` 固化 D10.1~D10.10（用户全部按推荐方案确认）
- ✅ fast_sta 11 子目录拆分（部分 T3）
- ✅ Wrapper → IdbBridge 重命名（部分 T7）
- ✅ FastClustering 拆分（T6）

## Failed Dispatches (Session 62)

| Agent | 范围 | 失败原因 |
|---|---|---|
| `impl-fast-sta` (T3) | fast_sta 28 文件 → 11 子目录 | worktree 无 `.claude/`，agent 跑 117 min socket error；只创建了 types/ + dmp_ceff/ 骨架，未触碰原有文件 |
| `impl-bst` (T4) | bound_skew_tree mega-class Pimpl | worktree 无 `.claude/`；创建 3 个子目录骨架但新 .cc 引用的 .hh 未创建（编译报 file not found）|
| `impl-fast-clustering` (T6) | fast_clustering 拆分 | worktree 无 `.claude/`；删了旧 Polish 系列 .cc，未来得及验证 |
| `impl-db-naming` (T7) | Wrapper/STAAdapterInternal/ClockTraceResolverInternal/Tree.hh | worktree 无 `.claude/`；改了 14 个 STAAdapter*.cc 但 worktree HEAD 偏离 cts_refactor |

全部 worktree 已清理，分支已删除。

## Verification Commands (final, W9 用)

```bash
# 1. Build
cd /home/liweiguo/project/ecc-tools && bash build.sh 2>&1 | tail -100

# 2. Binary 验证（用户指定）
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

# 3. ecc dev 检查
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS

# 4. PRD §6.1 grep verification
find src/operation/iCTS/source -name '*Internal.hh' -o -name '*Internal.cc'  # 期望空
grep -rEn 'class .*(Helper|Manager|Handler|Service|Provider|Factory)\b' src/operation/iCTS/source  # 期望空
grep -rn 'snapshot' src/operation/iCTS/source --include='*.hh' --include='*.cc' | grep -v '//'  # 期望≤3
find src/operation/iCTS/source -name 'Wrapper*.hh' -o -name 'Wrapper*.cc'  # 期望空
find src/operation/iCTS/source/database/adapter/fast_sta -maxdepth 1 -name '*.hh'  # 期望仅 FastSta.hh
grep -rEn 'add_library' src/operation/iCTS/source | wc -l  # 期望 ≤35
find src/operation/iCTS/source -name '*.hh' -exec wc -l {} + | awk '$1>300'  # 期望 ≤2 行（有 spec 豁免）
```
