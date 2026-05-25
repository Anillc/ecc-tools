# CTS 去单例化重构

## Goal

把 iCTS 内部状态从全局单例改为显式对象、显式接口和显式参数传递。`CTSAPI` 可以继续作为外部入口单例，但 `src/operation/iCTS/source/` 内部不再依赖 `*_INST` 宏或 `getInst()` 形式的隐藏全局状态。重构后的代码应更清晰、更可测试，并为后续多 flow / 并行化执行留下真实空间。

本 task 作为父任务和总体验收载体；具体实现拆到子任务执行。父任务负责保持架构原则一致、追踪跨子任务风险，并在所有子任务完成后执行最终验收。

## Background

当前 `.trellis/spec/backend/database-guidelines.md` 仍把 `CONFIG_INST` / `DESIGN_INST` / `WRAPPER_INST` / `STA_ADAPTER_INST` / `LOG_INST` 等列为既有单例边界，这已经落后于本次目标。历史 task `05-20-icts-incremental-refactor-from-origin` 曾尝试 `SingletonRegistry`，最后因 reset 顺序隐式、抽象层过重而撤回；本 task 不应重新引入 registry / service locator / resettable base class 这类间接单例。

本次调研确认当前 iCTS 内部仍大量使用 `_INST`：

| Scope | Count |
|---|---:|
| `src/operation/iCTS/source` + `api` | 371 |
| `src/operation/iCTS/test` | 404 |
| Total | 775 |

Source/API 内部分布：

| Macro | Source/API Uses |
|---|---:|
| `CONFIG_INST` | 99 |
| `SCHEMA_WRITER_INST` | 98 |
| `STA_ADAPTER_INST` | 72 |
| `DESIGN_INST` | 70 |
| `WRAPPER_INST` | 22 |
| `FLOW_INST` | 7 |
| `FAST_STA_INST` | 2 |
| `CTS_API_INST` | 1 |

详细证据记录在 `research/problem-inventory.md`。

## Task Map

实现拆分为 6 个子任务，推荐按下列顺序推进：

| Order | Task | Scope |
|---:|---|---|
| 1 | `05-24-cts-runtime-flow-desingleton` | `CTSAPI` runtime ownership；`Flow` 去 singleton；移除 `FLOW_INST` |
| 2 | `05-24-cts-reporter-config-explicit` | `SchemaWriter` / `Config` 显式化；窄 `{Name}Config` builder；移除 `SCHEMA_WRITER_INST` / `CONFIG_INST` |
| 3 | `05-24-cts-design-wrapper-explicit` | `Design` / `Wrapper` 显式 ownership；read/materialize/writeback 目标可见；移除 `DESIGN_INST` / `WRAPPER_INST` |
| 4 | `05-24-cts-sta-faststa-explicit` | `STAAdapter` / `FastSTA` 显式 dependency；移除 `STA_ADAPTER_INST` / `FAST_STA_INST` |
| 5 | `05-24-cts-synthesis-contract-cleanup` | HTree、Topology、Characterization 输入/配置/输出/summary contract 清理 |
| 6 | `05-24-cts-flow-contract-tests-spec` | Instantiation/Optimization/Evaluation/Report contract 收口；tests fixture 去全局化；spec 更新；最终验收 |

依赖顺序写入各子任务 PRD。父子关系只是 Trellis 组织方式，不替代代码中的显式接口边界。

## Requirements

- 保留 `CTSAPI` 作为对外稳定入口；外部调用可以继续使用 `CTS_API_INST` 或现有 `CTSAPI` 公开方法。
- iCTS 内部状态对象必须由 `CTSAPI` 或明确的 flow/runtime owner 持有，并通过构造参数、函数参数或窄接口传递。
- 最终移除除 `CTS_API_INST` 以外所有 `*_INST` 宏定义和使用，包括 source、api 内部实现和 test。
- 不引入 `SingletonRegistry`、`ServiceLocator`、全局 context、隐藏静态缓存、全局 reset list 抽象，避免把单例换成另一种形式的单例。
- `Config`、`Design`、`Wrapper`、`STAAdapter`、`FastSTA`、`SchemaWriter`、`Flow` 应成为普通对象或明确 facade；生命周期、reset 顺序和所有权从调用链上可见。
- 需要 reporter 的模块显式接收 `schema::SchemaWriter&` 或更窄的 report interface；算法层不直接写全局 reporter。
- 需要读写 design 的 flow 显式接收 `Design&`；真正 commit design 的边界必须可见，不能通过 `DESIGN_INST` 隐式修改。
- 需要 iDB / iSTA / fast STA 的代码通过显式 adapter 参数访问；外部工具对象不得泄漏到算法模块。
- 每个 algorithm / flow 的入口参数收敛为 `{Name}Input` + `{Name}Config`：
  - `{Name}Input` 承载本次运行的设计对象、服务引用、不可变环境事实和业务语义输入。
  - `{Name}Config` 只承载会改变算法行为的用户/flow 决策参数，不放 DBU、work dir、report path、object name prefix、log context、缓存指针等假配置。
- 每个 algorithm / flow 的输出收敛为 `{Name}Output` + `{Name}Summary`：
  - `{Name}Output` 只包含后续设计流程实际需要的设计 payload 或 artifact payload。
  - `{Name}Summary` 包含运行状态、失败原因、日志字段、metrics、diagnostics、QoR 统计和报告用信息。
- HTree、Topology、Characterization、Optimization、Instantiation、Evaluation、Report 都需要按上述 contract 重新梳理；不要只改 HTree。
- 运行环境事实应统一建模，例如 DBU 属于 design/layout input，不属于 HTree/Topology/Char algorithm config。
- 兼容现有 CTS flow 顺序：`setup -> synthesis -> optimization -> instantiation -> evaluation -> report`。
- 公开行为和 QoR 必须保持兼容；除明确清理 fake config / output contract 外，不改变 CTS 算法语义。
- 更新 `.trellis/spec/`，把新的全局开发规范写进去：内部禁用单例、显式 runtime ownership、`{Name}Input/Config/Output/Summary` 契约、config 最小化原则、summary/output 分离原则。spec 更新应短而有执行力，不写流水账。

## Acceptance Criteria

- [x] `rg -n '\b[A-Z][A-Z0-9_]*_INST\b' src/operation/iCTS/source src/operation/iCTS/test src/operation/iCTS/api` 只允许 `CTS_API_INST` 作为外部入口宏出现；source 内部实现不依赖其它 `_INST`。
- [x] `CONFIG_INST` / `DESIGN_INST` / `WRAPPER_INST` / `STA_ADAPTER_INST` / `FAST_STA_INST` / `FLOW_INST` / `SCHEMA_WRITER_INST` 宏定义被删除。
- [x] `Flow` 不再是 singleton；`CTSAPI` 持有 flow/runtime state，reset 生命周期显式。
- [x] `Config` 不再被算法/flow 隐式读取；每个 module/flow 有明确 `{Name}Config`，且只包含真实算法 knobs。
- [x] `Design`、`Wrapper`、`STAAdapter`、`FastSTA`、`SchemaWriter` 通过显式引用/对象传递；测试可创建独立 runtime fixture，不需要污染全局状态。
- [x] HTree 的入口 contract 被拆为 `HTreeInput` + `HTreeConfig`，输出拆为 `HTreeOutput` + `HTreeSummary`；当前 `HTreeSynthesisOptions` / `HTreeSynthesisResult` 中的输入、依赖、日志、summary、design payload 混合问题被清理。
- [x] Topology、Characterization、Optimization、Instantiation、Evaluation、Report 完成同类 contract 梳理，至少没有新的 broad `Options` / `Result` 混合结构。
- [x] DBU、routing/layout readiness、work/report dirs、log context、object name prefix、cache/library references 不再作为算法 config 字段散落在 HTree/Topology 等接口里。
- [x] 现有 CTS unit tests 更新为显式 runtime fixtures；新增或改造测试覆盖两个独立 runtime / flow 连续运行不共享内部状态。
- [x] iCTS 构建和测试通过，至少包括 `bash build.sh`、iCTS test target、一个代表性 `run_iCTS_dev.tcl` flow。
- [x] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` 通过。
- [x] `.trellis/spec/backend/database-guidelines.md`、`quality-guidelines.md`、必要时 `directory-structure.md` 更新为新的规范，删除或改写旧的内部 singleton 规则。

## Out Of Scope

- 不重写 CTS 算法目标函数、QoR 策略或数据结构语义。
- 不引入新的 plugin / command / UI。
- 不扩大到非 iCTS 模块；外部 iDB / iSTA 仅在 adapter 必须调整时做最小改动。
- 不用 `SingletonRegistry` 或类似抽象替代显式参数传递。

## Open Questions

- 无阻塞问题。已拆成 parent/child task；`CTS_API_INST` 仅作为外部入口例外，内部实现应直接访问 `CTSAPI` 持有的 runtime/flow state。
