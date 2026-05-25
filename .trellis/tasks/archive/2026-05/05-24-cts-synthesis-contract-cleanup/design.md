# Design · CTS Synthesis Contract Cleanup

## HTree Target Shape

目标 contract：

```cpp
HTreeBuildResult buildHTree(const HTreeInput& input, const HTreeConfig& config);
```

其中 `HTreeBuildResult` 如需存在，只能是薄 transport：

```cpp
struct HTreeBuildResult
{
  HTreeOutput output;
  HTreeSummary summary;
};
```

不得继续把 input echoes、report data、payload、状态混在一个 result 中。

## Field Classification

`HTreeInput`：

- `Net& root_net`
- loads / root pin / fixed root location
- DBU、clock period、object name prefix
- `CharacterizationLibrary&`
- `STAAdapter&`
- `schema::SchemaWriter&` 或窄 report sink
- load role / stage role 等业务语义

`HTreeConfig`：

- force branch buffer
- enable root driver sizing
- boundary relaxation
- analytical solver
- target depth / depth search window
- topology tolerance
- max fanout

`HTreeOutput`：

- topology/tree/level plan
- inserted inst/pin/net payload
- root input/output pins
- later-flow artifact payload

`HTreeSummary`：

- success/failure reason
- selected depth
- candidate/pruning counts
- char/analytical/root-driver compensation summaries
- warnings/diagnostics

## Topology And Characterization

Topology and characterization should mirror the same split. If a component is always used together with HTree, prefer binding it at synthesis input level and passing narrower references down, rather than making every low-level function repeat the full dependency list.

## Naming

Use module-qualified names (`HTreeInput`, `CharacterizationConfig`, `TopologySummary`). Avoid standalone `Input`, `Output`, `Options`, or `Result` names.
