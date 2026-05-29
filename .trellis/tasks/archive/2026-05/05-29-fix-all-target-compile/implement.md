# Implementation Plan

## Checklist

- [x] Run `cmake --build build --target all -j 8` and capture the first failure.
- [x] Inspect the owning source/CMake files for the failed target.
- [x] Apply minimal fixes for compile/link correctness.
- [x] Re-run the failed target or narrowed build command.
- [x] Re-run `cmake --build build --target all -j 8`.
- [x] Run any small focused tests/builds relevant to touched code.
- [ ] Summarize fixed files and validation output.

## Validation Commands

```bash
cmake --build build --target all -j 8
```

Additional focused commands will be selected after the failing target is known.

Observed failing target:

```text
icts_test_flow_synthesis
```

Root cause:

```text
iCTS test link line pulled platform/idm/tool_manager dependencies, including tool_api_ipw and iPA power. The iPA power static library used iSTA and libfort symbols but did not declare those link dependencies on the power target, so consumers could see power after sta/ista-engine/fort and fail static-library symbol resolution.
```

Applied fix:

```text
src/operation/iPA/api/CMakeLists.txt links power with ista-engine, sta, and fort.
```

Completed validation:

```bash
cmake --build build --target icts_test_flow_synthesis -j 8
cmake --build build --target all -j 8
cmake --build build --target power iPower iPowerTest ipower_cpp icts_test_flow_synthesis -j 8
ctest --test-dir build -R '^icts_test_flow_synthesis$' --output-on-failure
```

Skipped validation:

```text
src/operation/iCTS ecc_dev_tools checker was intentionally stopped/skipped because the source fix does not modify iCTS code.
```

## Risk Points

- `all` includes many targets beyond iCTS, so the first visible failure may not be in CTS.
- Top-level CMake dependencies can make an apparently local include/link change affect multiple tools.
- The build tree may contain stale generated files; if failure evidence points at configuration drift, regenerate before editing source.
