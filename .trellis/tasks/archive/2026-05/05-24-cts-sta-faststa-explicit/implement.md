# Implementation Plan · CTS Explicit STA And FastSTA Dependencies

## Steps

- [x] 记录基线：
  `rg -n 'STA_ADAPTER_INST|STAAdapter::getInst|FAST_STA_INST|FastSTA::getInst' src/operation/iCTS`
- [x] 将 `STAAdapter` 改为 runtime-owned facade，删除 singleton macro/API。
- [x] 改造 setup 和 evaluation，使 adapter 初始化/查询通过显式对象完成。
- [x] 改造 CharacterizationLibrary、HTree compensation、Topology sink/trunk、Optimization 的 STA 查询。
- [x] 将 `FastSTA` context 读写改为成员函数，删除 singleton macro/API。
- [x] 改造 fast_sta builder、liberty、clock state、optimization 中的 FastSTA 调用。
- [x] 更新 tests/fixtures 为显式 adapter/runtime。
- [x] 构建验证。

## Validation

```bash
rg -n 'STA_ADAPTER_INST|STAAdapter::getInst|FAST_STA_INST|FastSTA::getInst' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
bash build.sh
```

## Risk Files

- `src/operation/iCTS/source/database/adapter/sta/*`
- `src/operation/iCTS/source/database/adapter/fast_sta/*`
- `src/operation/iCTS/source/flow/setup/Setup.*`
- `src/operation/iCTS/source/flow/evaluation/*`
- `src/operation/iCTS/source/flow/optimization/*`
- `src/operation/iCTS/source/flow/synthesis/htree/*`
- `src/operation/iCTS/source/flow/synthesis/topology/*`
- `src/operation/iCTS/source/module/characterization/*`
- `src/operation/iCTS/test/**`

## Rollback Point

先完成 `STAAdapter&` 显式传递，再处理 `FastSTA` mutable context。若 FastSTA 静态函数中有纯函数，可保留为 free/static helper，但不得读写共享 context。
