# Clean CTS static archive CMake links

## Goal

Remove explicit `*.a` archive path hacks from CTS CMake and replace them with clean target-based CMake dependencies.

## Requirements

- Search all `src/operation/iCTS/**/CMakeLists.txt` files for explicit static archive path references, including `${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/lib*.a`.
- Replace every in-scope archive path link with real CMake targets and correct `target_link_libraries()` visibility.
- Resolve any static link-order or circular dependency issue by improving CTS target structure or dependency ownership, not by adding another archive path hack.
- Keep the change behavior-neutral for CTS synthesis, H-tree native/discrete flow, analytical H-tree flow, and characterization.
- Do not modify user-facing CTS config, specs, or runtime behavior for this task.
- Do not commit this task when implementation is complete; report the result first.

## Non-Goals

- Do not redesign H-tree algorithms or solution selection.
- Do not introduce new external dependencies.
- Do not clean unrelated non-CTS CMake files.
- Do not use broad global linker workarounds unless local target-based dependency cleanup is not viable and the tradeoff is explicitly documented.

## Acceptance Criteria

- [ ] `rg -n "CMAKE_ARCHIVE_OUTPUT_DIRECTORY|\\.a\\b|libicts_.*\\.a" src/operation/iCTS -g 'CMakeLists.txt'` returns no CTS static archive path hacks.
- [ ] Focused H-tree/characterization CMake targets build successfully.
- [ ] `iEDA` target links successfully.
- [ ] Focused H-tree tests pass.
- [ ] Final `src/operation/iCTS` checker is run after implementation; in-scope findings must be clean or explicitly reported if blocked by pre-existing out-of-scope diagnostics.
- [ ] Implementation remains uncommitted for user review.

## Notes

- User requested this task immediately after committing and archiving `05-27-htree-architecture-decomposition`.
- This task exists because the previous H-tree architecture task intentionally left several explicit archive path hacks in place after local target-only attempts exposed existing static link-order/cycle issues.
