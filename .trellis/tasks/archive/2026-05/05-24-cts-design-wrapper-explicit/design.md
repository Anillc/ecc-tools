# Design · CTS Explicit Design And Wrapper Boundaries

## Ownership

`CTSRuntime` 持有：

```cpp
Design design;
Wrapper wrapper;
```

`Design` 是 CTS 内部对象 owner。`Wrapper` 是 iDB adapter 和 cross-reference map holder。二者生命周期由 runtime 协调，但 `Wrapper` 不通过全局状态查找当前 design。

## Adapter Contracts

推荐方向：

```cpp
void Wrapper::read(Design& design, const WrapperReadInput& input);
void Wrapper::readClocks(Design& design, const ClockReadInput& input);
InstantiationSummary Wrapper::writeClocksDetailed(const Design& design, const WritebackConfig& config);
```

如果 writeback 只需要 clock 集合，则传 `Span<Clock*>` 或明确的 clock payload，而不是整包 runtime。

## Commit Boundary

算法临时输出和最终 design commit 分开：

- HTree/Topology 等输出可以短期持有 `unique_ptr<Inst/Pin/Net>`。
- instantiation/design conversion 负责把 output commit 到 `Design&`。
- 所有 commit 函数签名必须出现 `Design&` 或明确目标对象。

## Test Direction

测试 fixture 应构建局部 `Design`、`Wrapper` 或完整 `CTSRuntime`。旧的 singleton reset fixture 要逐步删除，避免测试顺序依赖。
