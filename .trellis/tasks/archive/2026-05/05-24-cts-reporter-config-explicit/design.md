# Design · CTS Explicit Reporter And Config Boundaries

## Reporter Boundary

Reporter 是运行期输出目的地，不属于算法配置。目标形态：

```cpp
void emitSomething(schema::SchemaWriter& reporter, const SomethingSummary& summary);
```

可选方案：

- 直接传 `schema::SchemaWriter&`，适合 flow/report 层。
- 引入窄 `ReportSink`，仅在测试需要 fake 且 writer API 过宽时使用。

禁止创建全局 current reporter，也禁止通过 `CTSRuntime&` 在深层模块临时取 reporter。

## Config Boundary

`Config` 保留为配置文件解析结果和兼容层，由 runtime 持有。flow 边界负责把它转换成窄 config：

- `CTSFlowConfig`：flow 开关和阶段选择。
- `SynthesisConfig`：综合阶段开关与组合配置。
- `HTreeConfig`：HTree 算法 knobs。
- `CharacterizationConfig`：char lattice、buffer choices、约束 knobs。
- `OptimizationConfig`：优化目标和约束。
- `EvaluationConfig` / `ReportConfig`：是否刷新 timing、输出格式等。

## Classification Rule

- 放入 `Input`：设计对象、adapter/reporter 引用、DBU、clock period、report path、object prefix、缓存/库引用、运行环境事实。
- 放入 `Config`：会改变搜索空间、约束、启发式、enable/disable 行为的参数。
- 放入 `Summary`：运行状态、metrics、diagnostics、report rows。

## Migration Note

先保持现有配置文件字段兼容，再逐步移除算法层假配置。字段是否继续出现在 JSON/YAML 解析里，不等于它能继续作为算法 config 字段下传。
