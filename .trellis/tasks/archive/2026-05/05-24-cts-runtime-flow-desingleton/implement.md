# Implementation Plan · CTS Runtime And Flow Desingleton

## Steps

- [x] 记录阶段前基线：`rg -n 'FLOW_INST|Flow::getInst' src/operation/iCTS`。
- [x] 在 API/flow 边界定义 `CTSRuntime` 或等价 owner，放在不会暴露给算法层的位置。
- [x] 修改 `CTSAPI.hh/.cc`，让 `CTSAPI` 持有 runtime 和 `Flow`。
- [x] 修改 `Flow.hh/.cc`，删除 singleton inheritance/API/macro。
- [x] 把 `CTSAPI.cc` 中的 `FLOW_INST` 调用改成成员对象调用。
- [x] 保持其它 singleton 宏暂时不动，只做必要适配以保持构建通过。
- [x] 更新受影响测试 fixture 的最小入口调用。
- [x] 构建验证。

## Validation

```bash
rg -n 'FLOW_INST|Flow::getInst' src/operation/iCTS
bash build.sh
```

Latest phase validation:

```bash
rg -n '\bFLOW_INST\b|Flow::getInst|getInst\(\) -> Flow' src/operation/iCTS/api src/operation/iCTS/source src/operation/iCTS/test
ninja -C build icts_source_flow icts_api icts_test_flow
./bin/icts_test_flow
```

Result: no `FLOW_INST` / `Flow::getInst` matches, `icts_test_flow` passed 29/29.

## Risk Files

- `src/operation/iCTS/api/CTSAPI.hh`
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/flow/Flow.hh`
- `src/operation/iCTS/source/flow/Flow.cc`
- `src/operation/iCTS/test/**/Flow*`

## Rollback Point

在只移除 `FLOW_INST` 后保持构建绿色，再进入 reporter/config 或 design/wrapper 子任务。
