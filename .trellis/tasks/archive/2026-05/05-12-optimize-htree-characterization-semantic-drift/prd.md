# 优化h-tree & characterization的语义偏差

## Goal

Review the temporary development repository at `~/project/ecc-tools` against the current repository at `~/project/ecc-tools-dev`, identify the native h-tree and characterization changes that optimize semantic alignment between h-tree construction and characterization, document each change with motivation/correctness/integration feasibility, and define the native changes to integrate into the current codebase.

The completed review phase was read-only. Production source integration requires an explicit transition into implementation after this PRD scope is accepted.

## Background / Known Context

- The current working repository is `/home/liweiguo/project/ecc-tools-dev`.
- The comparison repository is `/home/liweiguo/project/ecc-tools`.
- The user wants to ignore `.trellis` changes and analytical h-tree related changes.
- The focus is native h-tree construction and characterization, especially char builder behavior and h-tree build semantics.
- The likely target mismatch is between characterized h-tree root-input-to-leaf-output delay and evaluation STA after physical root-driver/root-closure embedding.

## Requirements

### Review Requirements

- Compare relevant code differences between `ecc-tools` and `ecc-tools-dev`.
- Scope the review to native h-tree and characterization changes.
- Exclude `.trellis` artifacts and analytical h-tree changes from the analysis.
- Do not edit production source code during review/planning.
- Produce a concise report summarizing:
  - the optimization points;
  - what changed;
  - the likely motivation;
  - why the change is semantically/correctness-wise valid or what risks remain.
- Analyze whether each native change can be integrated into current `ecc-tools-dev` while preserving existing fanout legality and routing overlap fixes.

### Integration Requirements

- Integrate only the native h-tree / characterization semantics needed to reduce root-input-to-leaf-output delay mismatch; do not directly merge whole files from `~/project/ecc-tools`.
- Preserve current `ecc-tools-dev` fanout legality fixes:
  - topology `max_leaf_load_count`;
  - `source_exposed_load_count`;
  - h-tree branch/root fanout pruning;
  - fanout=4 legality behavior.
- Preserve current router overlapping-terminal legalization before FLUTE.
- Integrate CharBuilder semantic timing observation:
  - observe the last inserted buffer output for buffered segment patterns;
  - keep dummy sink input observation for unbuffered segment patterns.
- Integrate characterization RC resistance unit correction by converting `queryWireResistance()` milliohm values to ohms before building temporary STA RC trees.
- Integrate root-driver input slew resolution so `options.min_top_input_slew_ns` is used for root-driver compensation when provided; keep the current fallback when it is absent.
- Add a `root_input_slew` config knob with default `0.0` and use it as the topology-provided root-driver Liberty input slew and source-trunk input slew boundary instead of deriving that value from `max_buf_tran * 0.5`.
- Integrate root-driver output slew query and bucket reporting.
- Add root boundary diagnostics comparing:
  - raw h-tree char source cap bucket vs physical root closure load projected to one root source branch;
  - physical root closure total load bucket used by root-driver compensation;
  - raw h-tree char top input slew bucket vs root-driver output slew bucket.
- Integrate strict root-boundary closure only while preserving existing fanout pruning order; if no closed candidate survives, report a distinct failure reason.
- Do not copy the temporary repo behavior that unconditionally disables `top_input_slew_covering_idx`; keep the current boundary filter unless strict root-driver closure is explicitly replacing it.
- Do not include analytical h-tree or analytical characterization changes in this task.

## Acceptance Criteria

### Review Acceptance Criteria

- [x] A review report is written under `research/`.
- [x] The report identifies changed native h-tree and char builder areas.
- [x] Each material change point includes change detail, motivation, and correctness analysis.
- [x] Excluded areas are explicitly noted so the scope is auditable.
- [x] Integration feasibility is analyzed for the current `ecc-tools-dev` codebase.

### Integration Acceptance Criteria

- [x] Native code implements the agreed CharBuilder observation, RC unit, and root-driver boundary semantics.
- [x] Current fanout legality behavior is preserved for small-fanout cases.
- [x] The final flow passes:
  - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- [x] Reports expose enough root boundary diagnostics to judge whether root cap/slew buckets are closed or mismatched.
- [x] `root_input_slew` is configurable, defaults to `0.0`, and replaces the old topology-side half-`max_buf_tran` input slew derivation.
- [x] Analytical h-tree / analytical characterization code is not introduced by this task.

## Notes

- Review artifacts:
  - `research/native-htree-char-diff-review.md`
  - `research/integration-feasibility.md`
- If implementation begins, this task should add `design.md` and `implement.md` before starting Phase 2.
