# Implementation Plan · CTS Explicit Design And Wrapper Boundaries

## Steps

- [x] 记录基线：
  `rg -n 'DESIGN_INST|Design::getInst|WRAPPER_INST|Wrapper::getInst' src/operation/iCTS`
- [x] 删除 `Design` singleton macro/API，改为普通 runtime member。
- [x] 删除 `Wrapper` singleton macro/API，改为普通 runtime member。
- [x] 改造 setup/clock read：显式传 `Design&` 和 `Wrapper&`。
- [x] 改造 `WrapperClockReader.cc`、trace target reader 和 wrapper writeback，消除对全局 design 的访问。
- [x] 改造 `DesignConversion`、instantiation 和 report/evaluation 中的 design 访问。
- [x] 更新 tests/fixtures，替换 `DESIGN_INST` 创建对象和 reset 逻辑。
- [x] 构建验证。

## Validation

```bash
rg -n 'DESIGN_INST|Design::getInst|WRAPPER_INST|Wrapper::getInst' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
bash build.sh
```

## Risk Files

- `src/operation/iCTS/source/database/design/Design.*`
- `src/operation/iCTS/source/database/io/Wrapper*`
- `src/operation/iCTS/source/flow/clock_data_read/*`
- `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.*`
- `src/operation/iCTS/source/flow/evaluation/*`
- `src/operation/iCTS/source/flow/report/*`
- `src/operation/iCTS/test/**`

## Rollback Point

先完成 `Design` 显式传递再完成 `Wrapper` 写回改造。若 wrapper read/write 同时改动导致难以定位问题，以 read/materialize 为第一边界、writeback 为第二边界。
