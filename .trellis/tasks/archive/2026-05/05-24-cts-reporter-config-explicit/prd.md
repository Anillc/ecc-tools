# CTS Explicit Reporter And Config Boundaries

## Goal

移除 `SCHEMA_WRITER_INST` 和 `CONFIG_INST` 对 flow/module/algorithm 的隐藏依赖，让 reporter 和配置转换在 flow 边界显式发生。算法层只能看到当前模块真正需要的 `{Name}Config` 和 report sink。

## Parent

`.trellis/tasks/05-24-cts-desingleton-refactor`

## Depends On

- `05-24-cts-runtime-flow-desingleton`

## Requirements

- `schema::SchemaWriter` 改为 runtime-owned 普通 reporter，不再通过 `SCHEMA_WRITER_INST` 获取。
- 所有需要 report 的 flow/module 显式接收 `schema::SchemaWriter&`，或接收更窄的 report interface。
- `schema::EmitTable` / `EmitKeyValueTable` / `EmitDiagnostic` / `EmitArtifact` 等 helper 不得隐藏访问全局 writer；改为接收 writer 参数或成员调用。
- `Config` 改为 runtime-owned 普通对象；保留现有配置文件解析兼容。
- `CONFIG_INST` 只能在迁移期间短期存在于尚未处理文件；本任务完成后 source/api/test 无 `CONFIG_INST`。
- 在 setup/config-adaptation 边界将全局配置拆成窄 config，例如 `CTSFlowConfig`、`SynthesisConfig`、`HTreeConfig`、`CharacterizationConfig`、`OptimizationConfig`、`EvaluationConfig`、`ReportConfig`。
- `{Name}Config` 只包含会改变算法行为的 knobs；DBU、目录、report path、object prefix、reporter、cache/library/reference 不放入算法 config。
- 不把 `CTSRuntime`、`Config&` 或大 options 包继续传进深层算法来替代 singleton。

## Acceptance Criteria

- [x] `SCHEMA_WRITER_INST` 和 `SchemaWriter::getInst` 从 `src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test` 移除。
- [x] `CONFIG_INST` 和 `Config::getInst` 从 `src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test` 移除。
- [x] Setup/config parsing 写入 runtime-owned `Config`，不写全局 config。
- [x] Flow/module 的 reporter 依赖从函数签名、input struct 或构造绑定中可见。
- [x] HTree/Topology/Characterization/Optimization 等算法不直接读取全局 `Config`。
- [x] fake config 字段被删除或迁移到 input/summary/report path；剩余 config 字段能说明其算法影响。
- [x] reporter/config 相关测试改为显式 fixture，测试之间不共享全局 writer/config 状态。
- [x] `bash build.sh` 或本阶段选定的 iCTS 构建目标通过。

## Out Of Scope

- 不负责删除 `DESIGN_INST`、`WRAPPER_INST`、`STA_ADAPTER_INST`、`FAST_STA_INST`。
- 不要求在本任务内完成所有 `HTreeOutput/HTreeSummary` 拆分；但不能新增新的 broad `Options` 混合结构。
