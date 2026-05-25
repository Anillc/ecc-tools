# CTS Explicit STA And FastSTA Dependencies

## Goal

移除 `STA_ADAPTER_INST` 和 `FAST_STA_INST`，让时序查询、Liberty 查询、FastSTA context store 等依赖从 flow/module 接口上显式表达。

## Parent

`.trellis/tasks/05-24-cts-desingleton-refactor`

## Depends On

- `05-24-cts-runtime-flow-desingleton`
- 建议在 `05-24-cts-reporter-config-explicit` 和 `05-24-cts-design-wrapper-explicit` 后执行，以减少交叉冲突。

## Requirements

- `STAAdapter` 改为 runtime-owned adapter facade，不再通过 `STA_ADAPTER_INST` 获取。
- `FastSTA` 改为 runtime-owned context store，不再通过 `FAST_STA_INST` 获取。
- Characterization、HTree compensation、topology clustering、optimization、evaluation、fast_sta builder 显式接收 `STAAdapter&` 或 `FastSTA&`。
- 若底层 iSTA 仍是进程级全局，必须封装在显式 `STAAdapter&` facade 内；调用方仍要暴露该依赖。
- 不承诺并行执行时 iSTA/FastSTA thread-safe，只为未来多 runtime/并行化消除 iCTS 自身隐藏共享状态。
- STA/FastSTA 依赖不得塞入算法 `{Name}Config`；应作为 input/service reference 传递。

## Acceptance Criteria

- [x] `STA_ADAPTER_INST` 和 `STAAdapter::getInst` 从 `src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test` 移除。
- [x] `FAST_STA_INST` 和 `FastSTA::getInst` 从 `src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test` 移除。
- [x] HTree/Topology/Characterization/Optimization/Evaluation 的 STA 依赖从签名或 input struct 中可见。
- [x] FastSTA mutable context 不再通过 static singleton 存取。
- [x] tests 使用显式 fake/local adapter 或 runtime fixture。
- [x] `bash build.sh` 或本阶段选定的 iCTS 构建目标通过。

## Out Of Scope

- 不改写 iSTA 本身的全局设计。
- 不保证并行 STA 查询正确性；只消除 iCTS 层的隐藏单例依赖。
- 不改变时序模型和 QoR 语义。
