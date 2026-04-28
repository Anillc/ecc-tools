# Superseded Research Notes

Date: 2026-04-27

Earlier research artifacts explored moving cluster-to-leaf details into
`cluster_leaf_distance.csv`, adding top-worst cluster sections, and keeping
scoped-stage elapsed time in per-stage summary tables. Earlier notes also
proposed narrative/detail `Notes` blocks in the main report. The accepted
correction is schema-writer-centered instead. Those ideas are superseded by the
accepted implementation contract:

- Normal CTS runs do not generate `cluster_leaf_distance.csv`; tests and
  scripts use clean output directories instead of hidden runtime stale-file
  cleanup.
- The main `cts.log` keeps cluster distance reporting compact with
  count/min/max/mean/median only; no top-worst table, percentile fields, row
  count, or artifact pointer is emitted.
- Scoped stage summary tables keep `outcome` and caller-provided status fields
  only. Major-stage elapsed time and peak virtual-memory delta are consolidated
  in `CTS Runtime Summary`.
- Runtime metric state lives in `SchemaWriter` / `SCHEMA_WRITER_INST`, not in
  `CTSAPI` and not in a CTS-specific stateful reporter. Stage scopes in CTSAPI
  and touched CTS flow/logging paths are acquired through
  `SCHEMA_WRITER_INST.beginStage(...)` rather than direct stage-handle
  construction.
- The main `cts.log` keeps markdown-like hierarchy and tables only; it does not
  emit `Notes` titles, narrative note prose, prose-only detail blocks, or empty
  placeholder subsections such as `### Synthesis Flow`.
- `Diagnostic` tables are still allowed because they are structured
  status/fallback output rather than narrative notes.
