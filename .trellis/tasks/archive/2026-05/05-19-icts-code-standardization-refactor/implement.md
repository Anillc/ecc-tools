# iCTS 代码规范化重构 · 执行清单 (implement.md)

> **关联**：本文档把 `design.md` §10 的决策与 §2~§9 的方案落地为**分阶段、可验证、可回滚**的执行序列。
> **本任务（parent）执行什么**：本任务只输出本目录下的 `prd.md` / `design.md` / `implement.md` 三份规划产物，以及 `.trellis/spec/` 下的规范文档草案。**不直接做大规模代码改动**——所有代码改动由下方 11 个 child task 各自承担。
> **child task 创建时机**：用户 review 本规划三件套并确认 §1 决策矩阵后，逐个用 `task.py create ... --parent <this-task>` 创建。

---

## 0. 前置条件

- [ ] 用户 review `prd.md`、`design.md`、`implement.md` 三份产物
- [ ] 用户确认 `design.md` §10 的 10 项决策（D10.1 ~ D10.10）
- [ ] 本任务进入 `task.py start`（status → in_progress）后，才创建 child task

---

## 1. 决策确认矩阵（Phase 0：本任务内部完成）

按 `design.md` §10 列出 10 项决策，本任务在 review 时一次性确认（用户答案见 `.trellis/tasks/05-19-icts-code-standardization-refactor/decisions.md`，本任务在 review 后创建）：

| ID | 待决策 | 推荐 | 用户决定 |
|---|---|---|---|
| D10.1 | sub-flow 范式动词 | `init / run / report` | _待确认_ |
| D10.2 | utils/visualization 归宿 | 移到 `flow/report/visualization/` | _待确认_ |
| D10.3 | fast_sta 物理位置 | 保留 `database/adapter/fast_sta/` | _待确认_ |
| D10.4 | `FastStaClockContext` 是否拆 | 保留同类型 | _待确认_ |
| D10.5 | SubFlowOrchestrator | 引入 | _待确认_ |
| D10.6 | `Wrapper` 改名 | `IdbBridge` | _待确认_ |
| D10.7 | mega-class 拆分方式 | Pimpl | _待确认_ |
| D10.8 | IResettable + Registry | 引入 | _待确认_ |
| D10.9 | sub-flow 独立 namespace | 是（`icts::synthesis::*`） | _待确认_ |
| D10.10 | test/*Support* 本次范围 | 是（最低优先级） | _待确认_ |

---

## 2. 整体阶段总览

```
Phase 0 (本任务)：规划与 spec 草案
   │
   ├──> 产出：prd.md / design.md / implement.md
   ├──> 产出：.trellis/spec/backend/icts-architecture.md (草案)
   ├──> 产出：.trellis/spec/backend/icts-naming-convention.md (草案)
   └──> 产出：.trellis/spec/guides/icts-refactor-checklist.md (草案)
                            │
                            ▼
Phase 1 (child T1)：命名规范固化 + spec 入库
                            │
                            ▼
Phase 2 (child T2)：flow 范式统一 + SubFlowOrchestrator     ─┐
Phase 2 (child T3)：fast_sta 拆分                            │  T2/T3/T7/T9 可并行
Phase 2 (child T7)：database 命名清理（Wrapper、Internal）   │
Phase 2 (child T9)：大头文件拆分                              ┘
                            │
                            ▼
Phase 3 (child T4)：BST mega-class 拆分                      ─┐
Phase 3 (child T5)：CharBuilder mega-class 拆分               │  T4/T5/T6 可并行
Phase 3 (child T6)：FastClustering mega-class 拆分            ┘
                            │
                            ▼
Phase 4 (child T8)：CMake target 收敛 + utils 反向治理
                            │
                            ▼
Phase 5 (child T10)：单例 IResettable 契约
                            │
                            ▼
Phase 6 (child T11)：test/Support 命名清理
                            │
                            ▼
   Parent Review：整体验收（运行 prd.md §6 全部 verification）
```

并行原则：**T2/T3/T7/T9 之间无文件级冲突**（touch 不同区域），可并行；T4/T5/T6 三个 mega-class 在不同子目录，也可并行；但 T8（CMake 收敛）必须在所有结构性 PR 之后。

---

## 3. Phase 0：本任务内部执行

| 步骤 | 动作 | 验证 |
|---|---|---|
| 0.1 | 本任务 `task.py start`（在 PRD/design/implement review 通过后） | — |
| 0.2 | 写 `.trellis/spec/backend/icts-architecture.md` 草案（基于 design.md §2 分层 + §3 sub-flow 范式） | spec lint 通过 |
| 0.3 | 写 `.trellis/spec/backend/icts-naming-convention.md` 草案（基于 design.md §6 + prd.md §3 禁用词表） | spec lint 通过 |
| 0.4 | 写 `.trellis/spec/guides/icts-refactor-checklist.md` 草案（每个 child task 必须遵守的 PR checklist） | spec lint 通过 |
| 0.5 | 在本目录创建 `decisions.md`，固化 §1 决策矩阵的最终结果 | review 通过 |
| 0.6 | 在本目录创建 `child-task-tracker.md`，列 11 个 child task 的 slug + status + 创建日期 | — |
| 0.7 | 创建 11 个 child task（slug 见下方各 phase） | `task.py list --tree` 显示完整树 |
| 0.8 | 本任务进入"等待 child 完成"状态（保持 in_progress，但实际推动由 child 进行） | — |

**Phase 0 完成 = 所有 spec 草案就绪 + 11 个 child task 已 `task.py create`**。

---

## 4. Phase 1：命名规范固化

### Child T1：`icts-naming-convention-spec`

| 项 | 内容 |
|---|---|
| slug | `icts-naming-convention-spec` |
| 复杂度 | 轻量（PRD-only） |
| 依赖 | Phase 0 完成 |
| 范围 | `.trellis/spec/backend/icts-naming-convention.md`、`.trellis/spec/backend/icts-architecture.md`、`.trellis/spec/guides/icts-refactor-checklist.md` 正式定稿（从 Phase 0 草案升级） |
| 输入 | parent design.md §6、prd.md §3 |
| 产出 | 3 份 spec 文档定稿；新增 `禁用词全表 + CTS 替代名映射表`；附录给每个 child task 的禁用词清单 |
| 验证 | spec review；用 `grep` 在示例代码中演示替换前后差异 |
| 不做 | 不动任何代码 |

---

## 5. Phase 2：可并行的结构性改造

### Child T2：`icts-flow-paradigm-unification`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂（PRD + design + implement） |
| 依赖 | T1 |
| 范围 | `flow/` 全部 |
| 关键动作 | 1. 新增 `flow/SubFlowContract.hh` + concept · 2. 新增 `flow/FlowOrchestrator.{hh,cc}`，替代 `Flow.cc` 编排逻辑 · 3. 每个 sub-flow 改造为 `init/run/report` 三阶段静态门面 · 4. 消除跨 sub-flow 反向调用（`Report → Evaluation`）· 5. 移动 `design_conversion/` 到 `setup/` 或 `database/io/` · 6. 删除 `synthesis/distribution → instantiation` 的 CMake 反向 link · 7. 移除 `_internal` namespace 后缀（重命名为 `detail`）· 8. 修复 `Synthesis.cc:200` schema stage 名 `"CTSFlow"` → `"Synthesis"` · 9. 删除自指 alias `using SynthesisTraceSummary = SynthesisTraceSummary;` · 10. 删除未引用 alias 与死代码 |
| 验证 | `FlowTest` 全过；schema 输出与重构前 diff（允许 stage 名变化但 metric 数值 bit-identical）；`grep -rn '_internal' flow/` 为空 |
| 风险 | 改动面大；分 5 个 sub-PR（每 sub-flow 1 个）合入 |
| Bit-identical 基准 | 跑 `iCTS/test/flow/FlowTest.cc` 一遍 baseline，记 hash；改造每步都对比 |

### Child T3：`icts-fast-sta-decomposition`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂 |
| 依赖 | T1 |
| 范围 | `database/adapter/fast_sta/` 全部 + `flow/optimization/preparation/OptimizationPreparation.cc:271` 修复 |
| 关键动作 | 见 `design.md` §4。**严格按 6 阶段迁移**（research/database/04-fast_sta-refactor-proposal.md 已列出）：① 消灭 `FastStaDmpCeffInternal.hh` → `dmp_ceff/` 子目录 · ② `types/` 子目录拆 13 个 POD 头 · ③ 9 个功能子目录落地 · ④ `snapshot*` → `extract*`/`capture*`/`materialize*` 命名 · ⑤ API 精简（21 → 17，删除 5 个未用 + `clear` 移 friend）· ⑥ 修复 `OptimizationPreparation` 破例 |
| 验证 | `FastSTATest` 全过；`flow/optimization/` 端到端跑通；`find database/adapter/fast_sta -maxdepth 1 -name '*.hh'` 仅返回 `FastSta.hh`；`grep -rn 'snapshot' database/adapter/fast_sta` 为空 |
| 风险 | `FastStaTypes.hh` 拆为 13 个头会让 include 数爆炸；提供 `FastStaTypesFwd.hh` 聚合前向声明缓解 |
| 跨子任务契约 | T3 完成后 `FastSta.hh` 17 个 API 锁定；后续 T4/T5 不得修改其签名 |

### Child T7：`icts-database-naming-cleanup`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂 |
| 依赖 | T1 |
| 范围 | `database/io/` + `database/adapter/sta/` + `database/adapter/sdc/` + `database/spatial/Tree.hh` |
| 关键动作 | 1. `io/Wrapper.hh/.cc/WrapperClock*` → `IdbBridge.hh/.cc/IdbClock*`，单例宏 `WRAPPER_INST` → `IDB_BRIDGE_INST` · 2. 提供过渡 `using Wrapper = IdbBridge;` 一个 release · 3. `STAAdapterInternal.{hh,cc}` 拆为 `database/adapter/sta/internals/` 子目录 `StaEngineAccess` + `LibertyLookup` + `UnitConversion` + `InstResolution` · 4. `ClockTraceResolverInternal.hh` 拆为 `database/adapter/sdc/clock_trace/` 子目录 `SdcRefResolve` + `SinkClassifier` + `PinClassification` + `ReportEmit` · 5. `WrapperClockWriterSupport.cc` → `IdbClockWriterAccess.cc` 或合并入 `IdbClockWriter.cc` · 6. `WrapperClockWriterInternal.hh` → `IdbClockWriterTypes.hh` · 7. `spatial/Tree.hh` 去掉对 `design::Pin` 的依赖（前向声明 + 模板化 NodeT）|
| 验证 | 编译通过；`grep -rn 'Wrapper' database/` 仅保留过渡 alias；现有测试通过 |
| 风险 | binding 调用 `WRAPPER_INST` 需同步改；准备一个 release 期的兼容宏 `#define WRAPPER_INST IDB_BRIDGE_INST` |

### Child T9：`icts-large-headers-split`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂 |
| 依赖 | T1 |
| 范围 | `design.md` §9 表格列出的 9 个 .hh |
| 关键动作 | 1. `flow/synthesis/htree/segment_pruning/SegmentLibrary.hh` 拆 4-5 个 .hh + inline 大方法下沉 .cc · 2. `flow/optimization/model/OptimizationTypes.hh` 拆 7 个子主题头 · 3. `flow/synthesis/htree/HTree.hh` 6 个嵌套 struct 提升为顶层 · 4. `flow/synthesis/topology/Topology.hh` 嵌套类型同上 · 5. `module/characterization/Frontier.hh` → `ParetoFront.hh`，模板按职责拆 · 6. `utils/visualization/core/SvgCommon.hh` 拆 3 个 · 7. `module/routing/bound_skew_tree/Components.hh` 拆 5 个 .hh（注意：与 T4 协调） |
| 验证 | `find iCTS/source -name '*.hh' | xargs wc -l | awk '$1>300'` 数量从 7 减到 ≤2（有 spec 豁免） |
| 风险 | T4 也要拆 Components.hh，需协调先后；约定 T4 在 T9 之后启动 |

---

## 6. Phase 3：mega-class 拆分

### Child T4：`icts-bst-decomposition`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂 |
| 依赖 | T1、T9（Components.hh 已拆）、T3（fast_sta 锁定） |
| 范围 | `module/routing/bound_skew_tree/` 全部 |
| 关键动作 | 见 `design.md` §5.2。① `BoundSkewTree` Pimpl 化 · ② 7 个 `BoundSkewTreeTopic.cc` → `algorithm/detail/Bst*Solver.{hh,cc}` · ③ `GeomCalc` 静态类拆为 `geometry/` 子目录 + 4 个分文件 · ④ `BSTRouterInternal.hh` 删除，free fn 移到 `adapter/` 子目录 · ⑤ `Components.hh` 5 类拆 5 个 .hh（与 T9 协调）· ⑥ `BSTRouter` → `BstRouter` 改名 · ⑦ `kMeansPlus` 改为 `KMeans<>` 模板实例化（去除重复） |
| 验证 | BST router 单测全过；`flow/synthesis/htree/` 端到端跑通；BST 输出 bit-identical |
| 风险 | Pimpl 引入小开销；先 benchmark；如热路径退化 >5% 用模板内嵌实现 |

### Child T5：`icts-char-builder-decomposition`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂 |
| 依赖 | T1、T3（fast_sta 锁定，CharBuilder 可重新决定如何依赖） |
| 范围 | `module/characterization/` 全部 + `module/analytical_characterization/` 顶层 aggregator 修复 |
| 关键动作 | 见 `design.md` §5.3。① `CharBuilder` Pimpl 化 · ② 11 个 `CharBuilderTopic.cc` → `detail/*Sampler.{hh,cc}` 阶段组件 · ③ 公开头不再 include `FastStaTypes.hh`（解耦 P7）—— 改为前向声明 + 在 .cc 内 include · ④ `Frontier.hh` → `ParetoFront.hh`（与 T9 协调）· ⑤ `HashJoinEngine` → `HashJoinConcat`（去 Engine）· ⑥ 顶层 `module/CMakeLists.txt` 把 `icts_source_module_analytical_characterization` 加入 aggregator |
| 验证 | CharBuilder 单测全过；`grep -n 'FastStaTypes' module/characterization/*.hh` 为空 |

### Child T6：`icts-fast-clustering-decomposition`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂 |
| 依赖 | T1 |
| 范围 | `module/topology/fast_clustering/` 全部 |
| 关键动作 | 见 `design.md` §5.4。① 删除 `FastClusteringInternal.hh` → `detail/` 子目录 · ② 10 个 `FastClusteringTopic.cc` 改名为组件类（`ClusterRefiner` / `MergeRefiner` / `BoundaryRefiner` 等）· ③ `Polish` → `Refine`，`Draft` → `Candidate`，`DraftAggregate` → `ClusterCandidateStats` · ④ `Bounds` → `ClusterBounds`（避免与 BST `BoundingBox` 冲突）· ⑤ `CalcManhattanDistance` 改用 `utils/geometry::Manhattan` 模板（去重复） |
| 验证 | FastClustering 单测全过；`grep -n 'Polish\|Draft' module/topology/fast_clustering/` 为空 |

---

## 7. Phase 4：CMake 收敛 + 反向依赖治理

### Child T8：`icts-cmake-consolidation`

| 项 | 内容 |
|---|---|
| 复杂度 | 复杂 |
| 依赖 | T2、T3、T4、T5、T6、T7（所有结构稳定后才收敛） |
| 范围 | 所有 `CMakeLists.txt` + `utils/` 反向依赖治理 |
| 关键动作 | 1. 新增 `cmake/icts_targets.cmake` 提供 `icts_add_library` + `icts_apply_debug_flags` · 2. `flow/synthesis/htree/` 10 子目录合并为 1 个 target，10 个 CMakeLists.txt → 1 个 · 3. `flow/optimization/` 8 子目录 → `stages/` 子目录 + 1 target · 4. `module/routing/router/` 与 `module/routing/` aggregator 分离，重命名 target · 5. `module/routing/database/` 空目录删除 · 6. utils/geometry 提取 `Point<T>/Rect<T>/Region<T>` 到 `utils/spatial/`，database/spatial 改 CTS 业务特化 · 7. `utils/visualization/` 按 D10.2 决策移走 · 8. `external_libs/` 三个 .cmake 合并为一个 · 9. 全仓库 `grep` 确认无 `_internal` namespace 残留 |
| 验证 | `add_library` 数从 88 → ≤38；`cmake --graphviz` 无循环；增量编译实测 |
| 风险 | 库依赖图变化大，可能暴露隐藏环；先生成依赖图比对再合并 |

---

## 8. Phase 5：单例治理

### Child T10：`icts-singleton-reset-contract`

| 项 | 内容 |
|---|---|
| 复杂度 | 中等 |
| 依赖 | T2（FlowOrchestrator 就绪）、T3（FastSta 锁定）、T7（IdbBridge 就绪） |
| 范围 | `utils/singleton/` 新增 + 7 个单例改造 + `CtsApi::resetAPI` 简化 |
| 关键动作 | 见 `design.md` §8。① 新增 `utils/singleton/IResettable.hh` + `SingletonRegistry` · ② 7 个单例（FLOW / CONFIG / WRAPPER→IDB_BRIDGE / STA_ADAPTER / FAST_STA / DESIGN / SCHEMA_WRITER）继承 `IResettable` · ③ `CtsApi::resetAPI` 改用 `SingletonRegistry::getInst().resetAllReversed()` · ④ 新增 reset 单测：连续 init 3 次验证状态干净 |
| 验证 | `iCTS/test/` 增加 `SingletonResetTest.cc`；多次 init/run/reset 循环无状态泄漏 |

---

## 9. Phase 6：test 命名清理（最低优先级）

### Child T11：`icts-test-support-naming`

| 项 | 内容 |
|---|---|
| 复杂度 | 轻量（机械重命名） |
| 依赖 | T1（命名规范定稿） |
| 范围 | `test/**/*Support*` 31 个文件 |
| 关键动作 | 按 `design.md` §6.2 规则，逐文件根据实际作用决定改名：`*Fixture.hh` / `*Asset.hh` / `*Builder.hh` / `*Scenario.hh` |
| 验证 | 测试编译通过；测试逻辑不变 |
| 备注 | 可以选择性合并入 T2~T6 的对应单测目录里做，不必单独 PR |

---

## 10. 整体验收（parent task 收尾）

所有 child task 归档后，本任务执行整体 verification：

### 10.1 物理 grep 验收

```bash
# 1. 无 Internal 文件
test -z "$(find src/operation/iCTS/source -name '*Internal.hh' -o -name '*Internal.cc')" && echo PASS

# 2. 无 _internal namespace
test -z "$(grep -rn 'namespace.*_internal' src/operation/iCTS/source)" && echo PASS

# 3. snapshot 仅注释保留
test "$(grep -rn 'snapshot' src/operation/iCTS/source --include='*.hh' --include='*.cc' | grep -v '//' | wc -l)" -le 0 && echo PASS

# 4. 无 Wrapper 类/文件
test -z "$(find src/operation/iCTS/source -name 'Wrapper*.hh' -o -name 'Wrapper*.cc')" && echo PASS

# 5. 无 Manager/Handler/Service/Provider/Factory
test -z "$(grep -rEn 'class .*(Helper|Manager|Handler|Service|Provider|Factory)\\b' src/operation/iCTS/source)" && echo PASS

# 6. fast_sta 顶层只剩 FastSta.hh
ls src/operation/iCTS/source/database/adapter/fast_sta/*.hh | wc -l  # 期望 1

# 7. CMake target 数收敛
grep -rEn '^\s*add_library\(' src/operation/iCTS/source | wc -l  # 期望 ≤38

# 8. 大头文件
find src/operation/iCTS/source -name '*.hh' -exec wc -l {} + | awk '$1>300 {print}' | wc -l  # 期望 ≤2

# 9. OptimizationPreparation 不再直调 FastStaBuilder
grep -n 'FastStaBuilder::' src/operation/iCTS/source/flow/optimization/preparation/OptimizationPreparation.cc  # 期望空
```

### 10.2 行为验收

```bash
# 全量构建
bash build.sh 2>&1 | tee /tmp/build.log
grep -E 'error|FAILED' /tmp/build.log | head  # 期望空

# 跑现有测试
cd build && ctest -R icts --output-on-failure  # 全 PASS

# 端到端 CTS bit-identical
# 选 1-2 个固定 design 作为 baseline；对比重构前后 QorSummary 关键指标
diff baseline_qor.json refactored_qor.json  # skew/power/area 等 bit-identical
```

### 10.3 文档验收

- [ ] `.trellis/spec/backend/icts-architecture.md` 与新代码一致
- [ ] `.trellis/spec/backend/icts-naming-convention.md` 列出禁用词全表与替代映射
- [ ] `.trellis/spec/guides/icts-refactor-checklist.md` 可指导新增 sub-flow / sub-module
- [ ] 每个 child task 在 `child-task-tracker.md` 中标记 archived

### 10.4 决策回溯

整理本任务执行期间产生的决策变化，更新 `design.md` §10 状态，并在 `journal-2.md` 记录回顾。

---

## 11. 回滚点

每个 child task 是一个独立 PR，单独 revert 不影响其他 task。具体回滚边界：

| Child Task | 回滚后影响 |
|---|---|
| T1 | 仅 spec 文档变化，无代码 |
| T2 | flow 层范式回退；其他 sub-flow 仍正常工作 |
| T3 | fast_sta 拆分回退；CharBuilder 也要回退对 fast_sta 的解耦修改（联动） |
| T4/T5/T6 | mega-class 各自独立 revert |
| T7 | Wrapper/Internal 命名回退；用 alias 兼容期可平滑回滚 |
| T8 | CMake 回退；最大风险点（依赖图重画）；保留旧 CMakeLists 备份 |
| T9 | 大头文件回退；与 T4 协调 Components.hh |
| T10 | IResettable 移除；resetAPI 回退手工列表 |
| T11 | test 文件重命名回退（不影响生产代码） |

---

## 12. 进度跟踪

每个 child task 完成后，在本任务的 `child-task-tracker.md` 更新状态：

```markdown
# Child Task Tracker

| Slug | Phase | Status | Started | Archived | PR |
|---|---|---|---|---|---|
| icts-naming-convention-spec | 1 | archived | 2026-05-20 | 2026-05-22 | #1234 |
| icts-flow-paradigm-unification | 2 | in_progress | 2026-05-22 | — | #1240 |
| icts-fast-sta-decomposition | 2 | in_progress | 2026-05-22 | — | #1241 |
| ... |
```

parent task 在所有 child archived 后做最终 verification 与 commit。

---

## 13. 不在本次范围

明确不做（避免 scope creep）：

- [ ] 算法收益优化（仅做外形重构，不调参数、不改算法逻辑）
- [ ] `LOG_FATAL_IF` 改造为异常或返回码（P13，留 spec 记录）
- [ ] 把 `fast_sta` 物理搬到 `module/`（D10.3 决定保留 adapter）
- [ ] iEDA 其它模块的代码改动（idm / idb / ista-engine / iSTA / iPlacer）
- [ ] Tcl / Python binding 重写（仅同步符号引用）
- [ ] 新增 `CTSAPI` 公共方法
- [ ] 修改 CTSConfig JSON schema
- [ ] 替换日志库 / Schema 库

---

## 14. 完成定义（DoD）

本 parent task `archived` 的条件：

1. 11 个 child task 全部 `archived`
2. §10.1 所有 grep verification 全 PASS
3. §10.2 全量构建 + 现有测试全 PASS + bit-identical 验证 PASS
4. §10.3 三份 spec 文档定稿入库
5. `.trellis/workspace/dawnli/journal-2.md` 记录重构总结
6. Git 标签 `iCTS-refactor-v2` 标记重构完成节点
