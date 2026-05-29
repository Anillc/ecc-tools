# Design

## Boundary

This task is a build-correctness repair. The primary contract is that the current branch's configured CMake build tree can build the `all` target.

The first source of truth is the compiler/linker output from:

```bash
cmake --build build --target all -j 8
```

If the existing `build` tree is stale or inconsistent, regenerate it with the repository's existing CMake configuration pattern before judging the result.

## Approach

1. Reproduce the `all` target failure and capture the first actionable error.
2. Map each failure to the owning target and source/CMake file.
3. Prefer local source or CMake dependency fixes over broad target disabling.
4. Re-run the failed target after each fix, then re-run `all`.
5. If multiple unrelated failures appear, fix them in the order reported by the build, keeping each change minimal.

## Compatibility

- Do not change public CTS API behavior unless a compile signature mismatch requires it.
- Do not remove targets from the default build merely to pass `all`.
- Do not alter generated third-party code unless the error is caused by repository integration glue.

## Rollback

Each repair should be easy to revert independently. If a change touches shared CMake or public headers, validate at least the owning target and the full `all` target before completion.
