# Design · CTS Runtime And Flow Desingleton

## Ownership

`CTSAPI` 是唯一外部入口，可以继续支持 `CTS_API_INST`。内部引入普通 runtime owner：

```cpp
struct CTSRuntime
{
  Config config;
  Design design;
  Wrapper wrapper;
  STAAdapter sta_adapter;
  FastSTA fast_sta;
  schema::SchemaWriter reporter;
};
```

该结构是生命周期 owner，不是服务定位器。`CTSAPI` 和顶层 `Flow` 可以看到完整 runtime；`Setup`、`Synthesis`、`Optimization`、`Report` 等只接收自己需要的对象引用。

## Flow Boundary

`Flow` 改为普通类：

- 构造函数不访问 singleton。
- run-local state 保留在 `Flow` 成员中。
- flow 方法接收 `CTSRuntime&` 或更窄的 flow input，作为后续任务逐步收窄的临时边界。
- `Flow::reset()` 只清理自身 run-local state。

## Reset Model

`CTSAPI::resetAPI()` 不再逐个 reset 全局 singleton。推荐语义：

- reset runtime-owned objects，或直接重建 `CTSRuntime`。
- reset/rebuild `Flow`。
- 后续子任务在去除各对象 singleton 后逐步删除对应全局 reset 路径。

## Compatibility

本任务只改变内部持有方式。若部分对象仍暂时保留 singleton API，为了阶段性编译可以短期存在，但 `Flow` 自身必须先完成去单例化，且不得引入新的全局 runtime accessor。
