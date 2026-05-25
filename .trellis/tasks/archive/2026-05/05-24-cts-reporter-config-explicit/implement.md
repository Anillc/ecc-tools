# Implementation Plan · CTS Explicit Reporter And Config Boundaries

## Steps

- [x] 记录基线：
  `rg -n 'SCHEMA_WRITER_INST|SchemaWriter::getInst|CONFIG_INST|Config::getInst' src/operation/iCTS`
- [x] 将 `SchemaWriter` 变为 runtime-owned 普通对象，删除 singleton macro/API。
- [x] 改造 `schema::Emit*` helper，显式传入 `SchemaWriter&`。
- [x] 从 flow/setup/evaluation/report 开始替换 reporter 调用，再推进到 HTree、TopologyGen、Characterization builder。
- [x] 将 `Config` 变为 runtime-owned 普通对象，保留解析接口兼容。
- [x] 在 setup/flow 边界建立窄 config builder，先覆盖高密度文件：`Setup.cc`、`HTree.cc`、`Plan.cc`、`Constraint.cc`、`CharacterizationLibrary.cc`、`Optimization.cc`。
- [x] 删除或迁移 DBU、work/report dir、object prefix、reporter/cache/library 指针等假配置下传。
- [x] 更新 tests/fixtures，显式构造 config 和 reporter。
- [x] 构建验证。

## Validation

```bash
rg -n 'SCHEMA_WRITER_INST|SchemaWriter::getInst|CONFIG_INST|Config::getInst' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
bash build.sh
```

## Risk Files

- `src/operation/iCTS/source/utils/logger/Schema.*`
- `src/operation/iCTS/source/database/config/Config.*`
- `src/operation/iCTS/source/flow/setup/Setup.*`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.*`
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/library/*`
- `src/operation/iCTS/source/module/characterization/*`
- `src/operation/iCTS/source/module/topology/*`
- `src/operation/iCTS/source/flow/optimization/*`
- `src/operation/iCTS/test/**`

## Rollback Point

先让 reporter 独立构建通过，再移除 `CONFIG_INST`。如果两者耦合过深，优先拆出 reporter，因为它对算法语义影响较小。
