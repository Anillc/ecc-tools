# CTS Runtime And Flow Desingleton

## Goal

把 `CTSAPI` 以内的运行期状态从散落的 singleton owner 收敛为一个显式 runtime owner，并把 `Flow` 从 singleton 改为普通对象。此任务是全部去单例化工作的第一步，只处理生命周期和 flow 入口，不清理所有算法 contract。

## Parent

`.trellis/tasks/05-24-cts-desingleton-refactor`

## Requirements

- 保留 `CTSAPI` 作为外部稳定入口；公开 API 签名尽量不变。
- 引入 `CTSRuntime` 或等价私有 owner，持有 `Config`、`Design`、`Wrapper`、`STAAdapter`、`FastSTA`、`schema::SchemaWriter`。
- `CTSRuntime` 只允许出现在 API/flow 边界；下层模块不得把它当作 service locator 传播。
- `Flow` 改为普通对象，持有 run-local state，例如 run summary、clock layout、evaluation state、instantiation result、characterization library。
- `CTSAPI` 显式持有 runtime 和 flow；`resetAPI()` 通过重建或 reset 成员对象表达生命周期。
- 移除 `FLOW_INST` 的定义和使用。
- 不引入 `SingletonRegistry`、全局 context、静态当前 runtime 或其它替代性单例。

## Acceptance Criteria

- [x] `src/operation/iCTS/source/flow/Flow.hh` 不再定义 `FLOW_INST`，`Flow::getInst()` 被删除。
- [x] `src/operation/iCTS/api/CTSAPI.cc` 不再通过 `FLOW_INST` 调用 flow。
- [x] `CTSAPI` 拥有一个显式 runtime owner 和一个非 singleton `Flow` 对象，reset 顺序从成员生命周期或局部代码可见。
- [x] 下层模块没有新增 `CTSRuntime&` 泛滥传递；只能在 flow/API 边界使用 runtime，并向模块传窄依赖。
- [x] 现有公开 CTSAPI 行为保持兼容。
- [x] `rg -n 'FLOW_INST|Flow::getInst' src/operation/iCTS` 无有效命中。
- [x] `bash build.sh` 或本阶段选定的 iCTS 构建目标通过。

## Out Of Scope

- 不在本任务内删除 `CONFIG_INST`、`DESIGN_INST`、`WRAPPER_INST`、`STA_ADAPTER_INST`、`FAST_STA_INST`、`SCHEMA_WRITER_INST`。
- 不重构 HTree/Topology/Optimization 等算法输入输出 contract。
- 不改 CTS 算法语义和 QoR 策略。
