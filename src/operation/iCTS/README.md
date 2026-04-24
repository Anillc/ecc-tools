## iCTS

iCTS 是 iEDA 的 Clock Tree Synthesis 模块。

这一轮重构没有改外部命令语义，`run_cts` / `cts_report` 仍然是对外入口；调整的是内部代码的组织方式、flow 边界和依赖关系。现在读 iCTS，建议先看 `source/module/flow`，再顺着 `router -> synthesis -> committer / evaluator -> utils` 往下看。

## 当前入口

对外链路保持不变：

`run_cts`
-> Tcl / Python
-> `tool_manager`
-> `CtsIO`
-> `CTSAPI::init`
-> `CTSAPI::runCTS`
-> `CTSFlowRunner::run`

`cts_report`
-> Tcl / Python
-> `tool_manager`
-> `CtsIO`
-> `CTSAPI::report`
-> `CTSFlowRunner::report`

`CTSAPI` 现在只保留 façade 和生命周期职责，内部 flow 编排已经收口到 `source/module/flow/`。

## 目录结构

当前 `source/` 只保留三个一级模块：

- `source/data_manager/`
  配置解析、CTS 领域对象、DB 适配、报表和输出基础设施。
- `source/module/`
  flow 直接调用的业务模块。
- `source/utils/`
  算子、算法、内部 IR、数学工具和其他基础能力。

主要目录职责如下：

- `api/`
  只保留 `CTSAPI.hh/.cc`。
- `source/module/flow/`
  `CTSFlowRunner`，负责 `init / run / report / summary`。
- `source/module/session/`
  `CTSSession` 和 `CTSState`，负责一次 CTS 运行的会话状态。
- `source/module/router/`
  逐时钟网调度，收口 net 级综合和结果回写。
- `source/module/synthesis/`
  业务级综合入口，`Solver` 保持薄封装。
- `source/module/committer/`
  把综合结果写回 CTS design / IDB，并同步 timing 视图。
- `source/module/evaluator/`
  时序评估、metrics、statistics 和调试输出。
- `source/module/runtime/`
  runtime 接口和适配层。
- `source/utils/synthesis_ir/`
  综合内部 IR。
- `source/utils/synthesis_operator/`
  net 级算子链。
- `source/utils/tree_builder/`
  树构建算法。
- `source/utils/timing_propagator/`
  RC / timing 传播能力。
- `source/utils/balance_clustering/`
  聚类和优化能力。
- `source/utils/math/`
  数学和拟合工具，原来零散的 `model` 职责已经并到这里。

`solver` 这个旧一级目录已经拆散：

- 业务语义明确的部分进入 `module/`
- 算法、IR、算子进入 `utils/`

## 当前 Flow

主流程按下面这条线理解：

`init -> readData -> routing -> evaluate -> writeGDS -> summary`

各阶段职责：

- `CTSFlowRunner::init`
  解析配置，处理 `work_dir`，创建 session、design、db wrapper、log 和 runtime 上下文。
- `CTSFlowRunner::readData`
  读取 clock net 名称，并通过 `CtsDBWrapper` 把 DB 数据转换成 CTS 侧对象。
- `CTSFlowRunner::routing`
  调用 `Router` 做逐 clock net 调度。
- `Router`
  负责逐网检查、调用 `module/synthesis` 的业务入口，并通过 `DesignCommitter` 回写结果。
- `CTSFlowRunner::evaluate`
  调用 `Evaluator` 做 timing evaluate 和指标准备。
- `CTSAPI::writeGDS`
  输出设计 GDS、flyline GDS 以及调试导出。

`cts_report` 是单独的 report 分支，不会重跑 CTS 主流程。当前语义是：

- 如果当前 session 里已经完成过 evaluator/timing 评估，就直接复用已有结果输出报表。
- 如果 report 在没有可复用评估状态的场景下单独调用，才补做 evaluator 准备。

这次修复后，report 阶段会明确打印 `Start CTS Report`，并区分是 `reuse` 还是 `rebuild evaluator timing`。

## Net 级综合

`module/synthesis/Solver` 现在只是业务入口，真正的 net 级算子链放在 `source/utils/synthesis_operator/`。

当前默认顺序：

`SinkClusteringOperator`
-> `TopologyBuilderOperator`
-> `LongWireBufferingOperator`
-> `LevelSizingOperator`

如果后续要加新能力，放置原则很简单：

- 能直接对应 CTS 业务阶段、会被 flow 调用的，放到 `module/`
- 算子、算法、IR、数学工具，放到 `utils/`
- 配置、DB、输出基础设施，放到 `data_manager/`

## CMake 和开发约定

iCTS 现在按目录细化 target，目录和 target 尽量一一对应。新增目录时，默认也要补自己的 `CMakeLists.txt` 和 target。

开发时保持这些约束：

- `api/` 不再放内部 flow
- include 不用相对路径，依赖路径通过 target include 提供
- 能前向声明的类型优先前向声明
- 非外部可见实现尽量收口在 `.cc` 内部
- target 链接按职责收敛，避免无边界透传

## 输出和运行

常见输出：

- `<work_dir>/cts.log`
- `<work_dir>/output/cts_design.gds`
- `<work_dir>/output/cts_flyline.gds`
- `<work_dir>/statistics/*.rpt`

常用脚本：

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

脚本里的核心调用：

```tcl
run_cts -config $CTS_CONFIG -work_dir $CTS_WORK_DIR
def_save -path $OUTPUT_DEF
netlist_save -path $OUTPUT_VERILOG -exclude_cell_names {}
cts_report -path $CTS_WORK_DIR
```
