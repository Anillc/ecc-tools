# Tool Documentation Research: clang-tidy and IWYU Runtime

Date: 2026-05-16

## Question

What do official docs and community material recommend for accelerating `clang-tidy` and Include What You Use (IWYU), and which approaches fit this task's constraint that full iCTS validation coverage must be preserved?

## Sources Reviewed

- LLVM clang-tidy documentation: <https://clang.llvm.org/extra/clang-tidy/>
- LLVM `run-clang-tidy.py` source/doxygen: <https://clang.llvm.org/extra/doxygen/run-clang-tidy_8py_source.html>
- IWYU README: <https://github.com/include-what-you-use/include-what-you-use>
- IWYU man page source: <https://raw.githubusercontent.com/include-what-you-use/include-what-you-use/master/include-what-you-use.1>
- IWYU `iwyu_tool.py` source/help: <https://raw.githubusercontent.com/include-what-you-use/include-what-you-use/master/iwyu_tool.py>
- CMake clang-tidy target property: <https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html>
- CMake IWYU target property: <https://cmake.org/cmake/help/latest/prop_tgt/LANG_INCLUDE_WHAT_YOU_USE.html>
- IWYU mapping-file community thread: <https://groups.google.com/g/include-what-you-use/c/kZJOMXaBByc>
- Community discussion on clang-tidy slowness: <https://www.reddit.com/r/cpp_questions/comments/10jx0uc/clang_tidy_is_very_slow_when_using_headers_like/>
- Community discussion on clang-tidy caching: <https://stackoverflow.com/questions/53597508/is-it-possible-to-accelerate-clang-tidy-using-ccache-or-similar>

Local help was also checked for the installed tools:

```bash
clang-tidy --help
include-what-you-use --help
iwyu_tool.py --help
```

## clang-tidy Findings

### Parallel execution is officially supported

The LLVM documentation says `clang-tidy` can process many source files but is sequential by itself, and that `run-clang-tidy.py` provides parallel execution for large projects. The same documentation shows `run-clang-tidy.py -p=build/ -j 4`.

The `run-clang-tidy.py` source confirms the same model:

- `-j` is the number of tidy instances to run in parallel.
- If `-j` is zero, it uses the CPU count.
- It creates one task per file and gates execution with an async semaphore.

Task implication:

- This directly supports the first implementation slice: `tidy-headers` should use the repository's existing `_parallel_map()` / `jobs` model.
- This is not a coverage shortcut; it preserves the same per-header `clang-tidy` invocations and only changes scheduling.

### Profiling is officially supported

`clang-tidy --help` and the LLVM docs expose:

- `--enable-check-profile`: prints per-check timing profile to stderr.
- `--store-check-profile=<prefix>`: writes per-TU profiles as JSON.

`run-clang-tidy.py` also has explicit profile aggregation logic for JSON profile files.

Task implication:

- Add or use a diagnostic profiling mode only for investigation. It can identify whether `clang-analyzer-*`, `bugprone-*`, `readability-*`, etc. dominate runtime.
- Do not turn profiling on by default unless measured overhead is acceptable.
- Profiling does not reduce coverage and is compatible with the user's "no shortcut" constraint.

### Diagnostic filters are not runtime fixes

LLVM documents `--header-filter`, `--exclude-header-filter`, and `--line-filter` as controls for which diagnostics are emitted. It also explicitly states that `clang-tidy-diff.py` only reports diagnostics for changed lines while `clang-tidy` still analyzes the entire file, so it does not improve performance.

Task implication:

- Do not use `line-filter`, diff-only filtering, or changed-line filtering as this task's runtime solution.
- Do not treat `header-filter` / `exclude-header-filter` as parse-cost reduction. They can reduce output/noise, but they do not avoid parsing the translation unit and included headers.

### Parameter files are supported

LLVM documents `@parameter-file` support for a large number of options or source files.

Task implication:

- This may be useful for `tidy-headers`, where local commands include many `--extra-arg=-I...` entries.
- Expected benefit is mostly command-line size robustness and small process-launch overhead reduction. It is unlikely to change the dominant Clang parsing/analyzer cost.
- This is a secondary, semantics-preserving improvement after header parallelism.

### CMake integration exists but is not the current best fit

CMake's `<LANG>_CLANG_TIDY` target property runs clang-tidy along with compilation for Makefile/Ninja generators. CMake also has `SKIP_LINTING` for individual source files.

Task implication:

- CMake integration validates that build-graph scheduling is a normal way to run clang-tidy, but migrating `ecc_dev_tools` checks into CMake would be a larger architecture change.
- `SKIP_LINTING` is explicitly not acceptable as an optimization here because it removes files from linting.
- The current checker already owns runtime reporting, suppression handling, scope filtering, and pass plans, so local scheduling improvements are lower risk.

## IWYU Findings

### IWYU is driven by compilation database or build system

The IWYU README describes these supported modes:

- Run the binary on a single source file with the same compiler flags.
- Plug it into the build system.
- Use CMake's `CMAKE_CXX_INCLUDE_WHAT_YOU_USE`.
- Use `iwyu_tool.py` with `compile_commands.json`; if no source filenames are provided, it analyzes all files in the compilation database.

Task implication:

- The repository's current IWYU design, which analyzes scoped compile commands from `compile_commands.json`, matches upstream usage.
- Scope reduction would be an effective runtime reduction mechanically, but it would violate this task's coverage constraint unless done as a separate explicitly-approved mode.

### IWYU parallelism is officially supported in the driver

`iwyu_tool.py --help` supports:

- `-j/--jobs`: number of concurrent subprocesses.
- `-l/--load`: do not start new jobs if the one-minute load average is above the provided value.

The source implements a pending-process scheduler and only starts more IWYU jobs when capacity remains.

Task implication:

- Current `run_iwyu_check()` already uses `_parallel_map()` over compile commands with `jobs=16`, so the most obvious official acceleration path is already present.
- A later improvement could add load-aware throttling or adaptive job sizing if memory/CPU pressure is observed, but this is more about stability and tail behavior than guaranteed speedup.
- The slow IWYU tail in iCTS is more likely caused by heavy translation-unit dependency graphs than absence of parallel execution.

### IWYU mappings and pragmas are correctness/noise tools

The IWYU man page supports:

- `--mapping_file=<filename>`
- `--no_internal_mappings`
- `--export_mappings=<dirpath>`
- pragma comments such as `keep`, `export`, `private`, and `no_include`

The man page explains that mapping files describe relationships that are hard to infer from source alone, such as private/public headers and symbols provided by headers. A community thread confirms that mapping files can be passed through `iwyu_tool.py` after `--` by using `-Xiwyu --mapping_file=...`.

Task implication:

- Mappings can make IWYU more accurate for project/library boundaries and may reduce noisy or unstable suggestions.
- Mappings are not a primary runtime lever because IWYU still has to parse the translation unit and its include graph.
- They are reasonable follow-up work if the iCTS STA/iDB/Liberty/Power boundary needs project-specific public/private header knowledge.

### Some IWYU options change semantics

The IWYU man page includes options with potential behavior changes:

- `--transitive_includes_only`: only suggest adding a header if it is already visible in transitive includes.
- `--no_fwd_decls`: avoid forward declarations and always include required headers.
- `--no_internal_mappings`: disables built-in mappings.
- `--prefix_header_includes=add|keep|remove`: changes handling of prefix headers.
- `--pch_in_code`: preserves first include as PCH in PCH-in-code projects.

Task implication:

- These should not be used as default runtime optimizations without a separate correctness review.
- `--transitive_includes_only` in particular narrows suggestion behavior and conflicts with the user's requirement to avoid coverage/semantics shortcuts.
- PCH-related flags are not relevant unless the iCTS build actually uses a PCH/prefix-header model.

### IWYU's purpose points to dependency hygiene as the long-term fix

The IWYU man page says the tool's main goal is to remove superfluous includes and replace includes with forward declarations where possible.

Task implication:

- This aligns with the local slow-file data: the worst IWYU units are not simply the largest source files; they are STA/SDC adapter files with heavy include graphs and broad external dependencies.
- Long-term runtime improvement should include dependency hygiene: smaller public headers, less transitive dependency exposure, forward declarations where safe, and clearer adapter boundaries.
- This is product-code refactoring risk and should be planned separately from the checker scheduling change.

## Community Signals

Community discussions are less authoritative than upstream docs, but they align with the official findings:

- Developers commonly report `clang-tidy` slowness from expensive included headers and third-party/system headers.
- Common speedups mentioned are adding cores/memory, disabling expensive checks, caching, and running only changed files.
- `clang-analyzer-*` checks are often called out as expensive.
- clang-tidy cache wrappers such as `ctcache`, `clang-tidy-cache`, or `cltcache` are discussed as possible repeated-run accelerators.

Task implication:

- "More parallelism" and "profile expensive checks" are consistent with official guidance and fit this task.
- Disabling analyzer checks or running only changed files would reduce validation semantics and should not be used for this task.
- Caching may be worth a future experiment, but it needs careful cache keys: tool version, `.clang-tidy`, compile flags, preprocessor output, and relevant environment inputs. It should not be introduced as the first fix.

## Method Matrix

| Method | Source support | Coverage impact | Fit for this task |
| --- | --- | --- | --- |
| Parallelize `tidy-headers` with existing `jobs` | Strong official support via `run-clang-tidy.py -j` | None | First implementation priority |
| Add better phase/unit runtime attribution | Supported by local need and clang-tidy profiling | None | High value, observability |
| Use `--enable-check-profile` / `--store-check-profile` in an opt-in diagnostic mode | Official clang-tidy support | None | Good follow-up/optional |
| Use parameter files for large clang-tidy commands | Official clang-tidy support | None | Secondary robustness/perf candidate |
| Add IWYU load-aware throttling | Official `iwyu_tool.py -l` precedent | None | Possible if resource pressure is observed |
| Improve include/dependency hygiene in STA/SDC adapters | Supported by IWYU's purpose | None if refactored carefully | Larger follow-up slice |
| Add IWYU mapping files | Official IWYU support | Usually none, but changes suggestions | Follow-up correctness/noise work |
| Use clang-tidy diff/line filtering | Officially documented, but not faster | Reduced reporting coverage | Not a runtime solution |
| Use `header-filter` / `exclude-header-filter` as speed fix | Officially diagnostic filters | Reduced emitted diagnostics | Not a parse/runtime fix |
| Disable expensive tidy checks | Community-supported speed lever | Reduces check coverage | Out of scope unless separately approved |
| Skip slow files or use CMake `SKIP_LINTING` | CMake supports skip | Reduces file coverage | Out of scope |
| Use IWYU `--transitive_includes_only` | Official option | Changes suggestion semantics | Out of scope by default |
| Add clang-tidy cache wrapper | Community-supported experiment | None if cache is correct | Later experiment, not first fix |

## Recommended Planning Conclusion

The researched material supports a conservative implementation path:

1. First, parallelize `tidy-headers` using `_parallel_map()` and `jobs`, preserving the same commands, parsing, findings, notes, and suppression behavior.
2. Keep IWYU enabled. It is already parallelized, so do not attempt to "speed it up" by skipping files or reducing scope.
3. Improve observability enough to compare phases and top units after the change.
4. Treat IWYU runtime as a dependency-shape problem after the checker scheduling fix. The STA/SDC adapter hotspots should be investigated for include hygiene and boundary cleanup as a separate product-code refactor.
5. Consider opt-in profiling (`--enable-check-profile` / JSON profile storage) and possibly parameter-file command construction after the first measurable improvement.

