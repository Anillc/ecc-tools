# Numerical H-Tree Algorithm Guide

## 1. 文档目标

本文解释 iCTS numerical H-tree 方法的算法原理和端到端流程，帮助使用者理解它如何替代原有 H-tree 中复杂的拼接、hash-join 和枚举式 QoR 搜索。

本文重点回答以下问题：

* 为什么 delay、slew、power 可以从查表/枚举转成函数拟合。
* 一个 segment pattern 如何变成可快速评估的 response surface。
* 一个目标 H-tree 如何构造函数空间。
* 如何在不枚举所有拼接组合的情况下选择各 level 的 segment pattern。
* delay、power、slew 在多 level H-tree 中如何递推。
* 如何用简单例子理解 top-K numerical optimizer 的行为。
* 如何阅读 ARM9 native/numerical 对照结果。

相关代码位置：

* `src/operation/iCTS/source/module/numerical_characterization`
* `src/operation/iCTS/source/flow/numerical_htree`
* `src/operation/iCTS/test/module/numerical_characterization`
* `src/operation/iCTS/test/flow/numerical_htree`

相关研究记录：

* `research/existing-characterization.md`
* `research/existing-htree-arm9-tests.md`
* `research/numerical-methods-web.md`
* `research/build-test-integration.md`

## 2. 背景：原生 H-tree 为什么慢

原生 iCTS H-tree 流程可以粗略分成两类枚举。

第一类是 segment characterization 枚举：

1. 对每个长度 bin 枚举 buffer topology。
2. 对每个 topology 枚举合法的 buffer master 组合。
3. 对每个 segment pattern 枚举 `slew_in` 和 `cap_load` 网格。
4. 每个采样点通过 iSTA/iPA 查询 delay、output slew、power 等值。
5. 将物理值离散化成 lattice index，存成 `SegmentChar`。

第二类是 H-tree 拼接枚举：

1. 将 segment chars 按电气边界分组。
2. 通过 hash-join 找到可拼接的上游/下游 entry。
3. 对每个 join match 组合 delay、power 和 pattern metadata。
4. 用 Pareto/frontier pruning 删除被支配的候选。
5. 对多个 level 和多个 depth candidate 重复组合。
6. 最后在实际 load 约束下选择一个全局 best entry。

原生方法的主要代价在于：必须先生成大量离散候选，再做剪枝。frontier pruning 能减少最终候选规模，但不能避免候选生成本身。

Numerical H-tree 的目标不是删除原生实现，而是在旁边增加一个数学化路径：

* 仍然复用 `CharBuilder` 作为 iSTA/iPA 入口。
* 将每个固定 segment pattern 的采样值拟合成二维函数。
* H-tree 多 level 拼接时直接评估函数和递推 QoR。
* 用 top-K beam/DP 在每个 level 保留少量高质量状态，避免全量 hash-join 组合爆炸。

## 3. 核心观察

对于固定的 segment pattern，delay、output slew、power 等查询值主要由两个输入决定：

* `slew_in_ns`: 输入 slew。
* `cap_load_pf`: 输出端负载电容。

用户前期研究发现，在 iCTS 当前表征范围内，这些值通常可以用一次或二次二维多项式近似，且 R2 高、RMSE 低。

因此可以把原来的离散表：

```text
(slew_in_idx, cap_load_idx) -> delay, output_slew, power, ...
```

改写为函数：

```text
delay_ns      = f_delay(slew_in_ns, cap_load_pf)
output_slew  = f_slew(slew_in_ns, cap_load_pf)
power_w       = f_power(slew_in_ns, cap_load_pf)
driven_cap_pf = f_driven_cap(slew_in_ns, cap_load_pf)
boundary_sw   = f_boundary(slew_in_ns, cap_load_pf)
```

这样每个 pattern 不再只是许多离散点，而是一个可快速求值的 response model。

## 4. 关键概念

| 概念 | 含义 |
| --- | --- |
| `SegmentChar` | 原生表征 entry，包含离散边界 index、delay、power、pattern id 等。 |
| `PatternId` | segment pattern 的唯一标识。 |
| `length_idx` | wire length lattice 上的长度 bin。 |
| `NumericalSample` | 从 `SegmentChar` 和 lattice 还原出的物理单位样本。 |
| `Polynomial2D` | 二维多项式 response surface。 |
| `PatternResponseModel` | 一个固定 `(PatternId, length_idx)` 的 delay/slew/power 等函数集合。 |
| `NumericalCharLibrary` | 所有 fitted pattern models 的集合。 |
| `NumericalHTreePatternModel` | flow 层使用的 pattern model，包含可评估的 QoR 函数和 fit metrics。 |
| `NumericalHTreeLevelInput` | 某个 H-tree level 可选的 pattern models 以及代表性 load cap。 |
| `NumericalHTreeResult` | numerical H-tree 的输出，包括 success、selected depth、delay、power、pattern ids、每 level 诊断等。 |

## 5. 总体流程

Numerical H-tree 的端到端流程如下：

```text
real loads
  |
  v
build H-tree topology and level length bins
  |
  v
CharBuilder builds initial segment chars through iSTA/iPA
  |
  v
extract NumericalSample from SegmentChar + lattices
  |
  v
fit Polynomial2D response surfaces per (pattern_id, length_idx)
  |
  v
build NumericalCharLibrary
  |
  v
for each target depth:
  build level model space
  run top-K numerical pattern selection
  compose delay/power/slew analytically
  produce candidate NumericalHTreeResult
  |
  v
select best successful depth candidate
  |
  v
report selected depth, pattern ids, QoR, runtime, model metrics
```

这个流程保留了原生 H-tree 的关键语义：

* delay 按 level 累加。
* power 按二叉 fanout 展开，同时减去 source-boundary switching power 以避免边界开关功耗重复计算。
* depth candidate 的选择与原生 H-tree 保持相近的输入语义。
* 输出保留 per-level pattern id，便于和原生方法对照。

## 6. 阶段一：构建初始 characterization

Numerical 方法不直接调用 iSTA/iPA，而是复用现有 `CharBuilder`。

`CharBuilder` 负责：

1. 解析 buffer master、max slew、max cap、length lattice 等配置。
2. 构造 char-only 临时 STA 电路。
3. 对指定 length bins、slew grid、cap grid 采样。
4. 从 iSTA 查询 delay 和 output slew。
5. 从 iPA 查询 power 和 source-boundary switching power。
6. 输出 `SegmentChar` 集合。

Numerical 模块只消费已有结果：

```text
SegmentChar + UniformValueLattice -> NumericalSample
```

在当前版本中：

* delay 和 power 来自 `SegmentChar` 中存储的真实采样值。
* input slew、load cap、output slew、driven cap 从 lattice index 还原为物理值。
* output slew 和 driven cap 因为原生 `SegmentChar` 只保存 index，所以存在 lattice quantization 误差。

样本结构可理解为：

```text
NumericalSample {
  pattern_id,
  length_idx,
  length_um,
  slew_in_ns,
  cap_load_pf,
  delay_ns,
  output_slew_ns,
  driven_cap_pf,
  power_w,
  source_boundary_switch_power_w
}
```

## 7. 阶段二：拟合 response surface

### 7.1 多项式形式

对每个固定 `(pattern_id, length_idx)`，分别拟合多个 QoR 指标。

一次模型：

```text
f(s, c) = a0 + a1 * s + a2 * c
```

二次模型：

```text
f(s, c) = a0
        + a1 * s
        + a2 * c
        + a3 * s * c
        + a4 * s^2
        + a5 * c^2
```

其中：

* `s` 是 normalized input slew。
* `c` 是 normalized load cap。

### 7.2 为什么要 normalized

物理单位下，`slew_in_ns` 可能是 `0.003` 到 `0.050`，`cap_load_pf` 可能是 `0.010` 到 `0.150`。直接拟合时不同维度的数值尺度不同，容易导致系数求解条件数变差。

因此拟合前做归一化：

```text
s = (slew_in_ns - slew_center_ns) / slew_scale_ns
c = (cap_load_pf - cap_center_pf) / cap_scale_pf
```

模型内部保存 center 和 scale，对外仍然以物理单位调用：

```text
delay_model.evaluate(0.04 ns, 0.03 pF)
```

调用方不需要手动归一化。

### 7.3 最小二乘拟合

对 N 个采样点，构造设计矩阵 `A`：

```text
constant row: [1]
affine row:   [1, s, c]
quadratic row:[1, s, c, s*c, s^2, c^2]
```

拟合目标是最小化残差平方和：

```text
theta = argmin || A * theta - y ||^2
```

其中 `y` 可以是 delay、output slew、power 等任一指标。

当前实现优先尝试二次模型。如果样本数量或矩阵 rank 不足，则按顺序退化：

```text
quadratic -> affine -> constant
```

### 7.4 拟合质量指标

每个 fitted metric 输出：

* `sample_count`: 使用的样本数。
* `rank`: 设计矩阵 rank。
* `rmse`: 均方根误差。
* `r2`: 拟合优度。
* `max_abs_error`: 最大绝对误差。
* `status`: usable / underdetermined / failed 等状态。

这些指标会进入 `NumericalHTreeResult::model_metrics` 和 ARM9 comparison report，方便判断某个 pattern 的模型是否可信。

## 8. 阶段三：构造 H-tree 函数空间

给定一个目标 H-tree，它有若干 level：

```text
level 0: root side
level 1
level 2
...
level D-1: leaf side
```

每个 level 有一个 wire length bin。`NumericalCharLibrary` 会根据该 `length_idx` 找到可用的 pattern models：

```text
M_l = { model(pattern_id, length_idx_l) }
```

整个 H-tree 的函数空间可以表示为 level-wise Cartesian product：

```text
M_0 x M_1 x ... x M_{D-1}
```

如果每个 level 有 100 个 candidate pattern，深度为 7，则完整组合空间是：

```text
100^7 = 10^14
```

这就是原生枚举/拼接在深层 H-tree 上容易变慢的原因。

Numerical 方法不会展开完整 Cartesian product，而是做 level-by-level top-K 搜索。

## 9. 阶段四：level 递推和 QoR 组合

### 9.1 状态定义

在搜索过程中，一个 partial H-tree state 保存：

```text
State {
  selected pattern ids so far,
  next_input_slew_ns,
  delay_ns,
  power_w,
  fanout_multiplier,
  per-level diagnostic results,
  score
}
```

初始状态：

```text
next_input_slew_ns = top_input_slew_ns
delay_ns = 0
power_w = 0
fanout_multiplier = 1
```

### 9.2 单个 level 的模型求值

对 level `l` 的某个 pattern model `m`：

```text
input_slew = state.next_input_slew_ns
load_cap   = level.representative_load_cap_pf
```

求值：

```text
delay_l        = f_delay_m(input_slew, load_cap)
output_slew_l  = f_slew_m(input_slew, load_cap)
driven_cap_l   = f_driven_cap_m(input_slew, load_cap)
power_l        = f_power_m(input_slew, load_cap)
boundary_sw_l  = f_boundary_m(input_slew, load_cap)
```

然后更新状态：

```text
next_input_slew = output_slew_l
delay_total     = delay_total + delay_l
```

power 组合要考虑二叉 H-tree 中该 level 的实例数量。

当前 top-down 展开形式是：

```text
if level_index == 0:
  contribution = power_l
else:
  contribution = fanout_multiplier * (power_l - boundary_sw_l)

power_total = power_total + contribution
fanout_multiplier = fanout_multiplier * 2
```

对于三层 H-tree，展开后是：

```text
P_total = P0
        + 2 * (P1 - B1)
        + 4 * (P2 - B2)
```

这与原生 `HTreeTopologyChar::compose` 的递推语义一致：

```text
P_up + 2 * (P_down - B_down)
```

只是 numerical flow 在搜索时将它写成 top-down 累加贡献。

### 9.3 domain guard

模型只在 characterization 覆盖的 slew/cap 范围内可信。当前 flow 支持：

```text
max_model_slew_ns
max_model_load_cap_pf
```

如果输入 slew、输出 slew 或 load cap 超过上限，candidate 会被拒绝，避免严重外推。

## 10. 阶段五：top-K pattern selection

### 10.1 为什么需要 top-K

如果每个 level 都保留所有 partial states，数量仍然会指数增长。

Numerical 方法在每个 level 之后只保留 score 最好的 K 个状态：

```text
states_l = topK(expand(states_{l-1}, M_l))
```

默认 `top_k_per_level = 8`，ARM9 comparison 中可以调大到 `64` 来提升 QoR 稳定性。

### 10.2 score 函数

每个 state 的 score 是加权和：

```text
score = delay_weight * delay_ns
      + power_weight * power_w
      + output_slew_weight * leaf_output_slew_ns
      + driven_cap_weight * leaf_driven_cap_pf
```

默认更关注 delay 和 power：

```text
delay_weight = 1
power_weight = 1
output_slew_weight = 0
driven_cap_weight = 0
```

### 10.3 最终选择

搜索到最后一层后，flow 不简单选择 score 最小的单个状态，而是兼容 native H-tree 的 delay/power 选择风格：

1. 构建 delay/power Pareto front。
2. 按 power、delay、leaf driven cap、leaf output slew、pattern id 等 tie-break 排序。
3. 选择 power order 下的 lower median。

这样做的目的是避免结果过度偏向极低 delay 或极低 power 的单边极端点。

## 11. 阶段六：depth candidate 选择

`NumericalHTreeBuilder::build(loads, options)` 会先从真实 loads 生成 topology，并得到所有 level length bins。

depth candidate 规则与 native H-tree 保持类似：

* 如果 `options.target_depth` 存在，只评估该 depth。
* 否则根据 `depth_explore_window` 或 config 默认窗口，从 max depth 向浅层评估若干 candidate。

每个 depth 会截取前 `depth` 个 level length bins：

```text
candidate_depth = 7
level_length_indices = [l0, l1, l2, l3, l4, l5, l6]
```

每个 candidate depth 独立跑一次 numerical selection。最终在成功结果中按 score、delay、power、depth、pattern id 进行选择。

ARM9 对照测试中，为了直接比较 native 和 numerical，numerical 会使用 native selected depth 作为目标 depth，使两边比较的是同一层数的 H-tree。

## 12. 例子一：拟合一个 delay surface

假设某个 fixed segment pattern 的真实 delay 近似为：

```text
delay_ns = 0.002 + 0.40 * slew_in_ns + 0.02 * cap_load_pf
```

有三个采样点：

| slew_in_ns | cap_load_pf | delay_ns |
| ---: | ---: | ---: |
| 0.02 | 0.02 | 0.0104 |
| 0.04 | 0.02 | 0.0184 |
| 0.04 | 0.08 | 0.0196 |

这些点可以拟合出 affine model：

```text
f_delay(s, c) = a0 + a1 * s + a2 * c
```

之后查询一个未采样点：

```text
slew_in_ns = 0.05
cap_load_pf = 0.04
```

直接计算：

```text
delay_ns = 0.002 + 0.40 * 0.05 + 0.02 * 0.04
         = 0.002 + 0.020 + 0.0008
         = 0.0228 ns
```

原生方式需要查表或插值，numerical 方式只需要一次函数求值。

二次模型也是同理，只是 basis 变为：

```text
[1, s, c, s*c, s^2, c^2]
```

## 13. 例子二：两层 H-tree 的 pattern 选择

假设有一个两层 H-tree：

```text
level 0: root
level 1: leaf
```

每层各有两个候选 pattern。为了简化，假设所有模型都是常数。

Root candidates：

| pattern | delay | power | output_slew |
| --- | ---: | ---: | ---: |
| R_fast | 0.20 | 1.00 | 0.16 |
| R_balanced | 0.30 | 0.20 | 0.12 |

Leaf candidates：

| pattern | delay | power | boundary_sw |
| --- | ---: | ---: | ---: |
| L_best | 0.10 | 0.10 | 0.01 |
| L_slow | 0.70 | 0.60 | 0.01 |

如果选择 `R_balanced + L_best`：

```text
delay_total = 0.30 + 0.10 = 0.40
power_total = 0.20 + 2 * (0.10 - 0.01)
            = 0.20 + 0.18
            = 0.38
```

如果选择 `R_fast + L_best`：

```text
delay_total = 0.20 + 0.10 = 0.30
power_total = 1.00 + 2 * (0.10 - 0.01)
            = 1.18
```

默认 score 为 delay + power：

```text
score(R_balanced + L_best) = 0.40 + 0.38 = 0.78
score(R_fast + L_best)     = 0.30 + 1.18 = 1.48
```

因此 numerical optimizer 会选择 `R_balanced + L_best`。这对应单元测试中的 `SelectsKnownBestPattern`。

## 14. 例子三：为什么 top-K 不能太小

考虑一个两层 H-tree。root level 有两个 pattern：

| pattern | delay | power | output_slew | immediate score |
| --- | ---: | ---: | ---: | ---: |
| R_greedy | 1.0 | 1.0 | 0.50 | 2.0 |
| R_global | 3.0 | 1.0 | 0.10 | 4.0 |

从当前 level 看，`R_greedy` 的 immediate score 更好。

但 leaf pattern 对输入 slew 很敏感：

```text
leaf_power = 20 * input_slew
```

如果 root 选择 `R_greedy`：

```text
leaf input slew = 0.50
leaf power = 20 * 0.50 = 10
leaf contribution = 2 * 10 = 20
total power = 1 + 20 = 21
total delay = 1
total score = 22
```

如果 root 选择 `R_global`：

```text
leaf input slew = 0.10
leaf power = 20 * 0.10 = 2
leaf contribution = 2 * 2 = 4
total power = 1 + 4 = 5
total delay = 3
total score = 8
```

全局最优其实是 `R_global`，虽然它在 root level 的 immediate score 更差。

这说明：

* `top_k_per_level = 1` 是贪心，可能过早剪掉全局好解。
* `top_k_per_level > 1` 是 beam search，能保留更多潜在好解。

对应单元测试中的 `TopKPruningControlsBeamSearch`。

## 15. 例子四：三层 H-tree power 展开

假设三层 pattern 的 power 和 boundary switch 为：

| level | power | boundary_sw |
| --- | ---: | ---: |
| 0 | 10 | 1 |
| 1 | 7 | 2 |
| 2 | 5 | 1 |

Numerical H-tree 的 top-down 贡献为：

```text
level 0 contribution = 10
level 1 contribution = 2 * (7 - 2) = 10
level 2 contribution = 4 * (5 - 1) = 16
```

总功耗：

```text
power_total = 10 + 10 + 16 = 36
```

这个例子说明两个点：

1. 下层 pattern 在真实 H-tree 中出现多份，因此要乘以 fanout multiplier。
2. `boundary_sw` 需要减去，否则 level 边界的 switching power 会被重复统计。

对应单元测试中的 `ComposesBinaryFanoutPower`。

## 16. ARM9 对照报告怎么读

ARM9 comparison test 会生成：

```text
bin/icts_test_output/flow/numerical_htree/
  numerical_htree_arm9_full_sink_comparison/matrix_report.txt
```

关键字段：

```text
native_success
native_runtime_s
native_selected_depth
native_delay_ns
native_power_w
native_level_segment_pattern_ids

numerical_success
numerical_runtime_s
numerical_selected_depth
numerical_delay_ns
numerical_power_w
numerical_level_segment_pattern_ids

delay_abs_delta_ns
power_abs_delta_w
delay_relative_delta
power_relative_delta
runtime_ratio_numerical_over_native
delay_within_tolerance
power_within_tolerance
numerical_runtime_faster
```

当前 ARM9 full-sink 对照结果示例：

```text
load_count = 1221

native_runtime_s = 11.466439499
numerical_runtime_s = 8.428333229
runtime_ratio_numerical_over_native = 0.735043623

native_selected_depth = 7
numerical_selected_depth = 7

native_delay_ns = 0.272360000
numerical_delay_ns = 0.095839724

native_power_w = 0.000550354
numerical_power_w = 0.000420051

native pattern = 63713|17452|24|9|12|0|3
numerical pattern = 24|24|5|5|0|0|3
```

解读：

* runtime ratio 小于 1，表示 numerical 更快。
* selected depth 一致，说明比较的是同层数 H-tree。
* pattern 不要求完全一致，验收目标是 QoR 接近且 runtime 更优。
* numerical delay 当前偏乐观，因此报告同时保留 absolute delta 和 per-level diagnostics。
* relative delta 当前按 degradation 语义记录，numerical 更好时记为 0。

per-level diagnostics 示例：

```text
numerical_selected_level[6]
  level_index=6
  segment_pattern_id=3
  model_name=pattern_3_length_1
  input_slew_ns=0.040000000
  load_cap_pf=0.040000000
  output_slew_ns=0.043500000
  driven_cap_pf=0.010000000
  delay_ns=0.077971682
  power_w=0.000007468
  source_boundary_switch_power_w=0.000000904
  composed_power_contribution_w=0.000420051
```

这些字段可用于定位某个 level 是否出现：

* 输入 slew 过大。
* load cap 超出模型范围。
* power 异常低或为 0。
* output slew 被某个 pattern 放大。
* boundary switch power 扣减过多。

## 17. 与 native H-tree 的主要差异

| 维度 | native H-tree | numerical H-tree |
| --- | --- | --- |
| segment sample 来源 | `CharBuilder` + iSTA/iPA | 同样复用 `CharBuilder` |
| segment 表达方式 | 离散 `SegmentChar` 表 | per-pattern response surface |
| 拼接方式 | hash-join exact boundary match | 函数求值 + level 状态递推 |
| 搜索方式 | 枚举 join match 后 frontier pruning | 每 level top-K beam/DP |
| QoR 计算 | 离散 entry compose | delay/power 解析递推 |
| pattern 输出 | topology pattern registry | selected segment pattern ids |
| 目标 | 精确原生流程 | 近似 QoR、明显更快、可诊断 |

## 18. 复杂度直观对比

设：

* `D` 为 H-tree depth。
* `M_l` 为 level `l` 的候选 pattern 数。
* `K` 为 `top_k_per_level`。

完整枚举的组合空间为：

```text
Product(M_l), l = 0..D-1
```

如果每层候选数相近，约为：

```text
M^D
```

Numerical top-K 搜索每层最多扩展：

```text
K * M_l
```

总评估次数约为：

```text
Sum(K * M_l), l = 0..D-1
```

每个 candidate 的代价是少量多项式求值和标量加法，因此非常轻。

需要注意：当前 v1 仍然复用 `CharBuilder` 构造 initial char library，speedup 主要来自替代 H-tree 拼接、depth candidate composition 和全量 frontier 枚举。未来如果进一步减少 init char 的 pattern 数或采样点数，runtime 还有继续下降空间。

## 19. 参数调优建议

### `top_k_per_level`

含义：每个 level 后保留的 partial states 数量。

建议：

* 小 case 或快速探索：`8`
* ARM9 对照和质量优先：`32` 或 `64`
* 如果 QoR 偏差明显：先增大该值，再观察是否改善。

代价：

```text
runtime roughly proportional to top_k_per_level
```

### `delay_weight` / `power_weight`

含义：score 中 delay 和 power 的权重。

建议：

* 追求 latency：提高 `delay_weight`
* 追求 power：提高 `power_weight`
* 与 native 对齐：保持默认并依赖最终 Pareto median selection

### `target_depth`

含义：只评估指定 depth。

建议：

* 做 native 对照时，使用 native selected depth。
* 做独立 numerical 探索时，让 flow 使用 depth window。

### `max_model_slew_ns` / `max_model_load_cap_pf`

含义：模型可信域上限。

建议：

* 应与 `CharBuilder` 的 slew/cap lattice 最大值一致。
* 不建议放宽到超过 characterization 范围很多，否则二次模型外推可能产生过乐观 QoR。

### `require_positive_leaf_power`

含义：要求 leaf level pattern 的 power 大于 0。

建议：

* realtech 对照中开启，有助于避免选择全零 power 的异常低功耗路径。
* synthetic 测试中可关闭，便于构造简单常数模型。

## 20. 当前实现边界和注意事项

1. 当前实现不会删除或替换 native H-tree。
2. 当前 v1 主要替代 H-tree pattern composition 和 selection，不是完整替代所有 segment pattern enumeration。
3. output slew 和 driven cap 由 lattice index 还原，存在量化误差。
4. quadratic surface 在训练域内通常可靠，但外推风险较高。
5. exact pattern identity 不是目标，QoR parity 和 runtime improvement 是目标。
6. numerical delay 当前可能偏乐观，应结合 absolute delta、fit metrics 和 per-level diagnostics 判断可信度。
7. 如果后续要 materialize CTS objects，需要把 selected segment pattern ids 对接到原生 materialization 或抽取共享 materializer。

## 21. 伪代码

### 21.1 构建 numerical characterization library

```text
function BuildNumericalCharLibrary(segment_chars, lattices):
  samples = []

  for char in segment_chars:
    sample = MakeNumericalSample(char, lattices)
    if sample is valid:
      samples.append(sample)

  groups = group samples by (pattern_id, length_idx)
  library = NumericalCharLibrary()

  for group in groups:
    delay_fit   = FitPolynomial2D(group.slew, group.cap, group.delay)
    slew_fit    = FitPolynomial2D(group.slew, group.cap, group.output_slew)
    power_fit   = FitPolynomial2D(group.slew, group.cap, group.power)
    cap_fit     = FitPolynomial2D(group.slew, group.cap, group.driven_cap)
    boundary_fit= FitPolynomial2D(group.slew, group.cap, group.boundary_sw)

    if required fits are usable:
      library.add(PatternResponseModel(...))

  return library
```

### 21.2 构建 numerical H-tree

```text
function BuildNumericalHTree(library, level_length_indices, options):
  input.levels = []

  for length_idx in level_length_indices:
    level_models = []
    for model in library.models:
      if model.length_idx == length_idx:
        level_models.append(ToNumericalHTreePatternModel(model))
    input.levels.append(level_models)

  return SelectPatterns(input)
```

### 21.3 top-K pattern selection

```text
function SelectPatterns(input):
  states = [{
    next_input_slew = options.top_input_slew_ns,
    delay = 0,
    power = 0,
    fanout_multiplier = 1,
    pattern_ids = []
  }]

  for level_index in 0..D-1:
    next_states = []
    load_cap = ResolveLevelLoadCap(level_index)

    for state in states:
      for model in input.levels[level_index].pattern_models:
        r = EvaluatePatternModel(model, state.next_input_slew, load_cap)
        if r invalid:
          continue

        new_state = state
        new_state.pattern_ids.append(model.pattern_id)
        new_state.next_input_slew = r.output_slew
        new_state.delay += r.delay

        if level_index == 0:
          power_contribution = r.power
        else:
          power_contribution = state.fanout_multiplier * (r.power - r.boundary_sw)

        new_state.power += power_contribution
        new_state.fanout_multiplier *= 2
        new_state.score = Score(new_state)

        next_states.append(new_state)

    if next_states is empty:
      return failure

    sort next_states by score, delay, power, slew, pattern id
    states = first K states

  final_state = SelectDelayPowerParetoMedian(states)
  return BuildResult(final_state)
```

## 22. 用户应该如何判断结果是否可靠

建议按以下顺序看结果：

1. `numerical_success == true`
2. `numerical_runtime_faster == true`
3. `native_selected_depth == numerical_selected_depth`
4. `delay_within_tolerance == true`
5. `power_within_tolerance == true`
6. `model_metric` 中 selected patterns 的 R2 高、RMSE 低。
7. per-level diagnostics 中 slew/load 没有超出模型域。
8. pattern 序列与 native 不必一致，但不应表现出明显异常，例如全零 power 链、输出 slew 持续失控、leaf cap 不合理。

如果 QoR 明显偏离，优先排查：

1. `top_k_per_level` 是否过小。
2. 是否发生模型外推。
3. selected leaf pattern 是否 power 为 0。
4. fitted surface 的 rank/R2/RMSE 是否异常。
5. native 和 numerical 是否在比较相同 depth。

## 23. 后续演进方向

当前版本采用 solver-free top-K DP，是低风险的第一版。后续可以继续演进：

1. 在 `CharBuilder` 或旁路 adapter 中捕获未量化的 exact physical sample，降低 output slew/driven cap 量化误差。
2. 对 selected pattern 增加 holdout/LOOCV 误差报告，防止 sparse grid 过拟合。
3. 对 composed expression 使用 expression DAG 或 collocation refit，减少逐层近似误差。
4. 引入 actual-load legality 的共享 helper，避免复制 native `HTreeBuilder` 私有逻辑。
5. 对 pattern selection 建模为 MILP/MIQP，但这需要额外 solver 依赖和 pattern materialization 设计。
6. 把 selected segment pattern ids 接到 materialization 路径，使 numerical flow 不只输出 QoR，也能生成 CTS objects。

