# CTS optimization fast STA migration design

## Boundary

This task owns optimization policy only. Fast STA owns timing, slew, cap, power, area, and incremental recomputation. Optimization may request a candidate buffer master change and observe the resulting fast STA state, but it must not duplicate fast STA timing calculations.

Affected existing areas:

```text
src/operation/iCTS/source/flow/optimization/
src/operation/iCTS/source/module/buffer_sizing/
```

Expected new dependency:

```text
module/buffer_sizing -> database/adapter/fast_sta
flow/optimization -> database/adapter/fast_sta
```

## Optimization Semantics

The optimization problem is fixed-topology buffer sizing.

Inputs:

- clock context from `FastStaAdapter`,
- legal buffer master candidates,
- target skew from CTS config,
- max cap legality from fast STA,
- area per candidate from fast STA/Liberty snapshot.

Outputs:

- accepted buffer master changes,
- final fast STA skew,
- final area delta,
- cap legality status,
- runtime and change distribution.

Objective:

1. If target skew can be reached legally, choose a legal sizing assignment with minimum area among explored candidates meeting target skew.
2. If target skew cannot be reached, keep the legal assignment that minimizes skew spread.

## Algorithm Direction

The implementation should move from char-backed local scoring to fast STA-backed state transitions:

1. Build or fetch the fast STA clock context after CTS topology is committed.
2. Query initial sink arrivals and skew.
3. Generate candidate sizing moves from legal buffer masters while preserving topology.
4. Apply a candidate through fast STA incremental update.
5. Query updated skew, cap status, and area.
6. Accept a move only if it improves the objective under the target semantics and does not introduce cap violations.
7. Revert or replace rejected moves through fast STA state restoration.
8. Continue until no improving legal move is found or target semantics are satisfied.

The exact search strategy can evolve, but it must use fast STA as the single timing source. Candidate batching is allowed when it improves quality, provided every accepted batch is validated by a fast STA update.

## Reporting

Normal logs should stay compact:

- runtime,
- target skew,
- initial skew,
- final skew,
- area delta,
- cap legality summary,
- changed master distribution, such as `CLKBUF_X2 -> CLKBUF_X4: 17`.

Detailed per-path traces should only be temporary debug instrumentation and should be removed before final handoff unless explicitly requested.

## Validation Matrix

Run the binary command three times with target skew set to:

- 80ps,
- 40ps,
- 0ps.

For each run, capture:

- command/config change used,
- initial/final fast STA skew,
- final reported CTS/STA skew if available,
- changed buffer distribution,
- area delta,
- cap violations before/after,
- optimization runtime,
- whether target was reached.

## Rollback

Keep the previous char-backed optimization code reachable until fast STA-backed results are validated. Once fast STA-backed optimization is accepted, remove dead char-backed move evaluation paths rather than keeping fallback behavior.
