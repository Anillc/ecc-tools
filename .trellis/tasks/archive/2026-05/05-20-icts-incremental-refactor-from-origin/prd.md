# iCTS 补充重构：基于 origin 增量移植 HEAD 的 4 个优势

> **任务定位**：轻量后置补遗（PRD-only 可接受）。
> **不重做**已被 origin/cts_refactor 合理覆盖的工作。
> **只增量**移植 HEAD `915468e2a` 上 4 个客观更优的结构性改造。

---

## 1. 背景

Session 63 期间在本地 `cts_refactor` 分支上推完了 W0~W9（commit `915468e2a`），与此同时 `origin/cts_refactor` 已经独立完成了相似目标的 11 个子任务（合并为 `597cc31b8`）+ 12 个 archive chore commits。两份代码客观对比（见 `.trellis/tasks/archive/2026-05/05-19-icts-code-standardization-refactor/research/review/01-comprehensive-issues.md`）显示：

- **origin 的优势**：CTS 业务化命名更地道（`clock_net_parasitic` / `clock_sizing` / `clock_state` / `segment_char`）、端到端 iCTS run 实测通过（CTS elapsed 16.154 s）、12 个子任务可追溯、spec 禁词更严格（含 `Input`/`Types`/`rollback`/`fallback`）
- **HEAD 的优势**：BoundSkewTree / CharBuilder 真正 Pimpl 化、CMake target 51 vs 97、IResettable 自注册落地、FlowInterface 范式抽象

本 task 的策略：以 origin 为底，**只把 HEAD 上客观更优的 4 项增量移植过去**，避免重做 origin 已经做对的事。

---

## 2. 前置条件（用户须在本 task `start` 之前完成）

```bash
# 1. 丢弃本地 commit 915468e2a（保留 archived task 文档可供参考）
git reset --hard b22bf658f

# 2. 拉到 origin/cts_refactor 最新（14 commits）
git pull --rebase origin cts_refactor

# 3. 验证 baseline 可编译
bash build.sh

# 4. 启动本 task
python3 ./.trellis/scripts/task.py start 05-20-icts-incremental-refactor-from-origin
```

本 PRD 假设上述前置已完成，代码状态 = origin/cts_refactor。

---

## 3. 移植清单（4 项 + 1 可选）

每项独立可验证，建议各成一次 commit。

### M1 · BoundSkewTree mega-class Pimpl 拆分

> **v3 重组（2026-05-21）**：本节最终落地的目录与类命名以 `research/bst-structure-redesign.md` Step 8 为准（`tree/` 改名 `algorithm/`、`detail/` 子目录拍平、6 组件类按 BST/DME 阶段语义重命名 + accessor / 数据成员 / friend 同步重命名）。下方原 PRD 文本保留作为变化前的对照基线。

| 维度 | 起点（origin） | 终点 |
|---|---|---|
| `bound_skew_tree/algorithm/BoundSkewTree.hh` 行数 | 373 | **≤ 70** |
| Mega-class private nested types | 11 个（KMeansConfig / MergeAreas / EmbeddingStep / BalanceRefAxis / JoiningSegmentDelayQuery / SideDelay / BalancePointQuery / BalancePointResult / MergeDistances / MergeRegionSpan / SideState / EndState / TimingState / AxisDelayFactor）暴露在公开头 | 全部下沉到 `bound_skew_tree/algorithm/BoundSkewTreeImpl.hh` |
| Chapter slicing `.cc`（`auto BoundSkewTree::xxx`）| `BoundSkewTreeBalance.cc` / `Embedding.cc` / `Flow.cc` / `InfeasibleMerge.cc` / `Joining.cc` / `Topology.cc`（6 文件，~3000 行） | 全部升级为 6 真组件类，按 BST/DME 阶段语义命名（`BstPipeline` / `BinaryTopology` / `BottomUpMergeJoining` / `BottomUpMergeBalance` / `BottomUpMergeInfeasibility` / `TopDownEmbedding`），各自独立 `.hh + .cc` 平铺在 `algorithm/` 目录 |
| 数据成员归属 | 全在 `BoundSkewTree.hh` | 全在 `BoundSkewTreeImpl.hh` |
| 组件类访问数据 | — | 构造时拿 `BoundSkewTreeImpl&` 引用；通过 `friend` 声明访问 private 成员；跨组件共享数学方法（如 `pointAt` / `linePoint`）保留为 `BoundSkewTreeImpl` 静态方法 |

**参考实现**：`.trellis/tasks/archive/2026-05/05-19-icts-code-standardization-refactor/research/review/04-w3a-baseline-counts.md` + archived 代码中的 `module/routing/bound_skew_tree/algorithm/detail/` 全部文件（注意：archived 代码用了 `algorithm/detail/` 目录，本 task v3 进一步把 `detail/` 拍平到 `algorithm/` 一级目录下，详见 §3.M1 v3 重组横幅）。

**v3 目录结构**（来自 `research/bst-structure-redesign.md` Step 8）：
- 顶层 wrapper 从 `tree/` 改名为 `algorithm/`
- 取消 `detail/` 子目录，BoundSkewTreeImpl 与 6 组件类全部平铺在 `algorithm/` 下（detail 仅保留为 namespace，不挂在路径上）
- `BSTRouter.hh` / `clock_tree_conversion/` / `component/` / `config/` / `geometry/` 全部保留 origin 命名

**约束**：BoundSkewTree 公开 API（`run` / `get_root` / `set_root_guide` / `set_rc_pattern`）签名完全不变；bit-identical 算法语义。

### M2 · CharBuilder mega-class Pimpl 拆分

| 维度 | 起点（origin） | 终点 |
|---|---|---|
| `characterization/builder/CharBuilder.hh` 行数 | 190 | **≤ 120** |
| Chapter slicing `.cc` | `builder/CharBuilderBuild.cc` / `Config.cc` / `Feasibility.cc` / `Topology.cc` / `circuit/CharBuilderCircuit.cc` / `pattern/CharBuilderPatternEnumeration.cc` / `pattern/CharBuilderPatternStorage.cc` / `sampling/CharBuilderSampleStorage.cc` / `Sampling.cc` / `SlewSampling.cc` / `StaSampling.cc`（共 11 文件） | 升级为真组件类。**保留 origin 的 4 子目录拆分**（builder/circuit/pattern/sampling），每个 chapter `.cc` 在原所属子目录内升级为命名清晰的组件类 |
| 组件类建议命名 | — | `builder/CharSetupConfigurator` + `CharBuildOrchestrator` + `CharFeasibilityChecker` + `CharTopologyPlanner` / `circuit/CharCircuitBuilder` / `pattern/CharPatternEnumerator` + `CharPatternStorage` / `sampling/CharStaSampler`（4 chapter 文件合并为 1 个 sampler，pipeline 内聚）+ `CharSampleStorage` |
| Pimpl 实现头 | — | `characterization/builder/detail/CharBuilderImpl.hh` |

**参考实现**：`.trellis/tasks/archive/2026-05/05-19-icts-code-standardization-refactor/research/review/05-w3b-baseline-counts.md` + archived 的 `module/characterization/detail/` 全部 10 组件类源码。

**保留 origin 决定**：
- 子目录拆分 `builder/circuit/pattern/sampling/pruning/table/buffer_cell` 全部保留（与 HEAD 把所有组件挤进单一 `detail/` 子目录不同——origin 这套子目录更符合 CTS 业务语义）
- `pruning/Frontier.hh` 不改名为 `ParetoFront.hh`（origin 保留 `Frontier`，HEAD 改名是冒进；本 task 尊重 origin）
- `pruning/HashJoinEngine.hh` 保留（origin 未删该 shim；HEAD 删了。本 task 尊重 origin）

**约束**：CharBuilder 公开 API 签名完全不变；bit-identical。

### M3 · CMake target 收敛

> **状态：撤回（2026-05-21）**。本节移植已实施后被回退；理由见 `decisions.md`（DSL 抽象成本不抵消重复 boilerplate 的可读性收益，与 §4「保留 origin 子目录粒度」精神冲突）。下方原 M3 设计保留作历史记录。

| 维度 | 起点（origin） | 终点 |
|---|---|---|
| `add_library` 数 | 97 | **≤ 60**（不强求达到 PRD §6.1 的 35——origin 选择了"子目录精细化"路线，35 不现实） |
| 公共宏 | 各 CMakeLists 重复 `option(DEBUG_ICTS_*)` + `target_compile_options(... -g3 -O0)` 模板（~880 行重复） | 新增 `src/operation/iCTS/cmake/icts_targets.cmake` 提供 `icts_add_library(...)` + `icts_apply_debug_flags(...)` 函数；新 target 默认走该函数 |
| 空目录 | `module/routing/database/`（origin 已删） | — |
| 0~1 `.cc` 子 target | 多（散布在 `flow/synthesis/*` / `flow/optimization/*` / `flow/report/visualization/*` 各子目录） | 合并到上层 aggregator |

**约束**：
- 不引入循环依赖（用 `cmake --graphviz` 验证）
- 不破坏 PUBLIC / PRIVATE 边界（合并后 PUBLIC 链是子 target PUBLIC 的并集）
- **保留 origin 的子目录命名**（如 `clock_net_parasitic` / `clock_sizing` / `clock_state` / `segment_char`）—— 只合并 CMake target，不动目录
- **iEDA 链接顺序验证**：完成后必须 `bash build.sh` 完整链接 iEDA binary（HEAD 经历过 fast_sta 链接顺序失败的教训，见 archived `child-task-tracker.md`）

**参考实现**：`.trellis/tasks/archive/2026-05/05-19-icts-code-standardization-refactor/research/review/09-w6-baseline-counts.md` + archived 的 `cmake/icts_targets.cmake`。

### M4 · IResettable 自注册 + SingletonRegistry

> **状态：撤回（2026-05-21）**。本节移植已实施后被回退；理由见 `decisions.md`（ResettableInterface + SingletonRegistry + 7 单例 once-flag 自注册三层间接导致 reset 顺序隐式不可见，不抵消 2 行 bug fix 的价值）。仅保留唯一有价值的发现：CTSAPI::resetAPI 漏掉 FAST_STA 的 `clear()`（STA_ADAPTER 经审查实为 no-op，已在 CTSAPI.cc 文档化）。
>
> 同时纠正原 PRD 的事实错误：表头写「`utils/singleton/ResettableInterface.hh` 已存在 - 不动」与事实不符。`git ls-tree origin/cts_refactor -- src/operation/iCTS/source/utils/singleton/` 返回空，origin **根本没有** `singleton/` 目录；撤回时整个 `source/utils/singleton/` 与 `test/utils/singleton/` 全部删除。下方原 M4 设计保留作历史记录。

| 维度 | 起点（origin） | 终点 |
|---|---|---|
| `utils/singleton/ResettableInterface.hh` | 已存在 | 不动 |
| `utils/singleton/SingletonRegistry.{hh,cc}` | 不存在 | 新增；提供 `registerSingleton` / `resetAll` / `resetAllReversed` |
| 7 单例（Flow / Config / Design / IdbBridge / STAAdapter / FastSTA / SchemaWriter）自注册 | 各单例 reset 散乱、CTSAPI::resetAPI 手工列表 | 每个单例 `getInst()` 用 once-flag 自注册 |
| `CTSAPI::resetAPI()` | 手工列举 5 reset（漏 STA_ADAPTER / FAST_STA）| 一行 `SingletonRegistry::getInst().resetAllReversed()` |
| 单测 | 无 | `test/utils/singleton/SingletonRegistryTest.cc` 5 测试（idempotency / null guard / 7 singletons 自注册 / 自定义 singleton sweep / 多次 sweep idempotency）|

**参考实现**：archived 的 `utils/singleton/SingletonRegistry.{hh,cc}` + `test/utils/singleton/SingletonRegistryTest.cc` 可直接 cherry-pick。

**这是最纯粹的补遗**：不动 origin 任何已有结构，纯加法。

### M5 ·（可选）Flow.hh include 链收敛

> **状态**：可选项。`origin/cts_refactor` 已在 `Flow.hh` 内嵌 `FlowStageStatus` enum + `ClockDataReadResult` struct，是个有意识的设计选择（与 HEAD 用 `flow/interface/` 子目录方案不同）。**优先尊重 origin 的内嵌选择**。

如果你（用户）确认希望进一步收敛，则：

| 维度 | 起点 | 终点 |
|---|---|---|
| `Flow.hh` 公开 `#include` | 7 个（含深 4 层 `synthesis/htree/characterization/library/CharacterizationLibrary.hh`） | ≤ 4 个 + 前向声明 |
| 数据成员 | 全在 Flow class 内 | Pimpl `std::unique_ptr<FlowImpl> _impl;`；`flow/FlowImpl.hh` 仅 `Flow.cc` include |

**约束**：保留 origin 的 `FlowStageStatus` 内嵌；**不**新建 `flow/interface/` 子目录。

**默认不做**——除非你（用户）明确同意。本 PRD 默认 M5 为 out of scope。

---

## 4. 不做（out of scope，明确避免范围蔓延）

| 项 | 原因 |
|---|---|
| 改 `fast_sta` 任何子目录命名 | origin 用 `clock_net_parasitic` / `clock_sizing` / `clock_state` / `segment_char` 等 CTS 业务化命名比 HEAD 的 `parasitics`/`incremental`/`builder`/`characterization` **更地道** |
| 拆分 origin 的 `fast_sta/timing/` 内的 `dmp_ceff` | origin 把 `DmpSolver` 等放在 `timing/` 内是合理的语义合并（dmp_ceff 本质是 timing 计算） |
| 改 `module/characterization/builder/circuit/pattern/sampling/pruning` 子目录拆分 | origin 这套按 CTS 角色拆分是合理的，HEAD 把所有组件挤 `detail/` 一坨反而损失语义 |
| 改 `quality-guidelines.md` | origin 的禁词更严格（含 `Input`/`Types`/`rollback`/`fallback`），HEAD 的表格形式更明显但禁词范围更窄；尊重 origin |
| 删 `Frontier.hh` 或 `HashJoinEngine.hh` | origin 保留这两个名字（与 HEAD 改名为 `ParetoFront.hh` / 删除 shim 不同）；origin 的判断也合理 |
| 拆 `flow/interface/` 子目录 | origin 内嵌 `FlowStageStatus` 到 `Flow.hh`（设计选择）；不强加 HEAD 的方案 |
| 重做端到端 iCTS run 验证 | origin 已经跑过（CTS 16.154 s + fanout=4 收敛） |
| 重新 archive / 删除 origin 的 12 个子任务 | 这些是 origin 工作的轨迹，保留 |
| 在新 task 内重做研究文档 | archived task 已有 30+ 份研究 markdown，需要时 git show 即可 |

---

## 5. Acceptance Criteria

### 5.1 物理验收（grep + wc）

- [ ] `wc -l src/operation/iCTS/source/module/routing/bound_skew_tree/tree/BoundSkewTree.hh` ≤ 70
- [ ] `wc -l src/operation/iCTS/source/module/characterization/builder/CharBuilder.hh` ≤ 120
- [ ] `grep -rEc 'auto BoundSkewTree::' src/operation/iCTS/source/module/routing/bound_skew_tree/tree/*.cc` = 0（chapter slicing 已清除；公开门面 `BoundSkewTree.cc` 的薄转发不计）
- [ ] `grep -rEc 'auto CharBuilder::' src/operation/iCTS/source/module/characterization/builder/*.cc` 仅薄转发数
- [ ] ~~`grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source | wc -l` ≤ 60~~ **（撤回 · M3）**
- [ ] ~~`grep -n 'CONFIG_INST\.reset()\|DESIGN_INST\.reset()' src/operation/iCTS/api/CTSAPI.cc` = 0（已用 Registry）~~ **（撤回 · M4；resetAPI 恢复显式列表，含新增 `FastSTA::clear()` 调用）**
- [ ] ~~`grep -rc 'public ResettableInterface' src/operation/iCTS/source` ≥ 7~~ **（撤回 · M4）**

### 5.2 行为验收（必须真跑）

- [ ] `bash build.sh` exit 0 + iEDA binary 完整链接（无 link order 失败）
- [ ] `ninja -C build` 所有 `icts_test_*` 编译通过
- [ ] `cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` 跑完 + CTS elapsed 与 origin baseline 比对（差异 ≤ 2%）
- [ ] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` 0 findings
- [ ] ~~`SingletonRegistryTest` 5 测试 PASS~~ **（撤回 · M4）**

### 5.3 算法语义验收

- [ ] BoundSkewTree 公开 API 4 个方法签名完全不变
- [ ] CharBuilder 公开 API 签名完全不变
- [ ] CTSAPI 5 个公开方法签名完全不变
- [ ] QoR bit-identical（用同一 design 输入对比重构前后的 skew / power / area / wirelength）

---

## 6. 实施约束

- **顺序**：M1（BoundSkewTree）→ M2（CharBuilder）→ CTSAPI 补漏 `FastSTA::clear()`（保留 M4 撤回时的有价值 bug 修复）。原 §3 顺序 M4 → M1 → M2 → M3 中的 M3 / M4 已撤回（详见 `decisions.md`）。
- **每个 M 单独 commit**：便于独立 revert；commit message 格式 `refactor(iCTS): <M-name> from HEAD W3a/b archived`
- **dispatch 模式**：主 tree 串行 `trellis-implement` sub-agent（不用 worktree，见 archived `worktree-pitfall.md`）
- **每个 M 完成后**：跑 `bash build.sh` PASS 才进下一个；M2 完成后跑 iEDA -script 验证（M3 撤回后链接顺序风险已消除）
- **不允许**：git commit 之前未经验证；改算法语义；改公开 API；引入循环依赖；触碰 §4 列出的 out of scope 项

---

## 7. 参考资料

- archived task `.trellis/tasks/archive/2026-05/05-19-icts-code-standardization-refactor/`：
  - `research/review/01-comprehensive-issues.md`（两份代码对比的原始证据）
  - `research/review/04-w3a-baseline-counts.md`（BoundSkewTree 拆分前后数字）
  - `research/review/05-w3b-baseline-counts.md`（CharBuilder 拆分前后数字）
  - `research/review/09-w6-baseline-counts.md`（CMake 收敛前后数字）
  - `research/review/10-w7-baseline-counts.md`（IResettable 落地结果）
  - `worktree-pitfall.md`（dispatch 模式踩坑记录）
- 当前本地 commit `915468e2a`（W0-W9 实现，可作为代码模式参考但**不要 cherry-pick 整体**——其上的命名/目录决策与 origin 冲突）

---

## 8. 复杂度与 PRD-only 判断

本 task 是**轻量后置补遗**（PRD-only 可接受）：
- 4 个 M 在 archived 代码中已有完整实现可参考
- 不涉及新的算法 / 数据模型 / API 设计决策
- 主要工作是"参照 archived 代码 + 适配 origin 已有的目录与命名"

**不需要 `design.md` / `implement.md`** —— 本 PRD 已含足够设计信息（§3 移植清单 + §4 不做清单 + §6 实施约束）。

如果在 M1 / M2 实施阶段发现 origin 的 `tree/` / `builder/circuit/pattern/sampling/` 子目录拓扑与 Pimpl 模式有冲突（例如某组件类需要跨子目录引用 private 数据），届时再补一份简短的 `design-deviation.md` 记录偏差与决策。
