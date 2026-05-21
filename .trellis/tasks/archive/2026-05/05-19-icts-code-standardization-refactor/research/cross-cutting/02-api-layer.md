# Research: api/ Layer Analysis

- **Query**: iCTS api 层设计、稳定性、是否暴露了不该暴露的内部 API、与 source 的关系
- **Scope**: internal
- **Date**: 2026-05-19

## api/ 目录结构

```
api/
├── CMakeLists.txt   (20 行)
├── CTSAPI.hh        (64 行, 64 行实际 ~40)
└── CTSAPI.cc        (102 行)
```

整个 api 层就两个文件、一个类。没有子目录、没有 namespace 分层、没有 internal 头。

## CTSAPI 接口完整列表

```cpp
class CTSAPI
{
 public:
  static auto getInst() -> CTSAPI&;

  // CTS CLI
  static auto runCTS() -> void;
  static auto report(const std::string& save_dir) -> void;

  // Flow API
  static auto resetAPI() -> void;
  static auto init(const std::string& config_file, const std::string& work_dir = "") -> void;

  // Feature API
  static auto outputSummary() -> ieda_feature::CTSSummary;
};
```

5 个静态方法 + 1 个 `getInst`。配套宏：`#define CTS_API_INST (icts::CTSAPI::getInst())`。

## 2.1 CTSAPI 是 thin wrapper 还是有额外职责？

读 `CTSAPI.cc` 后：

| API | 实现 | 性质 |
|---|---|---|
| `runCTS()` | `FLOW_INST.runCTS()` | thin |
| `report(dir)` | `FLOW_INST.report(dir)` | thin |
| `resetAPI()` | 五个全局单例 reset：CONFIG/DESIGN/WRAPPER/FLOW/SCHEMA_WRITER | 协调多个全局态，**不是 thin** |
| `init(cfg, work_dir)` | `resetAPI()` + `Setup::initialize()` + `FLOW_INST.setSetupReady()` + `FLOW_INST.outputRuntimeSetup()` | **有逻辑** |
| `outputSummary()` | `buildFeatureSummary(FLOW_INST.outputSummary())` —— 把 QorSummary → ieda_feature::CTSSummary | **有翻译逻辑** |

结论：**api/ 不是纯 thin wrapper**。它承担了：
1. 多单例 reset 协调
2. setup 序列编排
3. feature summary 翻译（QorSummary → ieda_feature::CTSSummary）

## 2.2 api 与 source/flow 关系

- api 必须 `#include` 这些 source 内部 header：
  ```cpp
  #include "database/config/Config.hh"
  #include "database/design/Design.hh"
  #include "database/io/Wrapper.hh"
  #include "evaluation/qor/QorEvaluation.hh"   // 间接来自 flow/evaluation/qor/
  #include "feature_icts.h"
  #include "feature_ista.h"
  #include "flow/Flow.hh"
  #include "flow/setup/Setup.hh"
  #include "utils/logger/Schema.hh"
  ```
- 这意味着 `CTSAPI.cc` 直接耦合：database/config、database/design、database/io、flow/evaluation/qor、flow/、flow/setup、utils/logger。
- api 的链接：
  ```cmake
  target_link_libraries(
    icts_api
    PRIVATE
    icts_source                    # source 全集合
    icts_api_external_libs)
  ```
  其中 `icts_api_external_libs INTERFACE: idm ista-engine log usage feature_db`。

### 是否有"内部 API"被错误地暴露？
- CTSAPI.hh 公开的 5 个方法都是高层入口，**未泄漏** Config/Flow/Setup/Schema 实例（实现细节都在 .cc）。
- 但 .cc 直接调用 `FLOW_INST.runCTS()` —— 真正的实现在 `flow/Flow.hh`，并且 `flow/Flow.hh` 也定义了 `getInst()` 单例和 `FLOW_INST` 宏。如果其他模块绕过 api 直接 `#include "flow/Flow.hh"`，会形成双入口。**实际未发生**（grep 显示 source/ 内没有用 `CTS_API_INST` 或 `CTSAPI::`，但 source/flow 内的子模块直接调用 FLOW_INST 是普遍现象）。
- 也就是说：`FLOW_INST` 是真正的"内部 API"，但因为定义在 source/flow/Flow.hh 里，并暴露给 source/ 中所有子模块用 → 这是事实上的"内部模块间总线"。从 api/ 角度看是 ok 的，但 source 内部其实是失控的。

## 2.3 外部调用方

api 真正的"消费方"在外部：

| 调用方 | 用法 |
|---|---|
| `src/interface/tcl/tcl_icts/tcl_cts.cpp:103` | `CTS_API_INST.report(str_path);` |
| `src/interface/python/py_icts/py_icts.cpp:31` | `CTS_API_INST.report(path);` |
| `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:44,45,58` | `init`/`runCTS`/`report` |
| `src/feature/builder/feature_builder_tool.cpp:59` | `outputSummary()` |
| `iCTS/test/flow/FlowTest.cc` | `runCTS()` / `outputSummary()` / `resetAPI()` |

5 个外部入口点，API 暴露面只有 5 个方法 —— **面非常小**，符合"thin API"的应有形态。

## 2.4 命名规范 / 参数风格

| 维度 | 现状 | 评价 |
|---|---|---|
| 类名 | `CTSAPI` | 全大写不一致 — 工程其他位置都用 PascalCase（Config / Wrapper / Design）。`CTSAPI` 像 C-style，但又是 class。改成 `CtsApi` 或 `CTSApi` 更符合工程基调。 |
| 命名空间 | `icts::CTSAPI` | 只在顶层 icts，无 namespace 分层 |
| 方法名 | runCTS / report / resetAPI / init / outputSummary | 风格不齐：`runCTS`(中缀大写) + `report`(全小写) + `resetAPI`(后缀大写) + `outputSummary`(camelCase) |
| 单例宏 | `#define CTS_API_INST` | 大写 + 下划线，和工程其他 INST 一致 |
| 参数 | `const std::string&` 居多 | 风格一致 |
| 返回类型 | `auto -> ...` | 风格一致 |
| 错误处理 | 无 | `init` 失败只是 `FLOW_INST.setSetupReady(false)` 然后 return；没有错误码、没有异常、没有诊断。外部 caller 不知道失败原因。 |

## 2.5 是否稳定？

- 接口数量稳定（5 个方法），近期 git 没频繁加新方法。
- 但内部实现 `resetAPI()` 列举 5 个全局单例 `reset()` —— 如果再加新的全局状态（如 fast_sta 引擎、Optimizer 状态），这个函数会膨胀且容易遗漏。**这是一个隐式契约**：所有新单例都要在 `resetAPI` 里加 reset 调用，但没有任何机制强制。

## 2.6 总结

- api 层是 **超薄的（5 方法 + 1 单例）**，但不是 thin wrapper：承担了 reset 编排、feature summary 翻译两类逻辑。
- 没有"内部 API 暴露"问题（外部只能看到 5 个公开方法）。
- 命名风格的小不一致（CTSAPI 全大写、方法名风格混杂）。
- `resetAPI` 的"全局单例列表"是隐式契约，不健壮。
- api 与 source 的方向是 **api → source 单向依赖**，没有反向；这点干净。

## Caveats / Not Found

- `CTSSummary` / `QorSummary` 的字段是否覆盖了所有 feature 需要的字段，没有跨 `feature_icts.h` 验证。
- api 是否对外提供 C 接口（用于 Tcl/Python binding），暂未验证 — 看上去全是 C++ 链接（`CTS_API_INST.xxx`）。
