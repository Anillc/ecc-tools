# Optimize ecc_dev_tools iCTS Runtime Design

Date: 2026-05-16

## Objective

Reduce wall-clock runtime for the default full iCTS `ecc_dev_tools` validation while preserving the current check surface, finding semantics, suppression behavior, and quality signal.

The first implementation slice is intentionally narrow: parallelize the existing `tidy-headers` pass. Later slices should improve observability and investigate dependency-shape reductions without using skip lists, reduced presets, broad suppressions, or disabled checks as the optimization.

## Current Architecture

The relevant code is in `.trellis/ecc_dev_tools/checkers.py`.

- `run_selected_checks()` executes check kinds in default preset order: `format`, `tidy`, `headers`, `cmake`, `iwyu`.
- `run_tidy_check()` executes each planned tidy pass serially and records pass-level runtime entries.
- `_run_tidy_pass()` dispatches to pass-specific runners.
- `_run_clang_tidy_tu_pass()`, `_run_clang_frontend_pass()`, `_run_native_compiler_pass()`, header self-check, dependency scan, format, and IWYU already use `_parallel_map()`.
- `_run_clang_tidy_header_pass()` currently loops over headers serially and is the largest tidy subphase in the baseline: 125.820s across 151 headers.
- `run_iwyu_check()` already parallelizes over scoped compile commands with `jobs`.

`_parallel_map()` uses a `ThreadPoolExecutor` and returns results in completion order for parallel jobs. That is fine for many aggregate-only checks, but header tidy must keep deterministic aggregation because finding order, notes, and top runtime tie-breaks should remain stable.

## First Slice: Parallelize tidy-headers

### Contract

The header tidy pass must continue to:

- Run the same `clang-tidy` binary selected by environment validation.
- Use the same `.clang-tidy` config handling.
- Use the same `checks_arg` from the `TidyPass`.
- Build the same exact-header filter for each header.
- Infer the same include directories from target ownership, declared links, trigger compile command include flags, profile interface include dirs, header parent, and `repo_root/src`.
- Parse output through `_parse_clang_tidy_output()` with `origin=tidy_pass.name`.
- Report the same findings after the existing tidy-level dedupe and suppression filtering.
- Record one command per attempted header.
- Preserve `runtime_detail` support for top header-unit timings.

### Scheduling

Add `jobs` to `_run_clang_tidy_header_pass()` and pass it through from `_run_tidy_pass()`.

Within `_run_clang_tidy_header_pass()`:

1. Keep setup outside the worker:
   - `clang_tidy_status`
   - `config_args`
   - `checks_arg`
   - `targets`
   - `target_include_sets`
2. Move per-header command construction and parsing into a worker function.
3. Run workers with `_parallel_map(_run_one_header, headers, jobs)`.
4. Have each worker return:
   - header path or repo-relative label
   - findings
   - runtime entry
5. Aggregate only after all workers finish.

### Determinism

Because `_parallel_map()` returns completion order under parallel jobs, the header pass should not aggregate in returned order.

Sort successful header worker results by repo-relative header label before extending `outcome.findings`, appending runtime entries, and incrementing `commands_run`.

Exception results should be collected into notes after successful result aggregation. Sort exception notes by their string message or by header label if the worker catches and returns header-labeled failures. The first implementation can follow existing checker behavior and append `"Tidy header pass error: ..."` notes for exceptions.

This keeps the change scoped to header tidy and avoids changing `_parallel_map()` semantics globally, which could affect unrelated tests or expectations.

### Runtime Detail Labels

Current tidy runtime details are category `unit` entries labeled only by file path. That causes the same TU or header to appear multiple times across tidy phases without phase context.

For the first implementation slice, preserve the current output schema and avoid changing unit labels. The phase-level entry already records `tidy-headers`.

As a follow-up observability slice, consider adding phase context without breaking existing consumers. Two compatible options:

- Change only tidy unit labels to `"{phase}:{path}"`.
- Add an optional field to `RuntimeEntry`, such as `origin` or `phase`, and update JSON/text rendering.

The first option is smaller but changes label strings. The second option is cleaner but touches model/reporting serialization and tests. Do not include this in the first parallelism patch unless needed for validation.

## IWYU Design Position

IWYU remains enabled in the default full iCTS validation.

Current state:

- `run_iwyu_check()` already uses `_parallel_map(analyze_one, commands, jobs)`.
- Baseline IWYU runtime was 210.705s for 191 translation units.
- Top IWYU units are STA/SDC adapter files with heavy dependency graphs, not simply the largest source files.

Allowed future IWYU work:

- Improve runtime attribution for IWYU tail tasks.
- Add load-aware throttling or adaptive job sizing only if measurements show resource pressure or regression under fixed `jobs`.
- Investigate dependency hygiene in heavy adapter TUs:
  - reduce public header fan-in where safe
  - use forward declarations where safe
  - split implementation boundaries along real adapter responsibilities
  - add IWYU mappings for correctness/noise if project/library public/private header relationships need them

Disallowed as runtime fixes for this task:

- Removing IWYU from default validation.
- Running IWYU only on changed files as the default full iCTS path.
- Skipping slow files.
- Suppressing broad classes of IWYU findings.
- Using IWYU options such as `--transitive_includes_only` without a separate correctness review.

## Code Size and Dependency-Shape Position

The task should treat code volume and dependency weight as separate causes.

Current evidence:

- `STAAdapter.cc` is only 85 lines but was one of the slowest IWYU TUs.
- `CharacterizationRealTechExactRegressionTest.cc` is 1919 lines but was not a top IWYU hotspot.
- `ClockTraceResolver.cc` is both large and dependency-heavy, so it remains a strong follow-up candidate.

Design implication:

- Do not split files only because line count is high.
- Do not assume large-file decomposition will fix IWYU.
- For follow-up product-code refactors, require a natural boundary and before/after runtime evidence.

## Compatibility

The first slice should be behavior-compatible:

- CLI flags remain unchanged.
- JSON output schema remains unchanged.
- Text output schema remains unchanged.
- Default preset remains `format`, `tidy`, `headers`, `cmake`, `iwyu`.
- Default tidy mode remains `deep`.
- The number of header tidy commands remains equal to the number of collected scope headers.
- Findings should remain equivalent apart from incidental ordering that is normalized by deterministic aggregation and existing sorting/deduping.

## Risks

### CPU or memory pressure

Parallel header tidy may run more simultaneous `clang-tidy` processes. This is the intended speedup path, but it can increase memory pressure.

Mitigation:

- Reuse existing `jobs` instead of inventing a separate value.
- Preserve `--jobs` CLI override behavior through the existing environment/snapshot path.
- Validate with a full iCTS run using default `jobs=16` on this machine.

### Non-deterministic finding order

Parallel completion order can scramble aggregation.

Mitigation:

- Sort header worker results by repo-relative header label before aggregation.
- Keep existing final finding sorting/deduping behavior.
- Add a unit test that forces out-of-order worker completion or mocks `_parallel_map()` to return reversed results, then verifies deterministic command count and finding aggregation.

### Hidden behavior change in error handling

Current serial header pass propagates `run_command` exceptions when `jobs <= 1`. Existing parallel helper returns exceptions as values for `jobs > 1`.

Mitigation:

- Match existing checker patterns used by TU/frontend/native/IWYU passes: append a note and continue when a parallel worker returns an exception.
- Keep sequential behavior through `_parallel_map()` for `jobs <= 1`, where exceptions still propagate. This preserves current single-job semantics.

## Rollback

Rollback is simple:

- Revert the signature changes passing `jobs` into `_run_clang_tidy_header_pass()`.
- Restore the serial header loop.
- Remove the focused unit test for header-pass parallel aggregation.

No build metadata, profile defaults, suppressions, or product source files need migration.

## Expected Impact

The baseline `tidy-headers` phase took 125.820s and was serial. With 151 headers and default `jobs=16`, the theoretical best case is bounded by the slowest headers and process/memory contention, not by total header runtime divided by 16 exactly.

Reasonable expectation:

- Full `tidy` runtime should drop materially because `tidy-headers` was 39.4% of tidy runtime.
- Full iCTS runtime should drop measurably because `tidy-headers` was 22.1% of total full-check wall time.
- IWYU runtime is not expected to change in the first slice.

The acceptance measurement should compare the post-change full iCTS command against the captured 569.365s baseline and ensure the final in-scope finding count remains zero after suppressions.

## Second Slice: Combine tidy-tu and analyzer-tu Execution

### Objective

Reduce repeated Clang parsing work in deep tidy mode by executing the adjacent `tidy-tu` and `analyzer-tu` translation-unit passes as one `clang-tidy` process per compile command.

This keeps check coverage intact:

- normal deep tidy check globs remain enabled
- `clang-analyzer-*` remains enabled
- headers, frontend, native fallback, IWYU, format, cmake, and suppressions remain enabled

### Contract

The logical plan still treats `tidy-tu` and `analyzer-tu` as distinct sources of diagnostics for dedupe and reporting semantics:

- normal tidy diagnostics keep origin `tidy-tu`
- `clang-analyzer-*` diagnostics keep origin `analyzer-tu`
- compiler diagnostics from the combined invocation are treated as `tidy-tu` diagnostics because they are emitted by the shared frontend before individual tidy/analyzer checks
- existing `TidyPass` dedupe priorities remain available from `plan.tidy_passes`
- native fallback candidate behavior remains equivalent: a command is queued only if the combined tidy/analyzer invocation yields no parsed diagnostics, or if clang-tidy reports only suppressed warnings

### Execution Shape

In `run_tidy_check()`:

1. Iterate planned tidy passes by index rather than a simple `for`.
2. When an adjacent pair matches:
   - first pass name `tidy-tu`, runner `clang-tidy-tu`
   - second pass name `analyzer-tu`, runner `clang-tidy-tu`
3. Run a combined TU runner once for that pair.
4. Record one phase runtime entry with label `tidy-tu+analyzer-tu`.
5. Skip the already-combined analyzer pass in the loop.

The combined runner should reuse the existing TU runner mechanics:

- same clang-tidy binary selection
- same `.clang-tidy` config args
- same compile database `-p`
- same `--quiet`
- same `--header-filter`
- same `_parallel_map()` / `jobs`
- same parsing and fallback candidate structure

### Origin Mapping

Extend the clang-tidy parser so callers can optionally provide an origin resolver.

Default behavior remains unchanged:

```text
fixed origin argument -> every parsed finding uses that origin
```

Combined pass behavior:

```text
category == "clang-analyzer" -> analyzer-tu
otherwise                    -> tidy-tu
```

This avoids splitting output text into multiple parser passes and keeps one parse over the combined clang-tidy output.

### Compatibility

Expected output differences:

- Runtime phase output has `tidy-tu+analyzer-tu` instead of separate `tidy-tu` and `analyzer-tu` phase entries.
- Tidy unit runtime labels for the combined runner are prefixed with `tidy-tu+analyzer-tu:`.

Preserved behavior:

- check coverage
- final finding categories/subtypes
- final suppressions
- final in-scope/out-of-scope counts
- analyzer diagnostics remain analyzer-originated for dedupe priority
- full iCTS validation remains the source of truth

### Risks

#### Analyzer finding origin drift

If analyzer diagnostics are not mapped back to `analyzer-tu`, dedupe priority and triage semantics can change.

Mitigation:

- Unit-test parser origin mapping for a mixed tidy/analyzer output.
- Keep `analyzer-tu` in `plan.tidy_passes` so `_dedupe_findings()` still has the existing priority map.

#### Native fallback drift

Separate passes could queue fallback candidates independently. A combined pass queues once.

Mitigation:

- The fallback decision should be based on whether any combined tidy/analyzer diagnostic was parsed for the TU. This is equivalent after the later existing filter removes candidates with findings from other passes.
- Unit-test no-diagnostic and diagnostic cases.

#### Runtime variability

Combined pass can reduce duplicate parsing, but high system load or too many jobs can still dominate wall time.

Mitigation:

- Record `Worker jobs: N` in notes.
- Use explicit `--jobs N` for comparable runtime experiments.
