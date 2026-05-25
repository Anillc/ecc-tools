# Design · CTS Explicit STA And FastSTA Dependencies

## STAAdapter Boundary

`STAAdapter` 是 iCTS 与 iSTA 之间的 facade。目标是让调用方显式看到它：

```cpp
struct HTreeInput
{
  STAAdapter& sta_adapter;
  // other design/runtime facts
};
```

如果底层 API 必须通过静态 iSTA 入口访问，静态细节留在 `STAAdapter` 内部，不能泄漏到 HTree/Topology/Optimization。

## FastSTA Boundary

`FastSTA` 持有 mutable context，例如 clock state、liberty cache、RC/timing approximation。目标：

- mutable context 是 `FastSTA` 成员。
- builder 和 optimization 接收 `FastSTA&`。
- 静态 helper 只允许无状态纯函数；任何读写 context 的函数都应为成员函数。

## Config Rule

STA/FastSTA 对象是 service dependency，不属于 `{Name}Config`。会影响算法选择的 STA 相关阈值放入 config；查询接口、library/cache/context 放入 input。

## Threading Note

显式 dependency 只表示 ownership 清晰，不表示底层 STA adapter 可并发。spec 更新时需要写明并行化必须另行验证 adapter thread-safety。
