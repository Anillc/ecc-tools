# Linear Clustering Architecture And Implementation

## Goal

在 iCTS 中新增工程级 `linear_clustering` 模块，对外仍通过 `src/operation/iCTS/source/module/topology/clustering/Clustering.hh` 暴露接口；该模块需具备后续扩展不同线性排序、分簇策略、约束评估与 cost 评估的结构基础。

## Scope

- 代码主落点：`src/operation/iCTS/source/module/topology/linear_clustering`
- 对外接口边界：`src/operation/iCTS/source/module/topology/clustering/Clustering.hh`
- 配置入口：`src/operation/iCTS/source/module/topology/config/TopologyConfig.hh`
- 输入：`std::vector<icts::Pin*>` + `LinearClusteringConfig`
- 输出：`ClusterResult`
- 相关后端支持：router / clock tree / STA cap 估计链路需补齐 `max_cap` 所需能力
- 测试：真实工艺接入、分布枚举、配置枚举、SVG 可视化、输出归档到 `icts_test_output/linear_clustering`
- 真实工艺测试组织：单独 test target，内部使用硬编码 ICS55 路径；路径可用时加载真实工艺，不可用时自动切换到 synthetic sweep

## Hard Requirements

- 线性聚类簇必须支持 `max_fanout`、`max_diameter`、`max_cap`
- `max_cap` 必须通过 `STAAdapter::queryPinCapacitance`、router、`ClockSteinerTree`、`RCTree`、`TimingEngine` 完成估计
- synthetic root 与 router 最终 root 语义必须同时保留在内部结果中，便于调试追踪，但不新增外部输出负担
- `linear_clustering` 子模块必须完整、职责清晰、可扩展，不引入空泛抽象
- 编码由 implement agents 执行；主线程负责方案收敛、review、质量把关

## Deliverables

- `dev_guide.md`：收敛后的开发清单与文件计划
- 完整 `linear_clustering` 模块代码、相关 router/back-end 支持、测试与可视化

## Acceptance Criteria

- [ ] `dev_guide.md` 收敛到工程级、可执行的开发计划，`prd.md` 与其保持一致
- [ ] `linear_clustering` 支持至少两类线性序列生成策略与多类 sequence split 策略
- [ ] 约束校验与整体 cost 评估可独立扩展，且默认实现可稳定覆盖原有能力
- [ ] `max_cap` 后端链路在四类 router 上可用
- [ ] dedicated real-tech test target 能在路径可用时拉起真实工艺信息，路径不可用时退回 synthetic，且两种模式都输出统计日志与 SVG
- [ ] in-scope findings 清理完成，并通过 `ecc_dev_tools` 的路径检查与 iCTS 全量检查

## Code-Spec Depth

- Target specs:
  - `src/operation/iCTS/source/module/topology/clustering/Clustering.hh`
  - `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh`
  - `src/operation/iCTS/source/module/topology/linear_clustering/*`
  - router / clock tree / STA timing support touched by `max_cap`
  - `src/operation/iCTS/test/module/topology/*`
- Core contracts:
  - `Clustering` 新增 linear clustering 接口与结果契约
  - `LinearClusteringConfig` 明确定义序列生成、分簇策略、DRV 约束与 router 选择
  - internal cluster evaluation path 明确 synthetic root / routed root / cap estimate 的职责边界
- Validation matrix:
  - Good: 多分布、多 config 下簇满足所有约束，统计与 SVG 输出正确
  - Base: 空载荷、单点、约束宽松、无可用工艺时的降级行为清晰
  - Bad: 约束过严、router/STA 不可用、估计失败时有明确日志与安全返回

## Working Notes

- `dev_guide.md` 是唯一编码执行基线；implement agents 必须按其中的文件计划和顺序推进
- `max_cap` 的 exact path 以 CTS-native routed evaluation 为准
- Q&A clean 仅处理真正阻塞的偏好或组织决策；其余内容按当前基线直接实施
- real-tech 测试采用独立 target；默认单测链路不因本地 PDK/DEF 缺失而失败
