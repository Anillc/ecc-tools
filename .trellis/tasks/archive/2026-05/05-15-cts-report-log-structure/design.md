# CTS Report Log Structure Design

## Objective

Refactor iCTS structured report emission so the default `cts.log` is a dense engineering report with concise summaries, while curated detailed internals are written to a sibling `cts_detail.log`.

## Constraints

- Follow `.trellis/spec/backend/logging-guidelines.md`.
- Use `LOG_*` for runtime diagnostics.
- Use schema/report helpers for structured file output.
- Do not add a new macro that hides console logging and file writing behind one generic wrapper.
- Keep field construction close to data owners.
- Preserve failure, skip, fallback, and first-failure diagnostics.
- Avoid broad refactors outside `src/operation/iCTS/`.
- Do not preserve the old noisy default report shape for compatibility. Repeated, redundant, or useless content should be removed from `cts.log`; useful detail should be moved to `cts_detail.log`.

## Current Architecture

`SchemaWriter` owns file output for `cts.log`. It supports:

- titled tables
- key-value tables
- detail blocks
- diagnostics
- artifacts
- runtime metrics
- stage scopes

`StageScope` currently auto-emits two types of structured report tables:

- `<Module> <Stage> Context` at construction when start fields are provided
- `<Module> <Stage> Summary` when the stage closes

This is convenient but too coarse as a default report policy. Any helper that wants scoped status creates visible `cts.log` content, even when the stage is only an implementation detail.

## Proposed Report Outputs

Produce two structured report files per normal CTS run:

- `cts.log`: default high-density engineering report.
- `cts_detail.log`: curated deep-dive report for algorithm internals and stage lifecycle detail.

The active writer should support routing a section/table to the default report, the detail report, or both.

Recommended output policy:

- Default-only: final run summary, key input clock ownership, selected CTS decisions, final QoR, runtime, artifact paths, warnings/errors/fallbacks.
- Detail-only: per-depth frontier internals, root compensation counters, bucket-index details, long per-level lists, helper stage lifecycle summaries, cache hit/load-resolution counters.
- Both: diagnostics that change interpretation of the run, artifact references, and concise selected-result identifiers that connect default and detail reports.

If a verbosity concept is useful internally, use it as an implementation detail:

```cpp
enum class ReportVerbosity
{
  kSummary,
  kDefault,
  kDebug
};
```

But the externally required product is the two-file split, not just a configurable verbose mode.

## Schema API Changes

Add a structured way to route and suppress stage lifecycle tables without removing console stage markers:

```cpp
struct StageReportOptions
{
  bool emit_context = true;
  bool emit_summary = true;
  bool emit_on_success = true;
  ReportSink sink = ReportSink::kDefault;
  ReportVerbosity min_verbosity = ReportVerbosity::kDefault;
};
```

Then add an overload:

```cpp
auto beginStage(std::string module,
                std::string stage,
                const KeyValueFields& start_fields,
                StageReportOptions options) -> StageScope;
```

`StageScope` should still emit console lifecycle markers via `LOG_INFO`, but `cts.log` should only receive Context/Summary tables when the stage is meaningful for the concise report. Failures, skips, warnings, and fallback diagnostics should remain visible in `cts.log`; successful helper lifecycle summaries can route to `cts_detail.log` or be suppressed.

This allows low-risk incremental migration:

- Keep existing behavior for untouched stages.
- Route clearly noisy but useful helper details to `cts_detail.log`.
- Suppress useless successful helper tables that add only `status=finished`.
- Replace repeated stage tables with aggregated report tables at data-owner boundaries.

`SchemaWriter::open(...)` should initialize the default report path and the detail report path together. For normal setup, if `cts.log` is:

```text
result/cts/cts.log
```

the detail log should be:

```text
result/cts/cts_detail.log
```

## Default Report Layout

Recommended default `cts.log` order:

1. Header and `Run Context`
2. `CTS Key Results` near the top after setup or repeated near the bottom only if top placement is too invasive
3. `Runtime Setup`
4. `Input Clock Overview`
5. `Synthesis Overview`
6. `HTree Characterization Summary`
7. `HTree Depth Candidate Summary`
8. `Selected HTree Summary`
9. `Source Trunk Summary`
10. `CTS Clock Tree Synthesis Overview`
11. `CTS Instantiation Overview`
12. `CTS Evaluation Overview`
13. `CTS Runtime Overview`
14. `Report Overview` / generated artifacts
15. Diagnostics, if not already inline

Default report content should be biased toward dense, decision-relevant rows. A table should survive in `cts.log` only if it answers one of these questions:

- What input clock data did CTS use?
- What topology/route/sizing decision did CTS select?
- Did CTS meet QoR expectations?
- What artifacts were generated?
- What warnings, errors, fallbacks, or abnormal conditions affect trust in the result?

## HTree Depth Candidate Collapse

Replace per-depth repeated tables with one table emitted after depth search.

Suggested columns:

```text
Depth
Levels
Input Frontier Entries
Closed Candidates
Rejected Candidates
Final Frontier Entries
Topology Patterns
Feasible Entries
First Failure
Selected
```

Optional debug-only columns:

```text
Cap Bucket Mismatches
Slew Bucket Mismatches
Unique Direct Lookups
Direct Cache Hits
Load Resolutions
Load Resolution Cache Hits
Route Estimates
Fallback Route Estimates
```

Implementation approach:

- Extend the existing depth-search result structs if they already hold the needed data.
- If the data is only present in local variables currently passed to `StageScope::finished`, move it into the per-depth evaluation summary object.
- Emit one `HTree Depth Candidate Summary` from `HTree.cc` after `SearchTopologyDepthCandidates(...)` returns.
- Mark the internal `HTreeDepth` build/filter/compensation stages as detail-only for `cts_detail.log`.
- If the default aggregate table already reports a counter, do not repeat the same counter in another default table unless it has different scope.

## HTree Scope Context

Emit one compact scope table before HTree characterization/selection:

```text
Clock
Net
Sink Domain
Stage
Object Prefix
Topology Loads
Topology Depth
Leaf Count
```

Then remove repeated scope rows from:

- `HTree Characterization Grid Plan`
- `HTree Characterization Overview`
- `HTree Synthesis Overview`

These tables should focus on their own data, not restate unchanged context.

## Selected HTree Summary

Split the current wide `HTree Synthesis Overview` into default and detail sections.

Default table should include:

- selected depth
- selected topology pattern id
- selected segment pattern ids, possibly shortened
- selected buffer count and weighted buffer count
- selected buffer area and weighted buffer area
- final frontier count
- inserted insts/nets
- pruned leaf buffers
- delay/power
- root-driver cell and compensation summary
- boundary fallback state
- selected H-tree load cap min/max/mean/median

Move to `cts_detail.log`:

- every per-level buffer count/area if too long
- raw bucket indices
- cap/slew bucket deltas
- detailed root physical load explanation
- long policy explanations

If retained in default output, long list fields should be capped and point to `cts_detail.log`.

## Characterization Tables

Keep:

- resolved wirelength unit
- wirelength iteration/bin counts
- buffer list
- sweep progress summary
- overflow counts and ratios

Remove or reword report-mechanics rows such as `Source = deduplicated`. A better default is:

```text
Parameter | Value | Source
max_slew  | 0.5000 ns | runtime_config
max_cap   | 0.1500 pF | runtime_config
```

Use detail text only when a fallback or auto-derivation happened.

## Detail Report Layout

Recommended `cts_detail.log` structure:

1. Same run context and CTS setup identifiers as `cts.log`, kept compact.
2. HTree scope context per clock/domain.
3. Per-depth candidate detail tables.
4. Root-driver compensation detail and counters.
5. Selected topology per-level and bucket detail.
6. Characterization detail beyond the concise default summary.
7. Helper stage lifecycle summary for stages that are useful during debugging.
8. Diagnostics copied from the default report with enough context to debug.

The detail report should not blindly mirror every default section. Its job is to carry detailed evidence behind the concise decisions.

## Console vs File Output

The existing free functions `schema::EmitTable` and `schema::EmitKeyValueTable` write to both `LOG_INFO` and `cts.log`. This is already entrenched, but new report code should be explicit about its intent:

- use `SCHEMA_WRITER_INST.emit...` for file-only report sections
- use `LOG_*` separately for runtime messages
- use existing `schema::Emit...` only when dual emission is intentionally desired

Do not introduce another dual-write facade.

## Compatibility

The default `cts.log` format is a human-facing report, so exact table ordering can change. The new `cts_detail.log` is also a structured report artifact and should be asserted in tests. Existing tests that assert important section presence must be updated carefully:

- Retain stable titles for high-level sections where possible.
- If a noisy table is removed, replace tests with assertions for the new aggregate table.
- Generated statistic files and visualization outputs should not change except for adding `cts_detail.log`.

## Rollback

The safest rollback path is to keep useful old internal emitters routed to `cts_detail.log`. If a default report omission is later found to hide an important engineering signal, promote that specific summarized field back into `cts.log`; do not restore the full trace-heavy default report.
