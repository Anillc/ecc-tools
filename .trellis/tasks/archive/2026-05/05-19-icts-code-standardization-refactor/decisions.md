# Decisions: iCTS 代码规范化重构

> **状态**：用户于 2026-05-19 批准 "按你的方案来"，全部采纳推荐方案。
> **2026-05-20 增补**：Session 63 启动 "全量收尾 Roadmap (W0~W9)" 之前，用户针对审视报告 (`research/review/01-comprehensive-issues.md`) 新增 3 项决断。

| ID | 决策点 | 最终采纳 | 来源 |
|---|---|---|---|
| D10.1 | sub-flow 范式动词 | **`init / run / report`**（与用户原话一致） | `design.md` §10 |
| D10.2 | `utils/visualization/` 归宿 | **合并到 `flow/report/visualization/`** | `design.md` §10 |
| D10.3 | `fast_sta` 物理位置 | **保留在 `database/adapter/fast_sta/`** | `design.md` §10 |
| D10.4 | `FastStaClockContext` 是否拆 | **保留同类型**（重构不改语义） | `design.md` §10 |
| D10.5 | 引入 `FlowOrchestrator` | **是** | `design.md` §10 |
| D10.6 | `Wrapper` 改名 | **`IdbBridge`** | `design.md` §10 |
| D10.7 | mega-class 拆分方式 | **Pimpl + 阶段组件** | `design.md` §10 |
| D10.8 | `IResettable` + Registry | **引入** | `design.md` §10 |
| D10.9 | sub-flow 独立 namespace | **是**（`icts::synthesis::*` 与目录对齐） | `design.md` §10 |
| D10.10 | test/Support 本任务范围 | **是**（优先级最低，可缩范围至高频文件） | `design.md` §10 |
| D10.11 | HTree 悬浮 .hh 收敛 | **全部合并进 HTree.hh**（不用 detail/ 子目录方案；用户原话"对外只有 HTree.hh/.cc"） | Session 63 review |
| D10.12 | SubFlowOutcome.hh 处理 | **删除原文件 + 新建 `flow/interface/` 子目录**（FlowInterface.hh/.cc + CMakeLists.txt），所有 sub-flow 实现 init/run/report 接口范式；用户原话"额外增加这个出来，很不优雅" | Session 63 review |
| D10.13 | W0~W9 执行节奏 | **一次性推完再汇报**（中途不打断；每 Wave 失败自动回滚 / 重试；最终汇报覆盖全程） | Session 63 review |

## 与现有 spec 的衔接

- `.trellis/spec/backend/directory-structure.md` 已规定 `setup → synthesis → optimization → instantiation → evaluation → report` 顺序，与本方案 D10.1 范式一致，**不动**
- `.trellis/spec/backend/database-guidelines.md` 列出 6 个单例（缺 `FLOW_INST` 与 `SCHEMA_WRITER_INST` 与 `FAST_STA_INST`），D10.8 引入 IResettable 后，spec 单例表会同步更新
- `.trellis/spec/backend/quality-guidelines.md` 已禁止"broad snapshots that duplicate queryable CTS/iDB state"，本方案 P3 把这条规则物化为禁用词全表（在 T1 中执行）
- `.trellis/spec/backend/logging-guidelines.md` 已指明 `SCHEMA_WRITER_INST`，所以 schema 单例保留命名

## 工作约束（来自现有 spec）

- AI 不得 `git push`（`project-constraints.md`）
- 不轻易改 spec，除非属于全局开发规范（**命名禁用词属于全局规范，可改**）
- ecc_dev_tools 在 `.trellis/ecc_dev_tools/check.py`，最终 finish-work 时跑

## 范围调整（应对工作量）

按 `implement.md` §13 已限定不做项，本次实际推进以下优先级：

1. **必做**（核心痛点）：T1 (spec) / T3 (fast_sta) / T7 (database 命名) / T9 (大头文件) / T4 (BST) / T5 (CharBuilder) / T6 (FastClustering) / T2 (flow 范式)
2. **尽力做**：T8 (CMake 收敛) / T10 (IResettable)
3. **缩范围**：T11 (test/Support) 只改 source 引用最多的 5-10 个文件，其余留作后续

如对话上下文不足，按上述优先级截断，未完成项明确列入最终汇报。
