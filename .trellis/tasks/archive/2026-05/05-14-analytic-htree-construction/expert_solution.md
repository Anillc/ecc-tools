根据 2026-05-14 的实验文档和你贴出的 `driven_cap` native 语义排查，现在技术路线可以明确了：

[
\boxed{
\text{iter-1 unit char}
+
\text{结构化 capacitance 递推}
+
\text{函数级 slew/delay/power compose}
+
\text{branch-aware 多目标 DP}
+
\text{interval-safe Pareto pruning}
}
]

最重要的变化是：**`driven_cap` 不再作为 (U(s,c)) 或 (U(c)) response surface 拟合，而是作为结构电容算子精确递推。** 这样上一轮实验里 function-level compose 出现的 driven cap 系统性高估，不应再被解释成“拟合模型不够复杂”，而应被解释成“数学模型里把 `driven_cap` 当错了对象”。实验文档里 function-level compose 已经明显优于 discrete frontier compose，但 driven cap 在 length-3 达到 1.4528 的 ratio，这正好和你们现在的排查结论吻合：它不应该走 response-surface 路径。

---

# 1. 修正后的核心建模对象

原来我会写：

[
{F_{a,k},D_{a,k},P_{a,k},W_{a,k},U_{a,k}}
]

现在应改成：

[
\boxed{
{F_{a,k},D_{a,k},P_{a,k},W_{a,k}}
+
{\mathcal C_{a,k}}
}
]

其中：

[
F_{a,k}(s,c)
]

拟合 output slew；

[
D_{a,k}(s,c)
]

拟合 delay；

[
P_{a,k}(s,c)
]

拟合 local power；

[
W_{a,k}(s,c)
]

拟合 source-boundary switching power；

但：

[
\mathcal C_{a,k}
]

不是拟合函数，而是 **结构电容算子**。

这与原问题文档中 slot transfer 的形式完全兼容，只是把原来的 (U_{a,k}(c)) 从“学习得到的 response”替换成“由 topology / buffer pin cap / wire cap 确定的结构算子”。原始问题文档中 slot transfer 本来就把 (c_{l,r}) 作为从下游往上游递推的 boundary state，并且 power 通过 (P-W) 的 ownership convention 去重。

---

# 2. driven cap 应该建模为仿射算子，而不是 response surface

对每个 unit slot 或 atomic pattern (a)，定义 source boundary capacitance operator：

[
\mathcal C_a(c)=\alpha_a c+\eta_a
]

其中：

[
\alpha_a \in {0,1}
]

### wire-only unit

如果该 unit 没有 buffer：

[
\mathcal C_a(c)=c+C_{\text{wire}}(a)
]

也就是：

[
\alpha_a=1
]

[
\eta_a=C_{\text{wire}}(a)
]

这对应 native 里的：

[
driven_cap=load_pf+\sum C_{\text{wire}}
]

### buffered unit

如果该 unit 有 buffer，并且从 source 到第一个 buffer 的 wire cap 是 (C_{\text{prewire}})，第一个 buffer input pin cap 是 (C_{\text{in}}(b_1))，则：

[
\mathcal C_a(c)=C_{\text{in}}(b_1)+C_{\text{prewire}}
]

也就是：

[
\alpha_a=0
]

[
\eta_a=C_{\text{in}}(b_1)+C_{\text{prewire}}
]

这对应 native 里的：

[
driven_cap=C_{\text{in}}(first_buffer)+C_{\text{wire}}(source\rightarrow first_buffer)
]

它与 input slew 无关，也与 downstream load 无关。

---

# 3. 长 pattern 的 source cap 是算子复合

给定 unit 序列：

[
a_0,a_1,\ldots,a_{m-1}
]

从 leaf 侧最终 load (c_m) 往 source 侧递推：

[
c_i=\mathcal C_{a_i}(c_{i+1})
]

因此：

[
c_0=
\mathcal C_{a_0}\circ
\mathcal C_{a_1}\circ
\cdots\circ
\mathcal C_{a_{m-1}}(c_m)
]

仿射算子复合满足：

[
(\alpha_1,\eta_1)\circ(\alpha_2,\eta_2)
=======================================

(\alpha_1\alpha_2,\alpha_1\eta_2+\eta_1)
]

这个公式很关键。

如果前面全是 wire-only：

[
\alpha=1
]

source cap 会把 downstream load 一直带回来：

[
c_0=c_m+\sum_i C_{\text{wire},i}
]

如果序列中遇到第一个 buffer，则：

[
\alpha=0
]

downstream load 被第一个 buffer input pin 隔断，source cap 变成：

[
c_0=C_{\text{in}}(first_buffer)+C_{\text{wire before first buffer}}
]

这正是你们 native 语义排查的数学表达。

所以新的结论是：

[
\boxed{
driven_cap \text{ 是一个结构仿射递推，不是一个 } (s,c)\text{ 响应面。}
}
]

---

# 4. H-tree branch point 也可以进入同一个电容算子体系

branch coupling 原来是：

[
c_{l,N_l(h)}(\omega)
====================

\rho_l(h)c_{l+1,0}(\omega)+c_l^J(h)
]

这正好也是一个仿射算子：

[
\mathcal B_l(c)=\rho_l(h)c+c_l^J(h)
]

如果是 binary branch：

[
\rho_l(h)=2
]

那么：

[
\mathcal B_l(c)=2c+c_l^J
]

root closure 也是：

[
C_{\text{root,total}}(\omega)
=============================

\rho_{\text{root}}(h)c_{0,0}(\omega)+c^R(h)
]

即：

[
\mathcal R_h(c)=\rho_{\text{root}}(h)c+c^R(h)
]

原问题文档中 level coupling 和 root closure 本来就是这种形式。

因此，整个 H-tree 的 capacitance propagation 可以统一写成：

[
\boxed{
\text{wire / buffer / branch / root closure 都是仿射电容算子复合}
}
]

其中 buffer 算子是“吸收型”：

[
\alpha=0
]

wire 算子是“传递型”：

[
\alpha=1
]

branch 算子是“fanout 型”：

[
\alpha=\rho_l
]

---

# 5. 修正后的 unit characterization

现在 unit char 只需要拟合四类响应：

[
\boxed{
F_{a}(s,c),\quad D_{a}(s,c),\quad P_{a}(s,c),\quad W_{a}(s,c)
}
]

不再拟合：

[
U_a(c)
]

或者：

[
driven_cap_a(s,c)
]

其中 (c) 的含义是 **当前 unit downstream load**，由结构电容算子从下游递推得到。

上一轮实验已经说明，iter-1 上 delay 和 power 的低阶拟合质量很好，source-boundary switching power 也几乎 exact；output slew 相对弱，约 10% relative RMSE，需要 envelope 或 calibration。

所以推荐默认模型是：

[
F_a(s,c)=\theta^F_{a,0}+\theta^F_{a,1}s+\theta^F_{a,2}c
]

[
D_a(s,c)=\theta^D_{a,0}+\theta^D_{a,1}s+\theta^D_{a,2}c
]

[
P_a(s,c)=\theta^P_{a,0}+\theta^P_{a,1}s+\theta^P_{a,2}c
]

[
W_a(s,c)=\theta^W_{a,0}+\theta^W_{a,1}s+\theta^W_{a,2}c
]

先用 constrained affine model；quadratic 可以作为局部 fallback，但不建议默认启用。因为上一轮实验里 linear 和 quadratic 的 function-level compose 结果几乎打平，quadratic 没有明显改善。

---

# 6. 修正后的 DP label

对任意 boundary (b=(l,r))，一个 suffix label 定义为：

[
L=
\left(
q_L,\
\mathcal C_L,\
\delta_L^\omega(s),
\pi_L^\omega(s),
I_L^\omega,
trace_L
\right)_{\omega\in\Omega}
]

其中：

[
q_L
]

是结构状态，例如 monotone order、terminal rule、buffer count、area、realization automaton state；

[
\mathcal C_L
]

是从当前 boundary 看向下游 leaf 的结构电容算子；

[
C_L^\omega
==========

\mathcal C_L(C_{\text{leaf}}^\omega)
]

是当前 boundary 的 downstream cap；

[
\delta_L^\omega(s)
]

是输入 slew 为 (s) 时 suffix 贡献的 delay；

[
\pi_L^\omega(s)
]

是输入 slew 为 (s) 时 suffix 贡献的 owned total power；

[
I_L^\omega
]

是该 suffix 可接受的 input slew domain。

这比原来的 label 更清楚：**cap 是结构算子，slew/delay/power 是连续响应函数。**

---

# 7. regular slot transition

假设当前 suffix label 是 (L)，在它前面 prepend 一个 action (a)，得到 (L')。

先计算 downstream cap：

[
c_\omega=C_L^\omega
]

新的 source-side cap operator 是：

[
\mathcal C_{L'}
===============

\mathcal C_a\circ \mathcal C_L
]

因此：

[
C_{L'}^\omega
=============

\mathcal C_a(C_L^\omega)
]

slot 输出 slew：

[
s_\omega^+(s)
=============

F_a^{safe}(s,c_\omega)
]

新的 delay function：

[
\delta_{L'}^\omega(s)
=====================

D_a^{rank}(s,c_\omega)
+
\delta_L^\omega(s_\omega^+(s))
]

新的 power function：

[
\pi_{L'}^\omega(s)
==================

M_l(h)
\left[
P_a^{rank}(s,c_\omega)
----------------------

\beta_{l,r}(h)W_a^{rank}(s,c_\omega)
\right]
+
\pi_L^\omega(s_\omega^+(s))
]

feasible domain：

[
I_{L'}^\omega
=============

\left{
s:
s\in[S_a^{min},S_a^{max}],
c_\omega\in[C_a^{min},C_a^{max}],
s_\omega^+(s)\in I_L^\omega
\right}
]

这里 (M_l(h)) 处理 physical multiplicity，(\beta_{l,r}(h)) 处理 source-boundary switching power 去重。原问题文档中 power 就是用 (M_l(h)\cdot O^+_{l,r,a}) 进入 slot recurrence，并用 (\beta) 防止 shared boundary power double count。

---

# 8. branch transition

从 child level (l+1) 回到 parent level (l) 时，不需要特殊拟合，只需要插入 branch capacitance operator：

[
\mathcal C_{L'}
===============

\mathcal B_l\circ \mathcal C_L
]

其中：

[
\mathcal B_l(c)=\rho_l(h)c+c_l^J(h)
]

delay 不变：

[
\delta_{L'}^\omega(s)=\delta_L^\omega(s)
]

power 不变：

[
\pi_{L'}^\omega(s)=\pi_L^\omega(s)
]

为什么 power 不乘 (\rho_l)？

因为采用的是 global physical-power convention：level (l+1) 的 slots 在各自 transition 里已经用 (M_{l+1}(h)) 加权，而通常：

[
M_{l+1}(h)=\rho_l(h)M_l(h)
]

所以 branch coupling 处只改 cap，不再额外乘 downstream power，否则会 double count。

如果 branch junction 本身有 power，则把它作为 pseudo-slot：

[
a=a_{\text{branch}}
]

并加：

[
M_l(h)P_{\text{junction}}(s,c)
]

即可。

---

# 9. root closure

对 root boundary label (L)，先得到 raw H-tree source boundary cap：

[
c_{0,0}^\omega=C_L^\omega
]

root driver 真实看到：

[
C_{\text{root,total}}^\omega
============================

\rho_{\text{root}}(h)c_{0,0}^\omega+c^R(h)
]

然后：

[
s_{\text{root}}^\omega
======================

F_{\text{root},g}(s_{\text{src}},C_{\text{root,total}}^\omega)
]

[
D_{\text{root}}^\omega
======================

D_{\text{root},g}(s_{\text{src}},C_{\text{root,total}}^\omega)
]

[
P_{\text{root}}^\omega
======================

P_{\text{root},g}(s_{\text{src}},C_{\text{root,total}}^\omega)
]

feasible if：

[
C_{\text{root,total}}^\omega\le C_{\text{root,max}}(g)
]

[
s_{\text{root}}^\omega\in I_L^\omega
]

最终：

[
D^\omega
========

D_{\text{root}}^\omega+
\delta_L^\omega(s_{\text{root}}^\omega)
]

[
P^\omega
========

P_{\text{root}}^\omega+
\pi_L^\omega(s_{\text{root}}^\omega)
]

bucket 只在和 native validation/reporting/root compensation 对齐时使用：

[
idx_C=\operatorname{Bucket}_C(c)
]

而不是在 DP 内部频繁 round-trip。原问题文档里也已经区分了连续 root load 和 bucket closure check，并要求 bucket representative 明确化。

---

# 10. 多目标 DP recurrence

对固定 depth (h)，定义每个 boundary 的 label set：

[
\mathcal L_{l,r}
]

regular slot：

[
\mathcal L_{l,r}
================

\operatorname{Compress}
\left(
\operatorname{ND}
\left[
\bigcup_{a\in A_{l,r}}
{T_{l,r,a}(L):L\in\mathcal L_{l,r+1}}
\right]
\right)
]

level branch：

[
\mathcal L_{l,N_l}
==================

B_l(\mathcal L_{l+1,0})
]

leaf base：

[
\mathcal L_{h-1,N_{h-1}}
========================

{L_{\text{leaf}}}
]

其中：

[
\mathcal C_{L_{\text{leaf}}}(c)=c
]

[
\delta_{L_{\text{leaf}}}(s)=0
]

[
\pi_{L_{\text{leaf}}}(s)=0
]

[
I_{L_{\text{leaf}}}=[0,S_{\max}]
]

最终得到 root boundary labels 后做 root closure，再得到每个 candidate 的：

[
D(h,x)=Agg_D({D^\omega})
]

[
P(h,x)=Agg_P({P^\omega})
]

然后保留 delay-power Pareto frontier。原问题定义要求固定 depth 保留 Pareto frontier，而不是把 delay/power collapse 成单个 scalar；跨 depth 也应先 union frontier，再选 cross-depth Pareto frontier 上 power median 的点。 

---

# 11. dominance pruning 的安全条件

在同一个 boundary 上，label (L_1) 可以支配 (L_2) 的条件应是：

[
C_{L_1}^\omega\le C_{L_2}^\omega,\quad \forall\omega
]

[
I_{L_1}^\omega\supseteq I_{L_2}^\omega,\quad \forall\omega
]

[
\delta_{L_1}^{+,\omega}(s)
\le
\delta_{L_2}^{-,\omega}(s),
\quad \forall s\in I_{L_2}^\omega
]

[
\pi_{L_1}^{+,\omega}(s)
\le
\pi_{L_2}^{-,\omega}(s),
\quad \forall s\in I_{L_2}^\omega
]

其中 (+) 和 (-) 表示带 uncertainty envelope 的上下界。

这里 cap 不需要 envelope 来自拟合误差，因为它是结构算子；但仍然可能需要 bucket rounding envelope：

[
C^{bucket,+}
============

Rep_C^+(\operatorname{Bucket}_C(C))
]

用于和 native bucket 行为保持一致。

---

# 12. output slew 是现在剩下的主要建模风险

在修正 driven cap 后，最大风险会变成 output slew：

[
F_a(s,c)
]

上一轮实验显示，iter-1 output slew 的 relative RMSE 约 10%，function-level compose 后 length-3 output slew ratio 约 0.869，即存在系统性低估。 

所以 (F) 必须用 safe envelope：

[
F_a^{safe}(s,c)
===============

F_a^{rank}(s,c)+\epsilon_a^S+\epsilon_{\text{rollout}}^S(n,\phi)
]

其中：

[
n=\text{composed unit depth}
]

[
\phi=(\text{action class},\text{buffer count},\text{wire-only prefix length},s,c)
]

delay/power 可以用 ranking model：

[
D^{rank},\quad P^{rank}
]

但 feasibility 必须用 conservative model：

[
F^{safe},\quad D^{safe},\quad P^{safe}
]

尤其是 slew feasibility：

[
s_{out}^{safe}\le S_{\max}
]

不能用 raw (F^{rank})。

---

# 13. 新的技术路线可以明确成 7 步

## Step 1：只做 iter-1 unit characterization

char 内容：

[
F_a,D_a,P_a,W_a
]

不 char long pattern，不拟合 driven cap。

数据量目标从：

[
K_{iter}N_SN_CN_A
]

降成：

[
1\cdot N_SN_CN_A
]

原问题文档中也明确指出 characterization sample volume 与 (K_{iter}) 线性相关，因此减少 iteration 是主要 runtime 收益来源。

---

## Step 2：建立结构电容算子库

对每个 atomic action (a)，预计算：

[
\mathcal C_a(c)=\alpha_a c+\eta_a
]

wire-only：

[
(\alpha_a,\eta_a)=(1,C_{\text{wire}})
]

buffered：

[
(\alpha_a,\eta_a)=(0,C_{\text{in}}(first_buffer)+C_{\text{prewire}})
]

branch：

[
(\alpha,\eta)=(\rho_l,c_l^J)
]

root：

[
(\alpha,\eta)=(\rho_{\text{root}},c^R)
]

---

## Step 3：拟合低阶 transfer functions

默认 affine：

[
F,D,P,W \sim 1+s+c
]

必要时局部启用：

[
1+s+c+sc+s^2+c^2
]

但不建议全局 quadratic，因为上一轮实验未显示明显收益。

---

## Step 4：函数级 compose，而不是 discrete frontier compose

DP 内部使用连续物理量：

[
s,\ c,\ D,\ P
]

而不是每一步都依赖 bucket index。

bucket 只用于：

1. native-compatible validation；
2. root compensation closure；
3. final reporting；
4. 必须与 legacy table 交互的地方。

上一轮实验显示，function-level compose 相比 discrete frontier compose 明显改善 delay/power agreement，并几乎消除了 key coverage 问题。

---

## Step 5：branch-aware continuous functional Pareto DP

DP index 用：

[
(l,r)
]

transition 用：

[
T_{l,r,a}
]

而不是统一的 stationary (T_a)。

每个 transition 显式带：

[
M_l(h),\quad \rho_l(h),\quad c_l^J(h),\quad \beta_{l,r}(h),\quad k_{l,r}(h)
]

这样 branch、multiplicity、power ownership 都在数学模型里，而不是工程后处理里。

---

## Step 6：interval-safe dominance + ε-Pareto compression

保留：

[
D/P \text{ Pareto frontier}
]

但用 interval dominance 避免模型误差导致错误剪枝。

再用 log-space ε-compression 控制 label 数量：

[
\tilde D=\log(D/D_0),\quad
\tilde P=\log(P/P_0),\quad
\tilde C=\log(C/C_0)
]

bucket width (\eta) 对应近似相对误差：

[
e^\eta-1
]

这样可以保持 runtime 对大规模 case 友好。

---

## Step 7：frontier-window re-eval

最终不要 re-eval 全空间，只 re-eval：

1. predicted Pareto frontier；
2. median-power 附近窗口；
3. interval overlap 导致可能 dominance flip 的 candidates；
4. 每个 depth 的 anchor points。

这一步不是主算法，而是生产安全阀。

---

# 14. 需要重跑的关键实验

你们现在应该重跑一次 function-level compose，但把 driven cap 处理改成结构电容算子。

预期结果：

### driven cap

应该从：

[
ratio_{length3}\approx1.4528
]

大幅回到接近：

[
1.0
]

只剩 bucket rounding / native convention 差异。

### delay / power

之前 function-level compose 的 delay/power 已经比 discrete compose 好很多：length-3 delay ratio 约 1.0613，power ratio 约 1.0537。

修正 cap 后，delay/power 有机会进一步收敛，因为之前 inflated cap 会让 delay/power 偏 conservative。

### output slew

output slew 未必自动变好，因为这是 (F(s,c)) 自身拟合和 rollout 的问题。因此它仍然要用 envelope 或 calibration。

---

# 15. 最终推荐方案

可以把最终路线命名为：

[
\boxed{
\text{Structural-Cap Functional Pareto DP}
}
]

完整数学表达是：

[
\boxed{
\begin{aligned}
&\text{capacitance: }
C \leftarrow
\mathcal C\text{-operator exact composition}
\
&\text{slew: }
s^+=F^{safe}_a(s,C)
\
&\text{delay: }
D\leftarrow D+D^{rank/safe}_a(s,C)
\
&\text{power: }
P\leftarrow P+
M_l(h)\left(P^{rank/safe}_a(s,C)-\beta W^{rank/safe}*a(s,C)\right)
\
&\text{optimization: }
\operatorname{ND}*{D,P}\text{ over functional labels}
\end{aligned}
}
]

一句话总结：

[
\boxed{
driven_cap \text{ 用结构递推，}
F/D/P/W \text{ 用 iter-1 函数拟合，}
H\text{-tree 构造用 branch-aware 多目标 DP。}
}
]

这条路线现在已经比之前更明确：**cap 维度不再是拟合风险源，而是精确结构状态；剩下真正需要拟合和校准的是 slew/delay/power，尤其是 output slew 的 rollout envelope。**
