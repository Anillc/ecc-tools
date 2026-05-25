# CTS Explicit Design And Wrapper Boundaries

## Goal

移除 `DESIGN_INST` 和 `WRAPPER_INST`，让 CTS design ownership、iDB adapter ownership、read/materialize/writeback 的目标对象都从调用链上可见。

## Parent

`.trellis/tasks/05-24-cts-desingleton-refactor`

## Depends On

- `05-24-cts-runtime-flow-desingleton`
- 与 `05-24-cts-reporter-config-explicit` 有交叉文件，执行时需要同步 rebase 或顺序推进。

## Requirements

- `Design` 改为 runtime-owned 普通对象，不再通过 `DESIGN_INST` 访问。
- `Wrapper` 改为 runtime-owned adapter，不再通过 `WRAPPER_INST` 访问。
- `Wrapper` 不隐式拥有 `Design`；read/materialize/writeback 方法必须显式接收 `Design&`、`Clock&`、clock 集合或其它明确 payload。
- `WrapperClockReader`、trace target reader、design conversion、instantiation、evaluation、report visualization 不得写全局 design。
- flow 和 tests 通过显式 `Design&` 创建、查询和提交 CTS 对象。
- 临时算法输出拥有自己的临时对象，commit 到 `Design` 的边界必须可见。
- 保持 iDB 访问封装在 `Wrapper` 内，不让外部模块直接依赖 iDB 内部对象。

## Acceptance Criteria

- [x] `DESIGN_INST` 和 `Design::getInst` 从 `src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test` 移除。
- [x] `WRAPPER_INST` 和 `Wrapper::getInst` 从 `src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test` 移除。
- [x] `Wrapper` 的读入/写回接口显式标明目标 `Design` 或 payload。
- [x] `DesignConversion`、instantiation、evaluation、report visualization 中对设计状态的修改不再依赖全局对象。
- [x] tests 可在一个进程中构建两个独立 `Design`/`Wrapper` fixture，互不污染。
- [x] `bash build.sh` 或本阶段选定的 iCTS 构建目标通过。

## Out Of Scope

- 不重写 iDB/iSTA 外部 API。
- 不改变 `Design` 内部对象语义或 ownership 规则，除非为显式生命周期必须做最小调整。
- 不负责 HTree output/summary 拆分，但要避免新增隐式 design commit。
