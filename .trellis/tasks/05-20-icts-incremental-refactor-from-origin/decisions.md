# 决策记录：M3 / M4 撤回（2026-05-21）

## 背景

PRD §3 原列 4 项移植（M1 BoundSkewTree Pimpl / M2 CharBuilder Pimpl / M3 CMake target 收敛 / M4 IResettable 自注册）。M1 / M2 已落地并经 `bash build.sh` 验证；M3 / M4 实施后经审查决定撤回。

## M3 撤回理由

- origin 选择「子目录就近 CMakeLists.txt」是更易维护的范式：每个含 `.cc` 的目录有自己的 CMakeLists，改动局部化、grep 友好，新人易上手。
- 新增的 `cmake/icts_targets.cmake` 提供的 `icts_add_library` / `icts_apply_debug_flags` DSL 增加一层间接，理解成本高；换来的 ~880 行 boilerplate 减少不抵消可读性折损（那些 boilerplate 重复但不易出错）。
- 与 PRD §4 「保留 origin 子目录命名/粒度」的精神直接冲突：M3 目标本身（合并 target）与 origin 的 fine-grained 决策对立。

## M4 撤回理由

- `ResettableInterface` 基类 + `SingletonRegistry` once-flag 自注册 + 7 单例 `getInst()` lambda 是三层间接，可读性下降明显。
- reset 顺序由「首次 `getInst()` 时机」决定，变成隐式不可见；原本 `CTSAPI::resetAPI` 显式列表 5 行 grep 即可。
- 抽象唯一解决的真实 bug 是「漏列 `FAST_STA`」（`STA_ADAPTER` 经审查实为 no-op，已在 `CTSAPI.cc` 文档化），是 2 行修复，不需要 ~120 行 infrastructure（`SingletonRegistry.{hh,cc}` + 5 测试 + 7 单例改造）。

## 事实纠正

PRD §3-M4 表头原写「`utils/singleton/ResettableInterface.hh` 已存在 - 不动」与事实不符。`git ls-tree origin/cts_refactor -- src/operation/iCTS/source/utils/singleton/` 返回空，origin **根本没有** `singleton/` 目录。撤回时整个 `source/utils/singleton/` 与 `test/utils/singleton/` 全部删除。

## 保留的有价值产出

M4 工作中唯一真有价值的发现是 `CTSAPI::resetAPI` 漏了 FastSTA 的状态清理（`STA_ADAPTER` 仅是 facade，其下静态 iSTA 引擎由 `init()` / `destroyPower` / `destroyTimingEngine` 自管，确为 no-op）。该修复以最小侵入方式保留：`CTSAPI::resetAPI` 增加 `FastSTA::clear()` 一行 + 文档化 STAAdapter 不需要 reset 的原因，无需引入任何抽象层。

## v3 BST 目录重组 + 类命名（2026-05-21）

依据 `research/bst-structure-redesign.md` Step 8，把原 `module/routing/bound_skew_tree/tree/detail/` 这套语义贫弱的 Pimpl 容器重组为按 BST/DME 算法阶段语义命名的扁平目录。

- **3 阶段分类**：6 个组件类按经典 DME 流水线划分到 A（topology）/ B（bottom-up merging，含 .1 joining / .2 balance / .3 infeasibility）/ C（top-down embedding）三阶段，类名直接承载阶段语义（`BinaryTopology` / `BottomUpMergeJoining` / `BottomUpMergeBalance` / `BottomUpMergeInfeasibility` / `TopDownEmbedding`），grep `BottomUp` 可一次找全 stage B 的所有代码。
- **`BstPipeline` 替代 `BstDriver`**：避开 CTS 上下文里 "driver pin" / "driver inst" 的常见歧义；`Pipeline` 直接对应 A→B→C 流水线语义。
- **`BinaryTopology` 替代 `BstTopologyBuilder`**：避开 iCTS 已有 `module/topology/` 模块的 grep 冲突，同时显式标注"二叉拓扑"业务语义。
- **`BottomUpMergeInfeasibility` 而非 `BottomUpFallback`**：保留 BST 文献的关键词 "Infeasibility"，与兄弟 `Joining` / `Balance` 同前缀，grep 友好。
- **Flat folder 选择**：取消 archived 代码用的 `detail/` 子目录。原 v2 曾设计单独 `impl/` 放 BoundSkewTreeImpl，但若仅 Impl 入 folder 而其他 5 个 phase 类平铺则不对称；既然 v2 已决定让 class 名前缀承载阶段语义，folder 全平更自洽，IDE 文件栏路径也更短（`algorithm/BottomUpMergeJoining.cc` vs `tree/detail/BstJoiningSolver.cc`）。
- **`detail` 仅保留为 namespace 标记**：`icts::bst::detail` 是 C++ Pimpl 约定俗成，与目录布局解耦；目录名"显式语义"，namespace 名"表态对外不可见"，各司其职。
- **公开 API 零改动**：`BoundSkewTree` class 名 / 4 个公开方法签名 / `icts::bst` namespace 全部不动；外部 client (`BSTRouter.cc` / `clock_tree_conversion/BSTRouterBinaryTopology.cc`) 仅 include 路径从 `tree/BoundSkewTree.hh` 改为 `algorithm/BoundSkewTree.hh`，class / API 零改动。算法语义 bit-identical。
