# Mathematical Problem Statement: Initial Characterization and H-Tree Construction

## 1. Scope

This document states the problem only. It defines the inputs, variables, boundaries, constraints, feasible candidates, and the final solution rule. It does not prescribe how the problem should be solved.

The mathematical objects to be defined are:

```text
initial characterization domain and response functions
feasible fixed-depth H-tree candidate set
delay-power Pareto frontier
median-power candidate selection over the retained frontier
validation error against final evaluation
```

For every candidate H-tree depth `h`, the problem is to characterize all feasible H-tree construction candidates, compute their predicted delay and power, keep the delay-power Pareto frontier, and select the candidate whose power is the median on that frontier.

## 2. Given Data

Let:

```text
H                     finite set of candidate H-tree depths
h in H                one candidate depth
l = 0,...,h-1         H-tree level index, from root side to leaf side
r = 0,...,N_l(h)-1    atomic slot index within level l
a in A                atomic action index
```

The atomic action set is:

```text
A = {a_0} union B
```

where:

```text
a_0                   no-buffer or wire-only action
B                     finite set of buffer-cell actions
```

For each fixed depth `h`, the topology and geometry provide:

```text
N_l(h)                number of atomic slots in level l
M_l(h)                physical multiplicity represented by level l
rho_l(h)              branch fanout factor from level l to level l+1
L_l(h)                physical length represented by level l
rho_root(h)           number of first-level branches driven by the H-tree root driver
Delta_L               atomic length unit used to index length-dependent responses
ell_{l,r}(h)          physical length represented by slot r of level l
k_{l,r}(h)            length class of slot (l,r), e.g. ell_{l,r}(h) / Delta_L when integral
```

Electrical bounds are given as:

```text
S_max                 maximum allowed slew
C_max                 maximum allowed local downstream capacitance
C_root_max(g)         maximum load allowed for root-driver cell g
```

The leaf-load region is:

```text
Omega_leaf            finite set of leaf-load scenarios
C_leaf(omega)         H-tree leaf boundary load in scenario omega
```

When only one representative leaf load is used:

```text
|Omega_leaf| = 1
```

When a load region is used, `Omega_leaf` contains the leaf-load cases that the final selected H-tree pattern must cover.

## 3. Initial Characterization Problem

Initial characterization provides observations for each atomic action `a in A`.

The working assumption is that, for a fixed atomic action, the timing and power responses can be represented with high correlation by low-order functions of input slew and downstream load capacitance. In particular, output slew, delay, local power, and source-boundary power are expected to be well approximated by first-order or second-order functions over the sampled operating region.

This matters because the runtime cost of characterization expands as the number of characterization iterations increases. Let:

```text
K_iter                 number of characterization iterations or length bins
N_S                    number of sampled input-slew points
N_C                    number of sampled downstream-load points
N_A                    number of atomic actions or action patterns sampled
```

The characterization sample volume is proportional to:

```text
N_char = K_iter * N_S * N_C * N_A
```

Each sample requires timing and power observation work. Therefore, increasing `K_iter` directly expands the characterization runtime, and may also increase the number of generated action or pattern states that must be carried into later H-tree construction.

For this problem statement, characterization data can be represented by a response-function set and a structural capacitance-operator set:

```text
{F_{a,k}, D_{a,k}, P_{a,k}, W_{a,k} : a in A, k in K_a}
{C_{a,k} : a in A, k in K_a}
```

The response functions define the mathematical timing and power responses used to evaluate feasible H-tree candidates and construct their delay-power frontier. The capacitance operators define how downstream capacitance is transformed into source-side boundary capacitance.

For sampled input slew and downstream load pairs:

```text
(s_i, c_i), i in I_{a,k}
```

the observed response data are:

```text
y^S_{a,k,i}           observed output slew
y^D_{a,k,i}           observed delay
y^P_{a,k,i}           observed power
y^W_{a,k,i}           observed source-boundary power term
```

The characterization dataset for action `a` and length class `k` is:

```text
Q_{a,k} = {
  (s_i, c_i, y^S_{a,k,i}, y^D_{a,k,i}, y^P_{a,k,i}, y^W_{a,k,i})
  : i in I_{a,k}
}
```

The characterization problem is to determine response functions:

```text
F_{a,k}(s,c)          output slew produced by action a at length class k
D_{a,k}(s,c)          delay produced by action a at length class k
P_{a,k}(s,c)          local total power contribution of action a at length class k
W_{a,k}(s,c)          source-boundary power term owned by action a at length class k
```

The source-side capacitance problem is to determine a structural operator:

```text
C_{a,k}(c)            upstream/source capacitance produced by action a at length class k
```

where `c` is the downstream load capacitance of the same action. A general affine form is:

```text
C_{a,k}(c) = alpha_{a,k} * c + eta_{a,k}
alpha_{a,k} >= 0
eta_{a,k} >= 0
```

The constants `alpha_{a,k}` and `eta_{a,k}` are part of the mathematical candidate definition. They are not timing or power response values.

The transfer functions may be first-order or second-order in `(s,c)`:

```text
F_{a,k}(s,c) approximately f_{a,k}(1, s, c, s*c, s^2, c^2)
D_{a,k}(s,c) approximately d_{a,k}(1, s, c, s*c, s^2, c^2)
P_{a,k}(s,c) approximately p_{a,k}(1, s, c, s*c, s^2, c^2)
W_{a,k}(s,c) approximately w_{a,k}(1, s, c, s*c, s^2, c^2)
```

The exact retained terms are part of the characterization model definition, but the problem statement assumes the retained functions are sufficiently correlated with the measured observations to support candidate comparison.

The characterization problem also determines nonnegative uncertainty envelopes:

```text
epsilon^S_{a,k} >= 0
epsilon^D_{a,k} >= 0
epsilon^P_{a,k} >= 0
epsilon^W_{a,k} >= 0
```

such that the observed samples are covered:

```text
|F_{a,k}(s_i,c_i) - y^S_{a,k,i}| <= epsilon^S_{a,k}
|D_{a,k}(s_i,c_i) - y^D_{a,k,i}| <= epsilon^D_{a,k}
|P_{a,k}(s_i,c_i) - y^P_{a,k,i}| <= epsilon^P_{a,k}
|W_{a,k}(s_i,c_i) - y^W_{a,k,i}| <= epsilon^W_{a,k}
for all i in I_{a,k}
```

The functions must also satisfy basic physical validity over their intended domain:

```text
F_{a,k}(s,c) >= 0
D_{a,k}(s,c) >= 0
P_{a,k}(s,c) >= 0
W_{a,k}(s,c) >= 0
C_{a,k}(c)   >= 0
```

For hard feasibility checks, define conservative responses:

```text
F^+_{a,k}(s,c) = F_{a,k}(s,c) + epsilon^S_{a,k}
D^+_{a,k}(s,c) = D_{a,k}(s,c) + epsilon^D_{a,k}
P^+_{a,k}(s,c) = max(0, P_{a,k}(s,c) + epsilon^P_{a,k})
W^-_{a,k}(s,c) = max(0, W_{a,k}(s,c) - epsilon^W_{a,k})
W^+_{a,k}(s,c) = W_{a,k}(s,c) + epsilon^W_{a,k}
```

For capacitance propagation, define:

```text
C^+_{a,k}(c) = C_{a,k}(c)
```

when continuous physical capacitance is used internally. If a boundary must be compared through a bucket lattice, then the bucketed conservative value is:

```text
C^{bucket,+}_{a,k}(c) = Rep_C^+(Bucket_C(C_{a,k}(c)))
```

The bucket maps and representatives are defined in the root-boundary section.

Each action also has an admissible domain:

```text
s in [S_a^min, S_a^max]
c in [C_a^min, C_a^max]
```

When length is treated as a continuous variable rather than a discrete class, the same notation applies with `k` replaced by `ell`.

## 4. Fixed-Depth Construction Variables

For a fixed depth `h`, the primary decision variable is:

```text
z_{l,r,a} in {0,1}
```

where:

```text
z_{l,r,a} = 1          slot r of level l selects action a
z_{l,r,a} = 0          otherwise
```

Exactly one action is selected per slot:

```text
sum_{a in A} z_{l,r,a} = 1
for all l = 0,...,h-1, r = 0,...,N_l(h)-1
```

State variables at level boundaries are:

```text
s_{l,r}(omega)         slew at boundary r of level l
c_{l,r}(omega)         downstream capacitance at boundary r of level l
d_{l,r}(omega)         accumulated delay from boundary 0 to boundary r of level l
p_{l,r}(omega)         accumulated weighted power from boundary 0 to boundary r of level l
```

with:

```text
r = 0,...,N_l(h)
omega in Omega_leaf
```

The action selection `z_{l,r,a}` is shared across all leaf-load scenarios. The electrical states may vary with `omega`.

Root-driver variables are:

```text
g in G_root             selected or fixed root-driver cell
s_src                  slew at the root-driver input
C_root_total(omega)    total load driven by the root-driver output
s_root(omega)          slew at the H-tree root input boundary
D_root(omega)          root-driver delay
P_root(omega)          root-driver power
```

If the root driver is fixed, `g` is a given constant. If root-driver choice is part of the candidate definition, introduce:

```text
x_g in {0,1}
sum_{g in G_root} x_g = 1
```

## 5. Slot Transfer Constraints

For every selected action:

```text
z_{l,r,a} = 1 implies:

  s_{l,r}(omega) in [S_a^min, S_a^max]
  c_{l,r+1}(omega) in [C_a^min, C_a^max]

  s_{l,r+1}(omega) = F^+_{a,k_{l,r}(h)}(s_{l,r}(omega), c_{l,r+1}(omega))
  d_{l,r+1}(omega) = d_{l,r}(omega)
                     + D^+_{a,k_{l,r}(h)}(s_{l,r}(omega), c_{l,r+1}(omega))
  p_{l,r+1}(omega) = p_{l,r}(omega)
                     + M_l(h) * O^+_{l,r,a}(omega)
  c_{l,r}(omega)   = C^+_{a,k_{l,r}(h)}(c_{l,r+1}(omega))

for all omega in Omega_leaf
```

The conservative owned power term is:

```text
O^+_{l,r,a}(omega) =
  P^+_{a,k_{l,r}(h)}(s_{l,r}(omega), c_{l,r+1}(omega))
  - beta_{l,r}(h) * W^-_{a,k_{l,r}(h)}(s_{l,r}(omega), c_{l,r+1}(omega))
```

where:

```text
beta_{l,r}(h) in {0,1}
```

specifies whether the selected action's source-boundary switching term has already been represented by the preceding connected slot or boundary. This prevents a shared boundary power term from being counted twice. A mathematically valid formulation must fix the ownership convention before comparing candidate power values.

For every level:

```text
d_{l,0}(omega) = 0
p_{l,0}(omega) = 0
```

The raw H-tree delay and power under scenario `omega` are:

```text
D_raw(h,z,omega) =
  sum_{l=0}^{h-1} d_{l,N_l(h)}(omega)

P_raw(h,z,omega) =
  sum_{l=0}^{h-1} p_{l,N_l(h)}(omega)
```

If boundary ownership is defined by an equivalent set expression rather than the recurrence above, it must produce the same total owned power for every complete action sequence.

## 6. Level Coupling Constraints

For adjacent levels:

```text
s_{l+1,0}(omega) = s_{l,N_l(h)}(omega)
for l = 0,...,h-2
```

The end load of level `l` must match the fanout-weighted source load of level `l+1`:

```text
c_{l,N_l(h)}(omega) =
  rho_l(h) * c_{l+1,0}(omega) + c^J_l(h)
for l = 0,...,h-2
```

where:

```text
c^J_l(h) >= 0
```

is the level-junction or branch-closure capacitance used by the problem statement.

The leaf boundary is:

```text
c_{h-1,N_{h-1}(h)}(omega) = C_leaf(omega)
for all omega in Omega_leaf
```

## 7. Root Boundary Constraints

Let:

```text
c^R(h)                 root-closure capacitance
```

The physical load driven by the root driver is:

```text
C_root_total(omega) =
  rho_root(h) * c_{0,0}(omega) + c^R(h)
```

Root-driver transfer is:

```text
s_root(omega) = F_root,g(s_src, C_root_total(omega))
D_root(omega) = D_root,g(s_src, C_root_total(omega))
P_root(omega) = P_root,g(s_src, C_root_total(omega))
```

The H-tree top boundary is:

```text
s_{0,0}(omega) = s_root(omega)
```

The root-driver load must be legal:

```text
C_root_total(omega) <= C_root_max(g)
for all omega in Omega_leaf
```

If bucketed boundaries are used, root closure additionally requires that the raw H-tree source boundary and the physical root boundary refer to the same bucket:

```text
Bucket_S(s_{0,0}(omega)) = Bucket_S(s_root(omega))

Bucket_C(c_{0,0}(omega)) =
Bucket_C((C_root_total(omega) - c^R(h)) / rho_root(h))
```

The bucket functions must be fixed maps:

```text
Bucket_S : R_{\ge 0} -> I_S
Bucket_C : R_{\ge 0} -> I_C
```

If a boundary value is converted from a continuous value to a bucket and then back to a conservative representative value, the representative must be specified:

```text
Rep_S^+(Bucket_S(s)) >= s
Rep_C^+(Bucket_C(c)) >= c
```

for all boundary values where an upper envelope is required for feasibility.

## 8. Electrical and Structural Constraints

For all valid `l`, `r`, and `omega`:

```text
0 <= s_{l,r}(omega) <= S_max
0 <= c_{l,r}(omega) <= C_max
```

The selected H-tree must cover the full requested leaf-load scenario set:

```text
all constraints in Sections 5, 6, 7, and 8
hold for every omega in Omega_leaf
with the same action variables z_{l,r,a}
```

The action choices must also admit at least one realization:

```text
R_h(z) != empty
```

where `R_h(z)` is the set of realization maps associated with the action sequence. A realization map:

```text
pi in R_h(z)
```

assigns the selected actions to physical positions, ordered action lists, branch or terminal semantics, and any boundary attachment semantics required by the mathematical H-tree definition. Feasibility requires every retained candidate to have at least one realization map satisfying the specified geometric, ordering, root-boundary, and leaf-boundary semantics.

Define buffer indicators:

```text
b_{l,r} = sum_{a in B} z_{l,r,a}
```

Weighted buffer count:

```text
B_cnt(h,z) =
  sum_{l=0}^{h-1} M_l(h)
  sum_{r=0}^{N_l(h)-1}
  b_{l,r}
```

Let `area(a)` be the physical area of action `a`, with:

```text
area(a_0) = 0
area(a) >= 0 for all a in A
```

Weighted buffer area:

```text
A_buf(h,z) =
  sum_{l=0}^{h-1} M_l(h)
  sum_{r=0}^{N_l(h)-1}
  sum_{a in A} area(a) z_{l,r,a}
```

Optional physical limits may be stated as:

```text
B_cnt(h,z) <= B_max
A_buf(h,z) <= A_max
```

If terminal-branch buffering is part of the candidate semantics, let:

```text
term(a,l,r) in {0,1}
```

and define:

```text
T_term(h,z) =
  sum_{l=0}^{h-1}
  I( sum_{r=0}^{N_l(h)-1} sum_{a in A} term(a,l,r) z_{l,r,a} >= 1 )
```

Optional terminal-branch constraints may be stated as:

```text
T_min <= T_term(h,z) <= T_max
```

If a level requires monotone action order, with a given rank function `rank(a)`, the constraint is:

```text
sum_{a in A} rank(a) z_{l,r,a}
  <=
sum_{a in A} rank(a) z_{l,r+1,a}
```

or the opposite direction, depending on the specified root-to-leaf convention.

Any additional structural rule that affects whether a candidate can be realized must be expressible as:

```text
Phi_h(z, pi) = true
```

for at least one `pi in R_h(z)`. Such rules may restrict action positions, relative order, terminal ownership, root attachment, leaf attachment, branch multiplicity, or other realization-level semantics. They are part of the feasible set, not post-processing preferences.

## 9. Feasible Candidate Set

For each depth `h`, define the feasible candidate set:

```text
X_h = {
  x = (z, states, g, pi)
  :
  x satisfies all action, transfer, coupling, root, leaf, electrical,
  structural, and realization constraints above
}
```

The compensated delay and power of candidate `x in X_h` under scenario `omega` are:

```text
D_total(h,x,omega) =
  D_root(omega) + D_raw(h,z,omega)

P_total(h,x,omega) =
  P_root(omega) + P_raw(h,z,omega)
```

The scalar delay and power used for candidate comparison are scenario aggregations:

```text
D(h,x) = Agg_D({D_total(h,x,omega) : omega in Omega_leaf})
P(h,x) = Agg_P({P_total(h,x,omega) : omega in Omega_leaf})
```

where `Agg_D` and `Agg_P` are specified aggregation operators over the covered leaf-load scenarios. The problem statement only requires these operators to be fixed before comparing candidates.

## 10. Delay-Power Pareto Frontier

For fixed depth `h`, a feasible candidate `x in X_h` is delay-power Pareto-optimal if there is no other feasible candidate `x' in X_h` such that:

```text
D(h,x') <= D(h,x)
P(h,x') <= P(h,x)
```

and at least one inequality is strict:

```text
D(h,x') < D(h,x)
or
P(h,x') < P(h,x)
```

The fixed-depth delay-power Pareto frontier is:

```text
F_h = {
  x in X_h
  :
  x is delay-power Pareto-optimal in X_h
}
```

The required fixed-depth result is not a single candidate obtained by collapsing delay and power into one numerical score. The required fixed-depth result is the full frontier `F_h`, or an equivalent representation from which the same median-power frontier candidate can be recovered.

## 11. Fixed-Depth Median-Power Rule

For each depth `h`, sort the frontier candidates by nondecreasing power:

```text
F_h = {x_{h,1}, x_{h,2}, ..., x_{h,n_h}}

P(h,x_{h,1}) <= P(h,x_{h,2}) <= ... <= P(h,x_{h,n_h})
```

If two candidates have the same power, sort them by nondecreasing delay:

```text
D(h,x_{h,i}) <= D(h,x_{h,j})
when P(h,x_{h,i}) = P(h,x_{h,j}) and i < j
```

Define the median-power index:

```text
m_h = ceil(n_h / 2)
```

The selected candidate for depth `h` is:

```text
x_h^med = x_{h,m_h}
```

Thus, the fixed-depth optimum is defined as the Pareto-frontier candidate at median power:

```text
Opt(h) = x_h^med
```

This means:

```text
The frontier expresses the delay-power tradeoff.
The final fixed-depth candidate is the frontier point whose power rank is the median.
```

## 12. Multi-Depth Final Selection

For final selection across multiple depths, first form the union of all fixed-depth Pareto frontier candidates:

```text
Y = {
  (h, x)
  :
  h in H, x in F_h
}
```

Compute:

```text
D_Y(h,x) = D(h,x)
P_Y(h,x) = P(h,x)
```

The cross-depth Pareto frontier is:

```text
F_Y = {
  (h,x) in Y
  :
  there is no (h',x') in Y with
  D_Y(h',x') <= D_Y(h,x),
  P_Y(h',x') <= P_Y(h,x),
  and at least one strict inequality
}
```

Sort `F_Y` by nondecreasing power and nondecreasing delay under equal power. If:

```text
F_Y = {(h_1,x_1), ..., (h_n,x_n)}
```

with:

```text
P_Y(h_1,x_1) <= ... <= P_Y(h_n,x_n)
```

then:

```text
m_Y = ceil(n / 2)
```

and the final H-tree construction result is:

```text
(h^*, x^*) = (h_{m_Y}, x_{m_Y})
```

Equivalently stated:

```text
Across all fixed-depth frontier candidates, keep the delay-power Pareto frontier,
then choose the Pareto candidate whose power is the median on that frontier.
```

If a separate policy compresses each fixed-depth frontier to `Opt(h)` before cross-depth comparison, that policy is a different mathematical selection rule and must be stated explicitly, because it can discard candidates that would survive the frontier union above.

## 13. Validation Error

After materializing `(h^*, x^*)`, final evaluation provides:

```text
D_eval              evaluated root-input to leaf-output delay
P_eval              evaluated total power under the same ownership convention
V_eval              Boolean indicator that the realization is physically valid
```

The predicted compensated delay is:

```text
D_pred = D(h^*, x^*)
P_pred = P(h^*, x^*)
```

The absolute and relative prediction errors are:

```text
E^D_abs = |D_pred - D_eval|
E^D_rel = |D_pred - D_eval| / max(D_eval, delta_D)
E^P_abs = |P_pred - P_eval|
E^P_rel = |P_pred - P_eval| / max(P_eval, delta_P)
```

where:

```text
delta_D > 0
delta_P > 0
```

are small positive constants used only to avoid division by zero.

The realization validity condition is:

```text
V_eval = true
```

The validation error is not part of the Pareto-frontier definition. It measures whether the characterization functions, boundary constraints, power ownership convention, and physical load assumptions were faithful enough for the constructed H-tree.
