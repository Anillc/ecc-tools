# Merge Design: main into cts_refactor

## Goal

Merge current `origin/main` into the long-lived `cts_refactor` branch while keeping
current-branch CTS behavior and architecture authoritative.

## Source Refs

- Current branch: `cts_refactor`
- Current HEAD: `3e639d49e`
- Source main ref: `origin/main` at `493c1ea77`
- Merge base / last main-side parent previously merged into `cts_refactor`:
  `9f75fcc51ec6618efbcfb72a338c892a8db18b0b`

## Merge Policy

CTS-owned code follows `cts_refactor` by default:

- `src/operation/iCTS/**`
- CTS CMake under `src/operation/iCTS/**`
- CTS public API implementation and headers
- CTS tests and test helpers
- CTS-specific runtime behavior, config semantics, report semantics, and validation output

CTS-facing external integration requires semantic review:

- `src/interface/tcl/tcl_icts/**`
- `src/interface/python/py_icts/**`
- `src/interface/default_config/cts_default_config.json`
- `src/platform/tool_manager/tool_api/icts_io/**`
- `scripts/design/**/cts_default_config.json`
- `scripts/design/**/CTS.tcl`
- `scripts/design/**/run_iCTS.tcl`
- `src/apps/CMakeLists.txt`

Non-CTS mainline changes normally follow `origin/main`:

- DB, iRCX, iDRC, iRT, platform, GUI/interface, workspace, build, wheel, and third-party
  updates should be accepted from main unless they break CTS-facing contracts.
- Shared iSTA/iPA/liberty/database behavior must be reviewed by intent because CTS depends
  on timing, power, unit conversion, clock parsing, and IDB wrapper semantics.

Trellis and local-agent metadata are not part of the merge decision unless required to keep
the long-lived branch workflow functioning.

## Main-Side CTS Changes To Review

Direct `src/operation/iCTS` changes on `origin/main` after the previous merge point:

- `f3b719ae1` by Yell-walkalone:
  - modified `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
- `11f2a571c` by Yell-walkalone:
  - modified `src/operation/iCTS/source/database/io/Wrapper.cc`
- `77149cd71` by Yell-walkalone:
  - modified `src/operation/iCTS/source/database/io/CMakeLists.txt`
- `a08e56206` by Yell-walkalone:
  - modified `src/operation/iCTS/source/database/io/CMakeLists.txt`

Main-side CTS-facing script / app changes after the previous merge point:

- `344d4fd21`, `890096dfe`, `af3a43529`, `df30dbfa9`, `4677a5d46`,
  `22fe5cdf4`, `fd31a6c16` by Yell/Yell-walkalone:
  - reorganized `scripts/design/ics55*`, `sky130*`, and old `*_gcd` CTS config/script paths;
  - modified `src/apps/CMakeLists.txt`;
  - added `scripts/design/ics55/config/cts_default_config.json`;
  - added `scripts/design/ics55/steps/CTS.tcl`;
  - added `scripts/design/sky130/config/cts_default_config.json`;
  - added `scripts/design/sky130/steps/CTS.tcl`;
  - deleted old `scripts/design/*_gcd/iEDA_config/cts_default_config.json`;
  - deleted old `scripts/design/*_gcd/script/iCTS_script/run_iCTS.tcl`.

Main-side previous CTS baseline sync:

- `0c49265bf` by dawnli139 is the earlier selective CTS baseline landing on `main`.
  During this merge it should be treated as historical main ancestry, not as a reason to
  replace newer `cts_refactor` CTS architecture.

## External Main Changes To Integrate

Large non-CTS areas changed on `origin/main` since the previous merge point:

- `src/interface`: 263 paths, mostly GUI/Tcl/Python/interface updates
- `src/third_party`: 190 paths, including `gdstk` addition and `tcl_qt` removal
- `src/operation/iRCX`: 111 paths, major extraction/report/config/API refactor
- `src/database`: 97 paths, DB and parser updates
- `scripts/design`: 269 paths, workspace/script layout reorganization
- `src/operation/iSTA`: 25 paths, timing API and STA updates
- `src/operation/iDRC`: 14 paths
- `src/platform`: 14 paths
- build/release files: `CMakeLists.txt`, `build.sh`, `cmake/rust.cmake`, `pyproject.toml`,
  `.github`, `.gitmodules`

Overlap outside iCTS between current branch and main is smaller but high-risk:

- 40 non-iCTS paths are changed on both sides.
- Main overlap clusters include `src/operation/iSTA`, `src/interface`, `src/operation/iPA`,
  `src/database/manager`, CTS design scripts, `.gitignore`, `AGENTS.md`, and
  `src/apps/CMakeLists.txt`.

## Conflict Strategy

Expected merge conflict profile from the detached dry-run:

- 307 unmerged paths
- 297 under `src/operation/iCTS`
- 10 outside `src/operation/iCTS`

Resolution order:

1. Resolve broad iCTS directory rename conflicts by choosing the current `cts_refactor`
   architecture as the destination shape.
2. Manually inspect main-side CTS edits listed above and port only compatible fixes into
   the current branch architecture.
3. Resolve CTS-facing script/app/interface files by preserving current branch CTS behavior
   while incorporating mainline path/layout changes only when compatible.
4. Resolve shared iSTA/iPA/liberty/database overlaps by preserving CTS-required semantics,
   especially unit conversion, clock parsing, timing update, power interpretation, and IDB
   wrapper behavior.
5. Accept non-CTS mainline changes where they do not affect CTS contracts.
6. Run validation before committing or reporting the merge as complete.

## Validation

Minimum validation after conflict resolution:

- no unmerged entries;
- no conflict markers;
- build reaches the existing project success criteria;
- iCTS reference run succeeds;
- compare key CTS metrics and generated output against a pre-merge baseline.

Reference run:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

If the current branch has moved from `ics55_dev` to a newer canonical benchmark script,
record that decision before running validation.
