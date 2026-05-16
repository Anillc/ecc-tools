# Optimize ecc_dev_tools iCTS Runtime Implementation Plan

Date: 2026-05-16

## Preconditions

- Active task: `.trellis/tasks/05-16-optimize-ecc-dev-icts-runtime`
- Task status must remain `planning` until the user approves implementation and `task.py start` is run.
- Do not modify or revert unrelated dirty file `src/apps/CMakeLists.txt`.
- Preserve full iCTS check coverage and semantics.

## Implementation Checklist

1. Planning gate
   - Review `prd.md`, `analysis.md`, `research.md`, and `design.md`.
   - Confirm implementation scope is the first slice only unless the user explicitly expands it:
     - parallelize `tidy-headers`
     - add focused tests
     - optionally adjust minimal notes/runtime details only if necessary for verification
   - Ask for user approval before `task.py start`.

2. Load development specs
   - Use `trellis-before-dev` after the task is started and before code edits.
   - Load relevant backend/spec guidance for `.trellis/ecc_dev_tools`.

3. Parallelize header tidy
   - In `.trellis/ecc_dev_tools/checkers.py`, pass `jobs=jobs` from `_run_tidy_pass()` into `_run_clang_tidy_header_pass()`.
   - Add `jobs: int = 1` to `_run_clang_tidy_header_pass()`.
   - Extract the per-header body into an inner `_run_one_header(header)` worker.
   - Run `header_results = _parallel_map(_run_one_header, headers, jobs)`.
   - Aggregate successful worker results in deterministic repo-relative header order.
   - Append error notes for exception results following existing checker style.
   - Keep command construction, include-dir inference, parsing, and runtime entries behavior equivalent to the current serial code.

4. Unit tests
   - Update `.trellis/ecc_dev_tools/tests/test_core.py`.
   - Keep the existing `test_reports_all_parsed_findings` passing with the new `jobs` argument default.
   - Add a test that patches `_parallel_map()` for `_run_clang_tidy_header_pass()` to confirm the function passes through the requested `jobs`.
   - Add a test that simulates reversed/out-of-order header worker results and verifies deterministic aggregation, or use a controlled fake `_parallel_map()` that returns results out of input order.
   - Add an exception-handling test only if the implementation changes current behavior for `jobs > 1`.

5. Fast validation
   - Run:

```bash
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
```

6. Focused functional validation
   - Run a small iCTS header/tidy subset with runtime detail to verify command behavior:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh \
  --path src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.hh \
  --profile icts \
  --build-dir build \
  --kinds tidy \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
```

   - Confirm:
     - command exits successfully
     - `tidy-headers` runs for the selected headers
     - final in-scope findings after suppressions remain acceptable for the focused scope

7. Full runtime validation
   - Run the baseline-comparable command:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

   - Record:
     - wall time
     - reported runtime total
     - per-kind runtime
     - tidy phase runtime, especially `tidy-headers`
     - final in-scope findings after suppressions
     - final out-of-scope findings after suppressions

8. Update artifacts
   - Append post-change results to `analysis.md`.
   - Update `prd.md` acceptance checkboxes only when evidence exists.
   - If the change reveals a reusable project rule, use `trellis-update-spec`; otherwise note that no spec update was needed.

## Validation Gates

Required before calling the implementation done:

- Unit tests pass:

```bash
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
```

- Full iCTS validation exits successfully with `--no-fail-on-findings`.
- Full iCTS final JSON reports `0` in-scope findings after suppressions.
- Runtime comparison shows a measurable reduction from the captured baseline, especially in `tidy-headers`.

## Out-of-Scope For First Slice

- Removing or disabling IWYU.
- Adding a reduced-coverage fast preset as the solution.
- Skipping slow files.
- Adding broad suppressions.
- Changing default tidy checks.
- Changing IWYU semantics or using `--transitive_includes_only`.
- Product-code decomposition of STA/SDC adapters.
- Introducing a clang-tidy/IWYU cache wrapper.

## Follow-Up Backlog

These are candidates after the first slice is measured:

- Add phase-aware runtime unit labels or a structured `RuntimeEntry.phase` field.
- Add opt-in clang-tidy check profiling support using `--enable-check-profile` or `--store-check-profile`.
- Evaluate parameter-file command construction for large `tidy-headers` command lines.
- Investigate IWYU load-aware scheduling if CPU/memory pressure is observed.
- Plan a separate dependency-hygiene task for STA/SDC adapter hotspots.
- Plan large-file decomposition only where a natural behavior-preserving CTS/EDA boundary exists.

## Second Slice Checklist: Combined tidy/analyzer TU Pass

1. Parser support
   - Extend `_parse_clang_tidy_output()` with an optional origin resolver.
   - Keep existing fixed-origin behavior unchanged for current callers.
   - Add unit coverage for mixed tidy/analyzer diagnostics.

2. Combined runner
   - Add helper for combining check globs from `tidy-tu` and `analyzer-tu`.
   - Add `_run_clang_tidy_combined_tu_pass()`.
   - Reuse existing clang-tidy TU command construction and `_parallel_map()`.
   - Map `clang-analyzer-*` diagnostics to `analyzer-tu`; map non-analyzer diagnostics to `tidy-tu`.
   - Preserve fallback candidate behavior.

3. Tidy orchestration
   - Update `run_tidy_check()` to combine adjacent `tidy-tu` + `analyzer-tu` passes in deep mode.
   - Keep `plan.tidy_passes` unchanged so dedupe priorities remain available.
   - Record phase runtime as `tidy-tu+analyzer-tu`.

4. Tests
   - Add unit test for origin resolver.
   - Add unit test for combined runner command count and origin mapping.
   - Add unit test for fallback candidate behavior with no diagnostics.
   - Keep all existing tests green.

5. Validation
   - Run unit tests.
   - Run focused tidy JSON validation.
   - Run full iCTS validation with `--no-fail-on-findings --runtime-detail --runtime-logging`.
   - Compare final in-scope findings and runtime with previous artifacts.

## Second Slice Completion Notes

Date: 2026-05-16

The combined tidy/analyzer TU pass checklist is complete:

- `_parse_clang_tidy_output()` accepts an optional origin resolver while preserving fixed-origin behavior for existing callers.
- `_combine_tidy_checks()` merges `tidy-tu` and `analyzer-tu` check globs without a secondary `-*` reset.
- `_run_clang_tidy_combined_tu_pass()` reuses the existing clang-tidy TU runner and maps `clang-analyzer-*` diagnostics back to `analyzer-tu`.
- `run_tidy_check()` combines adjacent `tidy-tu` + `analyzer-tu` clang-tidy TU passes and records phase runtime as `tidy-tu+analyzer-tu`.
- Fallback candidate behavior is preserved: zero parsed diagnostics or suppressed-only diagnostics still queue `.cc` commands for native fallback.

Validation completed:

```bash
python3 -m py_compile .trellis/ecc_dev_tools/check.py .trellis/ecc_dev_tools/checkers.py .trellis/ecc_dev_tools/tests/test_core.py
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/source/database/config/Config.cc \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --kinds tidy \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Validation results:

- `py_compile`: passed.
- Unit tests: passed, 185 tests.
- Focused tidy validation: passed, zero final findings, `tidy-tu+analyzer-tu` phase present.
- Full iCTS validation: passed, final in-scope findings 0, final out-of-scope findings 5119.
- Full jobs=16 wall time: 437.29s.
- Full jobs=16 reported checker runtime: 435.624s.

## Third Slice Completion Notes: Header Self-Check Subcheck Queue

Date: 2026-05-16

The header self-check runner now schedules direct header compilation and include-first compilation as separate subcheck tasks in one shared `_parallel_map()` queue.

Preserved behavior:

- Every scoped header still runs the direct self-contained header check.
- Every scoped header still runs the include-first wrapper check.
- Include-dir inference, trigger command selection, compiler selection, finding categories/subtypes, and suppressions are unchanged.
- Findings are aggregated deterministically by header label, subcheck phase, and finding sort key.

Implementation details:

- `HeaderCheckPlan` captures per-header setup once: header, owner, include directories, and trigger command.
- The direct subcheck records unit runtime as `<path>`.
- The include-first subcheck records unit runtime as `include-first:<path>`.
- The phase runtime remains `header-self-check`, with count equal to the number of headers covered.

Validation completed:

```bash
python3 -m py_compile .trellis/ecc_dev_tools/check.py .trellis/ecc_dev_tools/checkers.py .trellis/ecc_dev_tools/tests/test_core.py
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.hh \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh \
  --path src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.hh \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --kinds headers \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Validation results:

- `py_compile`: passed.
- Unit tests: passed, 188 tests.
- Focused header validation: passed; runtime total 0.719s, `header-self-check` 0.441s, and runtime detail included both direct and `include-first:<path>` labels.
- Full iCTS validation: passed, final in-scope findings 0, final out-of-scope findings 5119.
- Full jobs=16 wall time: 417.53s.
- Full jobs=16 reported checker runtime: 415.878s.
- Full jobs=16 phase highlights: `tidy` 167.126s, `headers` 34.879s, `iwyu` 211.100s, `header-self-check` 31.942s, `tidy-tu+analyzer-tu` 111.722s, `tidy-headers` 10.403s.

## Rejected Experiment: Kind-Level Parallelism

Date: 2026-05-16

An experiment overlapped `tidy`, `headers`, `cmake`, and `iwyu` in one Python process with `jobs_per_kind=8`.

Result:

- Wall time: 416.87s, effectively the same as the final sequential-kind full run at 417.53s.
- `tidy` runtime inflated to 366.905s.
- `iwyu` runtime inflated to 415.221s.
- `headers` runtime inflated to 66.594s.

Decision:

- Do not implement kind-level parallelism as the default checker behavior.
- The experiment overlaps work but causes substantial Clang/IWYU resource contention and provides no meaningful wall-clock win on this machine.

## Rejected Experiment: Lower Full-Check Worker Count

Date: 2026-05-16

A full final checker run was repeated with `--jobs 8` to test whether lower parallelism would reduce Clang/IWYU contention.

Result:

- Final in-scope findings after suppressions: 0.
- Final out-of-scope findings after suppressions: 5119.
- Wall time: 689.89s.
- Reported checker runtime: 688.262s.
- `tidy`: 285.159s.
- `headers`: 54.735s.
- `iwyu`: 345.497s.

Decision:

- Do not lower the default or recommended full-check worker count for this workload.
- `--jobs 8` preserves findings, but it is much slower than the final `--jobs 16` run at 417.53s wall time.

## Final Quality Gate

Date: 2026-05-16

Commands run after all code and documentation updates:

```bash
python3 -m py_compile .trellis/ecc_dev_tools/check.py .trellis/ecc_dev_tools/checkers.py .trellis/ecc_dev_tools/tests/test_core.py
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
python3 ./.trellis/scripts/task.py validate 05-16-optimize-ecc-dev-icts-runtime
git diff --check -- .trellis/ecc_dev_tools/check.py .trellis/ecc_dev_tools/checkers.py .trellis/ecc_dev_tools/tests/test_core.py .trellis/ecc_dev_tools/README.md .trellis/spec/backend/quality-guidelines.md .trellis/tasks/05-16-optimize-ecc-dev-icts-runtime
```

Results:

- `py_compile`: passed.
- Unit tests: passed, 191 tests.
- Task context validation: passed.
- `git diff --check`: passed.
- Final full iCTS validation was run and summarized in `analysis.md`: jobs=16, 417.53s wall time, 415.878s reported checker runtime, 0 final in-scope findings after suppressions. Raw JSON/stderr run artifacts were intentionally removed before commit.

## Completion Audit

Date: 2026-05-16

Objective restated:

- Optimize default `ecc_dev_tools` iCTS check runtime as much as possible without reducing check surface or check quality.
- Prioritize `tidy-headers` parallelism.
- Avoid shortcut optimizations such as skipping checks, changed-file-only validation, broad suppressions, or disabling expensive checks.
- Analyze whether large single files explain `tidy`/IWYU bottlenecks.
- Preserve final full iCTS validation with zero in-scope findings after suppressions.

Requirement-to-evidence checklist:

| Requirement | Evidence | Status |
| --- | --- | --- |
| Current runtime distribution captured | `analysis.md` baseline: 569.365s wall, `tidy` 319.318s, `iwyu` 210.705s, `headers` 34.805s | Done |
| Official/community `clang-tidy` and IWYU acceleration research captured | `research.md` covers `run-clang-tidy.py -j`, check profiling, IWYU `-j/-l`, mapping files, caching, and rejected filters/skip modes | Done |
| `tidy-headers` reuses checker parallelism first | `_run_clang_tidy_header_pass()` accepts `jobs` and uses `_parallel_map()`; tests `TestRunClangTidyHeaderPass` cover jobs pass-through and deterministic aggregation | Done |
| No check surface or quality reduction | Default kinds remain `format`, `tidy`, `headers`, `cmake`, `iwyu`; no skip list, no disabled checks, no changed-file-only mode, no broad suppressions added | Done |
| Combined tidy/analyzer does not reduce coverage | `_combine_tidy_checks()` preserves normal tidy globs and adds `clang-analyzer-*`; origin resolver maps analyzer diagnostics back to `analyzer-tu`; tests cover parser mapping, command execution, and fallback behavior | Done |
| Header self-check coverage preserved | Each header still schedules both direct and include-first subchecks; tests cover `2 * header_count` queueing and deterministic out-of-order aggregation | Done |
| Full iCTS remains clean | Final jobs=16 full-run summary in `analysis.md`: `in_scope=0`, `out_of_scope=5119`, all check kinds completed | Done |
| Runtime improvement measured against baseline | Final jobs=16 full-run summary in `analysis.md`: 417.53s wall vs 569.365s baseline, 26.7% faster; reported checker runtime 415.878s vs 567.716s baseline | Done |
| Large-file vs dependency-weight question answered | `analysis.md` shows small `STAAdapter.cc` is an IWYU hotspot while much larger files are not top IWYU blockers; STA/SDC dependency graph weight is the main remaining cause | Done |
| Non-shortcut alternatives evaluated | Kind-level parallelism and lower `--jobs 8` full-check experiments measured and rejected; IWYU already parallelized; slow IWYU files are already near front of compile command order; caching/load-aware/profile modes recorded as follow-up diagnostics, not default runtime shortcuts | Done |
| Quality gates passed | Final `py_compile`, 191 unit tests, task context validation, and `git diff --check` passed | Done |

Conclusion:

- Runner-level, no-coverage-loss optimizations identified for this task have been implemented or explicitly rejected with measurements.
- The remaining major runtime opportunity is product-code dependency hygiene in STA/SDC adapter TUs. That work touches design/include boundaries and should be a separate refactor task with its own correctness review and before/after runtime evidence.
