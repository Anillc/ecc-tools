# CTS Synthesis Contract Cleanup

## Goal

在去单例化基础上重构 synthesis 层 contract，重点清理 HTree、Topology、Characterization 的 broad `Options` / `Result` 混合结构，统一为 `{Name}Input` + `{Name}Config` 和 `{Name}Output` + `{Name}Summary`。

## Parent

`.trellis/tasks/05-24-cts-desingleton-refactor`

## Depends On

- `05-24-cts-runtime-flow-desingleton`
- `05-24-cts-reporter-config-explicit`
- `05-24-cts-design-wrapper-explicit`
- `05-24-cts-sta-faststa-explicit`

## Requirements

- `HTreeSynthesisOptions` 拆为 `HTreeInput` + `HTreeConfig` 或等价命名。
- `HTreeSynthesisResult` 拆为 `HTreeOutput` + `HTreeSummary` 或等价命名。
- `Topology`、sink branch、source trunk、sink load clustering、sink load region、CharacterizationLibrary、characterization builder 使用同样分类原则。
- DBU、clock period、object name prefix、reporter、STA adapter、char library/cache、fixed root location、semantic role 属于 input，不属于 config。
- force branch buffer、root driver sizing、boundary relaxation、analytical solver、target depth、depth window、tolerance、max fanout、char lattice knobs 属于 config。
- design payload 和 metrics/status 分开；output 不放 failure reason、log rows、diagnostics、candidate counts。
- summary 不拥有 design objects。
- 不用一个新的 broad wrapper 把 input/config/output/summary 重新混在一起。

## Acceptance Criteria

- [x] `HTreeSynthesisOptions` 被删除、重命名或缩减为不混合依赖/input/config 的结构。
- [x] `HTreeSynthesisResult` 被删除、重命名或缩减为不混合 design payload 和 summary/report 的结构。
- [x] HTree build 调用点可清晰看到 `HTreeInput`、`HTreeConfig`、`HTreeOutput`、`HTreeSummary` 的边界。
- [x] Topology/Characterization 入口不再读取全局 config/reporter/STA/design，并遵守 input/config 分类。
- [x] DBU 和其它 run invariant 不再作为 HTree/Topology/Char config 字段散落传递。
- [x] HTree/Topology/Characterization 相关 tests 更新为显式 input/config。
- [x] `rg -n 'struct .*Options|struct .*Result|BuildOptions|BuildResult' src/operation/iCTS/source/flow/synthesis src/operation/iCTS/source/module` 的剩余命中被逐项确认：要么符合新语义，要么记录后续处理。
- [x] `bash build.sh` 或本阶段选定的 iCTS 构建目标通过。

## Out Of Scope

- 不改变 HTree/Topology 搜索算法和 QoR 决策。
- 不负责 Optimization/Evaluation/Report 的最终 contract 收口；由 `05-24-cts-flow-contract-tests-spec` 处理。
