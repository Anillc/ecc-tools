# iCTS 代码规范化：FastSTA 单例 reset + test 入侵隔离 + 过程注释清理

> **任务定位**：轻量规范化（PRD-only）。3 项独立小改造，最终归一次 commit。
> **复用前一 task 的工作基线**：build PASS、iCTS 重构（BST + Pimpl）已完成。本任务只做规范化，不动算法。
> **基线 commit**：`597cc31b8`（origin/cts_refactor）+ 前一 task `05-20-icts-incremental-refactor-from-origin` 的本地未 commit 改动。

---

## 1. 背景

前一 task 完成后留下 3 个未规范点，用户在本 task 一次性处理：

1. **FastSTA 接口范式不统一**：FastSTA 已是单例（`getInst()` + `FAST_STA_INST` 宏），但暴露的清理接口是 `static auto clear() -> void`，而其他 6 个单例（CONFIG / DESIGN / WRAPPER / FLOW / SCHEMA_WRITER / STA_ADAPTER）用 instance `reset()`。`CTSAPI::resetAPI` 调用风格不一致（其他全是 `XXX_INST.reset()`，FastSTA 是 `FastSTA::clear()`）。
2. **Test 代码入侵 source**：`source/database/adapter/fast_sta/FastSta.hh` 含 `namespace icts_test { class FastStaTestAccess; }` forward decl + `friend class ::icts_test::FastStaTestAccess;`。即便只是 forward decl + friend 声明，按用户规范"source 中不允许出现任何 test 痕迹"也违规。
3. **开发过程注释残留**：`src/operation/iCTS/api/CTSAPI.cc:83-86` 的 4 行注释（"FastSTA owns clock and characterization contexts; clear them on every API teardown..."）是前一 task agent 留下的解释性注释，属于 commit message / decision log 范畴，不应留在代码里。需要全面扫描 source/ 与 api/ 是否还有同类。

---

## 2. 范围

**In scope**：
- M1 · FastSTA 单例 `reset()` 接口（替代 static `clear()`）
- M2 · Test 代码入侵 source 的隔离（含 FastStaTestAccess + 类似全扫描）
- M3 · source/ + api/ 内过程注释全面清理（含 CTSAPI.cc 已知点 + 全扫描）

**Out of scope**：
- 任何算法语义改动
- 任何公开 API（CTSAPI 5 个公开方法）签名改动
- test/ 下代码的改动（除非是配合 M2 必须的 testAccess 实现迁移）
- ecc dev 检查在 agent 内执行（**全部实现后由用户决定何时跑统一检查**）
- BST / CharBuilder / 其他模块的进一步重构

---

## 3. 移植清单

### M1 · FastSTA 单例化的 `reset()` 接口

**改动**：
| 文件 | 当前 | 目标 |
|---|---|---|
| `source/database/adapter/fast_sta/FastSta.hh:229` | `static auto clear() -> void;` | `auto reset() -> void;`（去 static） |
| `source/database/adapter/fast_sta/FastSta.cc:235-242` | `auto FastSTA::clear() -> void { auto& adapter = getInst(); adapter._contexts->...; }` | `auto FastSTA::reset() -> void { _contexts->clock_contexts.clear(); _contexts->clock_context_valid.clear(); _contexts->char_contexts.clear(); _contexts->char_context_valid.clear(); }`（直接走 this） |
| `api/CTSAPI.cc:83-87` | `// FastSTA owns ... \n FastSTA::clear();` | `FAST_STA_INST.reset();`（删除 4 行注释 + 调用风格改单例） |

**约束**：
- `clear()` 的语义不变（4 个 vector clear），只是改为 instance method
- 不破坏其他调用者：grep 确认 `FastSTA::clear()` 仅有 `api/CTSAPI.cc:87` 一处调用（已验证）

### M2 · Test 代码入侵 source 隔离

#### M2.1 已确认入侵点（必须修）

`source/database/adapter/fast_sta/FastSta.hh`：
- Line 38-40：`namespace icts_test { class FastStaTestAccess; }` forward decl
- Line 263：`friend class ::icts_test::FastStaTestAccess;`
- Line 268-270：`static auto queryClockContext(FastStaClockId clock_id) -> const FastStaClockContext*;` + `static auto mutableClockContext(...)` + `static auto registerClockContextForTest(...)`（这 3 个 private static 是为 test 服务，命名也含 `ForTest`）

#### M2.2 全面排查（agent 必做）

agent 在实施前先跑这些 grep 把入侵清单收齐：

```bash
# 1. friend class 含 Test/Fixture/Access 关键词
grep -rnE 'friend class .*(Test|Fixture|Access)' src/operation/iCTS/source src/operation/iCTS/api

# 2. namespace icts_test 或类似 test 命名空间引用
grep -rnE 'namespace .*test.* \{|icts_test::' src/operation/iCTS/source src/operation/iCTS/api

# 3. 方法名 / class 名含 ForTest / TestOnly / Testing
grep -rnE 'forTest|ForTest|TestOnly|test_only|ForTesting' src/operation/iCTS/source src/operation/iCTS/api

# 4. include test 路径或 gtest/gmock
grep -rnE '#include.*"test/|#include.*<gtest|#include.*<gmock' src/operation/iCTS/source src/operation/iCTS/api

# 5. FRIEND_TEST 宏
grep -rn 'FRIEND_TEST' src/operation/iCTS/source src/operation/iCTS/api

# 6. 任何注释提及 "for test" / "test only" / "test access"
grep -rniE '// .*(for test|test only|test access|testing purposes)' src/operation/iCTS/source src/operation/iCTS/api
```

每条命中都纳入 M2 清理目标。**预期目标命中数 = 0** 在清理后。

#### M2.3 隔离方案

**首选方案**（"public 受控 internal API"）：
- 删除 `FastSta.hh` 内：
  - `namespace icts_test { class FastStaTestAccess; }` forward decl
  - `friend class ::icts_test::FastStaTestAccess;` friend 声明
- 把当前 3 个 `private static` 的"为 test 服务" 方法**提升为 public**，并适度改名去掉 `ForTest` 后缀：
  - `private: static auto queryClockContext(...)` → `public: static auto queryClockContext(...)`（本来就是 query 语义，public 也合理）
  - `private: static auto mutableClockContext(...)` → `public: static auto mutableClockContext(...)`
  - `private: static auto registerClockContextForTest(...)` → `public: static auto registerClockContext(...)`（删 `ForTest` 后缀，命名通用化）

  这 3 个方法本质就是 FastSTA 的低层 internal API（不为 test 专设），public 暴露是合理的，命名不再绑定 test。

- `FastStaTestAccess` class 本体已经只存在于 `test/database/adapter/fast_sta/FastSTATest.cc` 等 test 文件内（grep 已确认），删除 source 端的 forward decl + friend 后，test 代码可以直接调用上述 3 个 public method，**不需要 friend 访问**。test 端可能需要少量适配（去掉对 friend 的依赖），但这属于 test/ 范围。

**禁用方案**（如果不可行再考虑）：
- 不要用 `#ifdef ICTS_BUILD_TESTS`（仍是 source 含 test 字眼）
- 不要用 PIMPL 拆 Internal header 把 test 代码暴露过去（增加抽象，得不偿失）

**验证**：
- M2.2 列出的 6 条 grep 在清理后全部 0 hits
- `bash build.sh` exit 0（test 端可能需要适配，agent 必须修通）

### M3 · 过程注释全面清理

#### M3.1 已确认必删（必须修）

| 文件 | 行 | 内容 | 处理 |
|---|---|---|---|
| `api/CTSAPI.cc` | 83-86 | `// FastSTA owns clock and characterization contexts; clear them on every API teardown. STAAdapter is a thin facade over the global iSTA engine whose lifetime is managed by init()/destroyPower/destroyTimingEngine, so it has no instance state to reset here.` | 直接删除 4 行 |

#### M3.2 全面排查（agent 必做）

agent 必须扫描以下模式，逐条人工判断后清理：

```bash
# 1. 多句解释性注释（提到 owns / manages / lifetime / because / so that / context）
grep -rnE '// .*(owns|managed by|lifetime|because|so that|on every|whose)' src/operation/iCTS/source src/operation/iCTS/api | grep -v Copyright | grep -v Mulan

# 2. "kept for / formerly / previously / used to / lifted from / moved from"（重构残留）
grep -rnE '// .*(kept for|formerly|previously|used to|lifted from|moved from|removed XXX|deprecated)' src/operation/iCTS/source src/operation/iCTS/api

# 3. banner 注释 ===== xxx =====
grep -rn '^[[:space:]]*// ===== ' src/operation/iCTS/source src/operation/iCTS/api

# 4. NOLINT / NOLINTNEXTLINE（前一 task 已清，确认无残留）
grep -rn 'NOLINT' src/operation/iCTS/source src/operation/iCTS/api

# 5. 提到任务 / commit hash / PR / 历史背景
grep -rnE '// .*(task|commit [a-f0-9]{7,}|PR #|see #|cherry|rebase|squash)' src/operation/iCTS/source src/operation/iCTS/api
```

#### M3.3 判断准则

**保留**（不算过程注释）：
- License header 块（`Mulan PSL` / `Copyright` 块）
- Doxygen `@file` / `@brief` / `@author` / `@date` / `@param` / `@return` 等
- 算法注释（如 `// Bottom-up merging: ...` / `// Manhattan distance computation`）
- 非显然 trade-off / 工程约束（如 `// safe under bottom_up=true precondition guaranteed by Driver`）
- 真 TODO（描述未来要做的事）

**删除**（过程注释）：
- 描述"为什么这么实现"而非"这是什么"的多句解释（特别是引用其他模块状态 / 历史决策）
- "kept for backward compat" / "formerly XXX" / "previously inlined" 等历史描述
- banner 装饰行
- NOLINT suppression（如果还有残留，必须修根因而非加 suppression）
- 提及 task / commit / PR / 子任务名的注释

---

## 4. 不做

- 不动 BST / CharBuilder / Flow / Schema 等任何模块的算法逻辑
- 不动 CTSAPI 5 个公开方法签名（`init` / `synthesis` / `optimization` / `report` / `resetAPI`）
- 不动 test/ 下的 fixture / utility（除非 M2 必须的适配）
- 不修改 `.trellis/ecc_dev_tools/` 任何文件
- 不引入新的 ResettableInterface / SingletonRegistry 抽象（已在前 task 撤回）

---

## 5. Acceptance Criteria

### 5.1 物理验收（grep + wc）

- [ ] `grep -n 'static auto clear' src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh` = 0
- [ ] `grep -n 'auto reset() -> void' src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh` ≥ 1
- [ ] `grep -n 'FastSTA::clear' src/operation/iCTS/` = 0
- [ ] `grep -n 'FAST_STA_INST.reset' src/operation/iCTS/api/CTSAPI.cc` ≥ 1
- [ ] `grep -rEn 'friend class .*(Test|Fixture|Access)' src/operation/iCTS/source src/operation/iCTS/api` = 0
- [ ] `grep -rnE 'namespace .*test.* \{|icts_test::' src/operation/iCTS/source src/operation/iCTS/api` = 0
- [ ] `grep -rnE 'forTest|ForTest|TestOnly|test_only|ForTesting' src/operation/iCTS/source src/operation/iCTS/api` = 0
- [ ] `grep -n 'FastSTA owns' src/operation/iCTS/api/CTSAPI.cc` = 0

### 5.2 行为验收（实测）

- [ ] `bash build.sh` exit 0 + iEDA binary 完整链接（含 test target 全部编译通过）
- [ ] `cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` 跑完 + CTS QoR 与前一 task baseline bit-identical（elapsed_time 允许 ±5% 抖动）

### 5.3 范围验收

- [ ] `git diff --stat` 仅涉及：
  - `source/database/adapter/fast_sta/FastSta.{hh,cc}`
  - `api/CTSAPI.cc`
  - `test/database/adapter/fast_sta/`（M2 配合适配）
  - 其他 M2.2 / M3.2 grep 扫描发现的 source 文件
- [ ] 不改动 BST / CharBuilder / Flow / Schema 等模块的 .cc/.hh
- [ ] 不新增任何 abstract interface / virtual base class

---

## 6. 实施约束

- **顺序**：M1（最小改动暖身） → M2（test 入侵清理 + 可能涉及 test 端适配，比 M1 复杂） → M3（注释清理，最后做以避免与 M1/M2 改动冲突）
- **每个 M 完成后**：`bash build.sh` PASS 才进下一个
- **全部完成后**：跑 `iEDA -script` 一次验证 QoR
- **不要在 agent 内跑 ecc dev**（用户明确要求统一最后一起跑）
- **不要 git commit**（最终 commit 由用户在 review 后决定）
- **不要 mask 任何编译/链接错误**（必须根因修复）
- **不要顺手做 PRD 之外的"小优化"**

---

## 7. 复杂度与 PRD-only 判断

本 task 是**轻量规范化**（PRD-only 可接受）：
- M1 是 5 行级别的 API 改名
- M2 已经定位到唯一已知入侵点 + 给出明确隔离方案；扫描可能发现 1~3 个同类，处理方式同模板
- M3 是机械的注释删除 + 全树扫描，已给出 grep 命令模板

**不需要 `design.md` / `implement.md`**——本 PRD 含足够设计与判断准则。

如果 M2 扫描发现意外的复杂入侵（如某个 source 模块大量依赖 test 内部状态），届时再补一份简短的 `m2-deviation.md` 记录处理方式。
