# Quality Guidelines

## Coding Bar
- Match existing C++ style: file banner, `namespace icts`, `.hh/.cc` pairing, and local `CMakeLists.txt` updates.
- Preserve the existing public CTS entrypoints before adding a parallel external service path.
- When slimming a God Object such as `CTSAPI` or `Router`, it is acceptable to extract a single-responsibility helper under `api/` or `source/module/`, but document the new ownership boundary and keep wrapper-facing behavior stable.
- Keep wrappers thin: interface code should delegate to `tool_manager` or `CTSAPI`, not rebuild CTS logic.
- Respect current ownership style. If you introduce `new`, make the long-lived owner explicit.
- Prefer target-scoped includes over relative includes, prefer forward declarations where possible, and keep file-local helpers inside `.cc` anonymous namespaces.
- New directories should land with a matching `CMakeLists.txt`, target, and `PUBLIC/PRIVATE` linkage boundary.

## Change Boundary
- Default write scope is `src/operation/iCTS/` and the nearest CTS-owned interface or config entrypoints required by the same change.
- Verification and cleanup scope also stays inside iCTS by default; do not broaden tidy/fix work into external modules.
- Do not modify non-iCTS code by default.
- Exception: non-iCTS code has a confirmed bug or is the minimal integration point required to make the CTS change compile, run, or expose a public interface.
- When an exception is used, record the touched non-iCTS paths and the reason, and repeat that disclosure during `/trellis:finish-work`.

## Git Boundary
- Treat git as read-only inspection during implementation: `git status`, `git diff`, `git log`, and `git show` are allowed.
- Do not run write-side git commands on behalf of the user, including `git add`, `git commit`, `git push`, `git rebase`, `git reset`, or `git checkout --`.
- Human review and commit remain mandatory even if code, tests, and docs are ready.

## Spec Boundary
- `update-spec` output must stay under `.trellis/`, primarily `.trellis/spec/backend/` for CTS backend rules.

## Verification
- Run `clang-format` on touched CTS files.
- Structural changes must build the nearest CTS targets after the move.
- Run `clang-tidy` on touched CTS TUs with `src/utility/.clang-tidy`; TU clean means real clean, not `// *LIN*`-style suppression.
- Add or update `gtest` cases under `src/operation/iCTS/test/` for algorithm changes.
- Manually exercise Tcl or Python bindings when interface signatures change.
- If a change normalizes a cross-layer call path, record the canonical path and smoke-test that exact wrapper route.
- Flow or report changes must be smoke-tested with:
  `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Report-output changes must confirm both terminal logging and `result/cts/cts.log` contain the expected summary body, not only the title.

## Examples
- `src/operation/iCTS/source/CMakeLists.txt`
- `src/operation/iCTS/test/TreeBuilderTest.cc`
- `src/interface/python/py_icts/py_register_icts.h`
- `src/interface/tcl/tcl_icts/tcl_register_cts.h`

## Avoid
- Duplicating tree-building, config, or wrapper logic in a new file.
- Mixing experimental debug output into production code paths.
- Adding CTS files without wiring them into the nearest build script.
- Quietly expanding a CTS task into unrelated modules without first proving a CTS-external bug or integration requirement.
