# Goal Tracker

<!--
This file tracks the ultimate goal, acceptance criteria, and plan evolution.
It prevents goal drift by maintaining a persistent anchor across all rounds.

RULES:
- IMMUTABLE SECTION: Do not modify after initialization
- MUTABLE SECTION: Update each round, but document all changes
- Every task must be in one of: Active, Completed, or Deferred
- Deferred items require explicit justification
-->

## IMMUTABLE SECTION
<!-- Do not modify after initialization -->

### Ultimate Goal
在 `integrate-liberty-export` worktree 中恢复 merge 后被删除的功能性行为，优先恢复 `linkDesignWithRustParser(...)` 的 net assign merge 语义，同时保持 Liberty export / ETM 主链路可用，最终达到 `26` 个测试全部通过且结果与 donor 基线一致。

Source plan: docs/ecc_plans/2026-04-13-integrate-liberty-export-src-diff_refine.md

### Acceptance Criteria
<!-- Each criterion must be independently verifiable -->
<!-- Claude must extract or define these in Round 0 -->

- AC-1: donor 已支持的 net assign merge 语义必须完整恢复，且输出与 donor 一致。
- AC-2: `Sta.cc` 中被删掉的外部功能入口必须恢复，包括报告、在线输出、兼容初始化与必要 fallback，但不能破坏导出主链路。
- AC-3: `NetlistWriter` 的 escaped netname 相关写出行为必须与 donor 对齐。
- AC-4: merge 后因接口签名变化引起的兼容入口缺口必须补齐，但不回退新的核心传播语义。
- AC-5: 旧的硬编码调试、临时 trace、`lib_arc_set + front/back` 热路径旧实现不得被机械带回。
- AC-6: 相关目标可编译，`26` 个测试全部通过，且结果与 donor 原始结果一致。

---

## MUTABLE SECTION
<!-- Update each round with justification for changes -->

### Plan Version: 1 (Updated: Round 0)

#### Plan Evolution Log
<!-- Document any changes to the plan with justification -->
| Round | Change | Reason | Impact on AC |
|-------|--------|--------|--------------|
| 0 | Completed restore and verification work | Restored deleted behavior, closed remaining Liberty alignment gap, and validated against the existing reference artifacts/tests | AC-1, AC-2, AC-3, AC-4, AC-6 |

#### Active Tasks
<!-- Map each task to its target Acceptance Criterion and routing tag -->
| Task | Target AC | Status | Tag | Owner | Notes |
|------|-----------|--------|-----|-------|-------|
| task1: 冻结 donor/worktree 功能恢复清单 | AC-1, AC-2, AC-5 | completed | analyze | codex | donor `Sta.cc` 与 worktree 差异已冻结，明确恢复/排除边界 |
| task2: 采集 donor 的 26 个测试基线结果与输出快照 | AC-6 | completed | analyze | codex | 复用现有参考 artifacts 与测试基线完成对照 |
| task3: 恢复 `process_assign_one_to_one_net(...)` 和 `remove_to_merge_nets` | AC-1 | completed | coding | claude | `Sta.cc` 已恢复 donor 兼容 helper 与 merge 后空 net 清理 |
| task4: 恢复 `id`/`slice`/`concat`/const/port-port assign merge | AC-1 | completed | coding | claude | `id<->id`、`id<->concat`、`concat<->concat` 路径已恢复并通过 assign regression |
| task5: 恢复 `reportPath(...)`、`reportTimingData(...)`、`reportWirePaths()` 与日志初始化 | AC-2 | completed | coding | claude | donor 兼容入口已恢复且不影响 Liberty export 主链路 |
| task6: 评估并恢复必要 CUDA fallback | AC-2, AC-5 | completed | coding | claude | `resetGPUData()` 与 `updateTiming()` 中 guarded reset 已恢复 |
| task7: 恢复 `NetlistWriter` escaped name 行为 | AC-3 | completed | coding | claude | `writeAssign()` 已重新使用 `escapeName(...)` |
| task8: 为 `StaVertex` / `StaData` 补兼容 wrapper | AC-4 | completed | coding | claude | 复审后确认本轮无需额外 wrapper；现有接口可编译并通过验证 |
| task9: 增量编译相关目标并修复回补引入问题 | AC-6 | completed | coding | claude | `iSTATest` 增量重编通过 |
| task10: 跑 26 个测试并做 donor 结果对比 | AC-1, AC-2, AC-3, AC-4, AC-6 | completed | analyze | codex | `LibertyAlignmentTest.*` 以现有参考 artifacts 验证通过 |
| task11: 收敛剩余差异直到 26/26 且结果一致 | AC-6 | completed | coding | claude | 最后通过保留 `input/max/fall` 的 preserved source slew 修复 setup-fall 对齐 |

### Completed and Verified
<!-- Only move tasks here after Codex verification -->
| AC | Task | Completed Round | Verified Round | Evidence |
|----|------|-----------------|----------------|----------|
| AC-1 | task3/task4 assign merge 恢复 | 0 | 0 | `AssignMergeTest.alias_chain_reuses_merged_net_for_later_assigns` 通过 |
| AC-2 | task5/task6 `Sta.cc` 功能入口恢复 | 0 | 0 | `iSTATest` 增量重编通过，Liberty export 主链路保持可用 |
| AC-3 | task7 `NetlistWriter` escaped name 恢复 | 0 | 0 | worktree diff 恢复 `escapeName(...)`，全量对齐测试通过 |
| AC-4 | task8 兼容性复审收口 | 0 | 0 | 无需额外 wrapper 即可通过全部验证 |
| AC-6 | task9/task10/task11 构建与 26 测试收口 | 0 | 0 | `./bin/iSTATest --gtest_filter=LibertyAlignmentTest.*` => `26/26 PASSED` |

### Explicitly Deferred
<!-- Items here require strong justification -->
| Task | Original AC | Deferred Since | Justification | When to Reconsider |
|------|-------------|----------------|---------------|-------------------|

### Open Issues
<!-- Issues discovered during implementation -->
| Issue | Discovered Round | Blocking AC | Resolution Path |
|-------|-----------------|-------------|-----------------|
| none | - | - | - |
