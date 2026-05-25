# CTS Flow Contracts Tests And Spec Finalization

## Goal

完成 synthesis 以外的 flow/module contract 收口、测试 fixture 去全局化、最终 `_INST` 清理和 Trellis spec 更新，使父任务达到总体验收。

## Parent

`.trellis/tasks/05-24-cts-desingleton-refactor`

## Depends On

- `05-24-cts-runtime-flow-desingleton`
- `05-24-cts-reporter-config-explicit`
- `05-24-cts-design-wrapper-explicit`
- `05-24-cts-sta-faststa-explicit`
- `05-24-cts-synthesis-contract-cleanup`

## Requirements

- Instantiation、Optimization、Evaluation、Report 使用 `{Name}Input` + `{Name}Config` 和 `{Name}Output` + `{Name}Summary` 分类原则。
- flow 阶段之间传递明确 payload 和 summary，不把日志、metrics、design object、adapter 引用混在同一个 result 中。
- 全部 tests/fixtures 从 singleton reset 改为显式 runtime、design、wrapper、reporter、adapter fixture。
- 新增或改造测试覆盖两个独立 runtime/flow 在同一进程连续运行，不共享 iCTS 内部状态。
- 删除除 `CTS_API_INST` 以外的所有 `_INST` 宏定义和内部调用。
- 更新 `.trellis/spec/`：
  - 内部禁用 singleton/service locator/global context。
  - `CTSAPI` 是唯一允许的外部 singleton entry。
  - 明确 runtime ownership、input/config/output/summary、config 最小化、output/summary 分离。
  - spec 应短、规范化、可执行，不写流水账。
- 跑父任务最终验证。

## Acceptance Criteria

- [x] Instantiation/Optimization/Evaluation/Report 不再暴露 broad `Options`/`Result` 混合结构。
- [x] tests 不再依赖 `CONFIG_INST`、`DESIGN_INST`、`WRAPPER_INST`、`STA_ADAPTER_INST`、`FAST_STA_INST`、`FLOW_INST`、`SCHEMA_WRITER_INST` reset。
- [x] `rg -n '\b[A-Z][A-Z0-9_]*_INST\b' src/operation/iCTS/source src/operation/iCTS/test src/operation/iCTS/api` 只剩允许的 `CTS_API_INST` 外部入口命中。
- [x] 非 `CTS_API_INST` 的 macro definition 和 `getInst()` singleton API 被删除。
- [x] 代表性 iCTS tests 和 flow 通过。
- [x] `.trellis/spec/backend/database-guidelines.md`、`quality-guidelines.md`、必要时 `directory-structure.md` 更新为新规范，并删除旧 singleton 边界描述。
- [x] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` 通过。

## Out Of Scope

- 不扩大到非 iCTS 模块。
- 不重写 CTS QoR 策略。
- 不把 spec 写成详细迁移日志；迁移历史留在 task artifacts。
