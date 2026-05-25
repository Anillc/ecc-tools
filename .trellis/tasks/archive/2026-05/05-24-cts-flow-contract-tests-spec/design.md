# Design · CTS Flow Contracts Tests And Spec Finalization

## Remaining Flow Contracts

Instantiation:

- `InstantiationInput`: `Design&`、`Wrapper&`、reporter、writeback payload。
- `InstantiationConfig`: writeback mode 等真实开关；若无真实 knob 可以为空。
- `InstantiationOutput`: writeback artifact 或 committed object references。
- `InstantiationSummary`: counts、failure reason、diagnostics。

Optimization:

- `OptimizationInput`: `Design&`、clock layout、char library、`FastSTA&`、`STAAdapter&`、reporter。
- `OptimizationConfig`: skew bound、allowed sizing cells、cap/slew constraints。
- `OptimizationOutput`: accepted edits 或 changed design payload。
- `OptimizationSummary`: sizing metrics、QoR deltas、warnings。

Evaluation:

- `EvaluationInput`: `Design&`、optional clock layout、`Wrapper&`、`STAAdapter&`、reporter。
- `EvaluationConfig`: refresh STA timing、report timing toggle。
- `EvaluationOutput`: `EvaluationState`。
- `EvaluationSummary`: QoR summary。

Report:

- `ReportInput`: summaries/evaluation state、report paths、reporter、visualization 所需 design/wrapper。
- `ReportConfig`: requested formats and artifact toggles。
- `ReportOutput`: generated artifact paths。
- `ReportSummary`: per-artifact status。

## Test Model

Tests should create local runtime fixtures:

```cpp
struct CTSTestRuntime
{
  CTSRuntime runtime;
  Flow flow;
};
```

Narrow unit tests may build only the needed dependency subset. Avoid shared singleton reset in fixture setup/teardown.

## Spec Model

Specs should state durable rules, not task history:

- internal singleton ban with `CTSAPI` exception;
- runtime owner only at API/flow boundary;
- narrow dependency passing;
- module-qualified `{Name}Input/Config/Output/Summary`;
- config minimality;
- output/summary separation;
- no service locator/global context replacement.
