# Optimize ecc dev iCTS runtime

## Goal

Reduce the wall-clock runtime of the default `ecc_dev_tools` iCTS validation path while preserving the safety signal expected from the existing full-module check.

The first step is to understand the current runtime distribution of `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` before choosing implementation changes.

## Confirmed Facts

- The iCTS profile is defined in `.trellis/ecc_dev_tools/profiles.py` with the default preset `default`, default tidy mode `deep`, and default pass plan `complete`.
- The default preset runs five check kinds in order: `format`, `tidy`, `headers`, `cmake`, and `iwyu`.
- The iCTS scope currently contains 337 profiled C/C++ files: 186 `.cc` files and 151 `.hh` files.
- The active build metadata has 191 iCTS compile commands.
- Existing runtime instrumentation reports per-kind runtime, selected per-phase runtime, and top unit-level runtime when `--runtime-detail` is enabled.
- Baseline run on 2026-05-16, with build metadata reused and `jobs=16`, took about 569 seconds wall time for full iCTS default validation.
- Runtime distribution from that run:
  - `tidy`: 319.318s, 56.2%
  - `iwyu`: 210.705s, 37.1%
  - `headers`: 34.805s, 6.1%
  - `cmake`: 2.658s, 0.5%
  - `format`: 0.229s, ~0.0%
- Within `tidy`, the largest phases were `tidy-headers` at 125.820s, `analyzer-tu` at 91.785s, and `tidy-tu` at 56.669s.
- `tidy-headers` processes 151 headers sequentially, while most TU-oriented checks already use the shared parallel runner.
- `iwyu` analyzes 191 translation units and is the second-largest runtime contributor.
- `headers` is dominated by `header-self-check` at 32.606s; `dependency-scan` was only 2.060s in this baseline.
- User decision: the first implementation priority is to reuse the existing checker parallelism for `tidy-headers`.
- User decision: runtime must not be optimized by reducing validation coverage, skipping check kinds, or adding a "shortcut" lane that hides work.
- Current evidence shows single-file code volume is a contributor for some files, but not the primary explanation for the worst `tidy`/`iwyu` bottlenecks. Heavy adapter translation units with broad iSTA/iDB/Liberty/Power dependencies dominate IWYU even when individual `.cc` files are small.
- External documentation research supports the chosen first slice: LLVM's clang-tidy docs recommend `run-clang-tidy.py -j` parallel execution for many files, while IWYU's driver already has `-j`/`-l` scheduling support that the current checker mostly mirrors for translation units.
- External documentation research also confirms several approaches are not acceptable runtime fixes for this task: clang-tidy diff/line filtering, diagnostic-only header filtering, CMake `SKIP_LINTING`, IWYU scope reduction, disabling expensive tidy checks, or IWYU options that change suggestion semantics.
- Post-change validation shows `tidy-headers` is no longer the structural bottleneck, but total wall-clock runtime is load-sensitive because default worker jobs are derived from current system load and heavy TU-level Clang tooling can vary significantly.
- A combined `tidy-tu`/`analyzer-tu` clang-tidy invocation was implemented after an explicit equivalence design: it preserves normal tidy checks, `clang-analyzer-*`, diagnostic origin mapping, dedupe priorities, native fallback behavior, suppressions, and full iCTS final finding counts.
- Header self-check task granularity was improved without reducing coverage: every header still runs both the direct self-contained check and include-first check, but those subchecks now share one parallel work queue.
- The final explicit `--jobs 16` full iCTS run after all checker scheduling slices took 417.53s wall time / 415.878s reported checker runtime, with 0 in-scope findings and 5119 out-of-scope findings after suppressions.
- A kind-level parallelism experiment was measured but rejected: overlapping `tidy`, `headers`, `cmake`, and `iwyu` caused heavy resource contention and did not materially improve wall time on this machine.
- The pre-existing uncommitted change in `src/apps/CMakeLists.txt` is unrelated and must not be overwritten.

## Requirements

- Preserve full iCTS check semantics and coverage.
- Do not optimize runtime by removing `iwyu`, skipping `tidy-headers`, hiding known slow files, adding broad suppressions, or introducing a fast preset that checks less than the current default.
- First implementation priority: parallelize `tidy-headers` by reusing the existing `_parallel_map()`/`jobs` model, while keeping findings and notes deterministic.
- Keep full iCTS default validation reporting `0` in-scope findings after suppressions.
- Optimize the major runtime contributors first: `tidy` and `iwyu`.
- Keep runtime reporting accurate enough to compare before/after runs by check kind and key subphase.
- Any change to checker behavior must include focused unit coverage under `.trellis/ecc_dev_tools/tests/`.
- Investigate heavy single-TU causes by separating source line count from dependency/include graph weight.

## Acceptance Criteria

- [x] A Trellis task exists for optimizing `ecc_dev_tools` iCTS runtime.
- [x] Current iCTS full-check runtime distribution is captured with command, environment, counts, and hotspot analysis.
- [x] User decisions on coverage policy and first implementation priority are captured.
- [x] Current slow-file analysis distinguishes code size from heavy dependency/include graph effects.
- [x] External clang-tidy/IWYU documentation and community runtime guidance is captured.
- [x] Optimization design identifies concrete changes, expected runtime impact, and behavior risks before implementation starts.
- [x] Implementation reduces the default full iCTS check runtime by a measurable amount against the captured baseline without reducing check coverage.
- [x] `python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v` passes.
- [x] A full iCTS `ecc_dev_tools` validation still reports zero in-scope findings after suppressions.

## Notes

- Runtime baseline details are recorded in `analysis.md`.
- Tool documentation research is recorded in `research.md`.
- First-slice post-change validation details are recorded in `analysis.md`.
- Remaining optimization opportunities and the combined clang-tidy TU experiment are recorded in `analysis.md`.
- Header self-check subcheck scheduling, the final jobs=16 runtime result, and the rejected kind-level parallelism experiment are recorded in `analysis.md`.
- This is a complex task because it changes development tooling behavior and validation coverage, so implementation should not start until `design.md` and `implement.md` exist.
