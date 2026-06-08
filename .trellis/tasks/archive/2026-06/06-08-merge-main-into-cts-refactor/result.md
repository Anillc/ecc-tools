# Merge Result

## Commands Run

```bash
git fetch origin
git branch -f main origin/main
GIT_MERGE_AUTOEDIT=no git merge --no-ff main
```

## Branch Update

- Current branch before merge: `cts_refactor`
- Local `main` updated from `0c49265bf` to `d54ba9398`, matching latest fetched `origin/main`.

## Merge Status

The merge from `main` into `cts_refactor` has conflicts and stopped before creating a merge commit.

Conflicted files:

- `.gitignore`
- `src/interface/python/py_imp/idb_to_imp_db/PyPlaceDB.cpp`
- `src/interface/python/py_imp/idb_to_imp_db/PyPlaceDBRoutbility.cpp`
- `src/interface/python/py_imp/idb_to_imp_db/PyPlaceDBTiming.cpp`
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/api/CTSAPI.hh`
- `src/operation/iCTS/source/database/adapter/sdc/clock_parser/SdcClockCommands.cc`
- `src/operation/iCTS/source/database/adapter/sdc/clock_parser/SdcClockValue.cc`
- `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTraceResolve.cc`
- `src/operation/iCTS/source/database/config/Config.cc`
- `src/operation/iCTS/source/database/io/WrapperClockReader.cc`
- `src/operation/iCTS/source/flow/Flow.cc`
- `src/operation/iCTS/source/flow/Flow.hh`
- `src/operation/iCTS/test/database/CMakeLists.txt`
- `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc`
- `src/operation/iCTS/test/flow/FlowTest.cc`

## Current State

The initial merge was intentionally left in the conflicted state for inspection. The conflicts were then resolved with the following
selection rules:

- `.gitignore`: keep the current branch (`cts_refactor`) version.
- `src/operation/iCTS/**`: keep the current branch (`cts_refactor`) version.
- Other conflicted files: keep the incoming `main` version.

Resolved non-iCTS conflict files with the `main` version:

- `src/interface/python/py_imp/idb_to_imp_db/PyPlaceDB.cpp`
- `src/interface/python/py_imp/idb_to_imp_db/PyPlaceDBRoutbility.cpp`
- `src/interface/python/py_imp/idb_to_imp_db/PyPlaceDBTiming.cpp`

Post-resolution checks:

```bash
git diff --name-only --diff-filter=U
git ls-files -u
rg -n '<<<<<<<|=======|>>>>>>>' .gitignore src/interface/python/py_imp/idb_to_imp_db src/operation/iCTS
```

All three checks report no unresolved conflict entries or conflict markers. The merge is resolved in the index but not committed yet.

## Build Validation

Full build was run after resolving conflicts:

```bash
cmake --build build -j"$(nproc)"
```

The first full-build attempt exposed two incoming `main` iRCX compatibility issues in the existing GCC 10 build environment:

- `src/operation/iRCX/source/utils/StringUtils.hh` used floating-point `std::from_chars`, which is not available in libstdc++ 10.
- `src/operation/iRCX/source/utils/StageLog.hh` used `std::source_location`, which is not available in this compiler/library setup.

Temporary local compatibility edits were tested, but they were reverted after confirming the machine has GCC 11. The resolved merge now keeps iRCX
code identical to `main`.

A separate GCC 11 build directory was configured to avoid changing the existing GCC 10 `build/` cache:

```bash
cmake -S . -B build-gcc11-merge -G Ninja \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_EXTRA_UNIT_TESTS=OFF \
  -DBUILD_PYTHON=ON \
  -DBUILD_TESTING=ON \
  -DENABLE_AI=OFF \
  -DUSE_DOTNET_STD_21=ON \
  -DUSE_GPU=OFF \
  -DUSE_PROFILER=OFF
cmake --build build-gcc11-merge -j"$(nproc)"
```

The GCC 11 full build passed. It emitted existing warnings, including linker warnings about `libunwind.so.8` possibly conflicting with
`libunwind.so.1`, but no compile or link errors.

Compiler default status:

- `/usr/bin/gcc` points to `gcc-11`.
- `/usr/bin/g++` points to `g++-11`.
- `sudo update-alternatives` could not be modified from this session because sudo requires a password.
- `gcc/g++` are not currently registered as alternatives on this host, while `cc/c++` already resolve through `gcc/g++`.

The newer iSTA path-depth regression tests also passed under the GCC 11 build output:

```bash
./bin/iSTATest --gtest_filter=StaPathDepthTest.*
```

Result: 2 tests passed. Runtime emitted a conda `libcurl.so.4` version-info warning and existing test logging, but no test failure.

iCTS quality validation was also run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result: `src/operation/iCTS` reported 0 in-scope findings. The checker reported existing out-of-scope diagnostics from external headers/modules.

To abandon the merge:

```bash
git merge --abort
```

To finish the merge after resolving conflicts:

```bash
git add <resolved-files>
git commit
```

## iSTA Difference Review

Current resolved iSTA content is identical to `main`.

The iSTA difference between the old `cts_refactor` branch tip and `main` is from newer upstream iSTA commits, not from manual conflict resolution:

- `StaCheck.cc` and several iSTA test build fixes were already changed on old `cts_refactor`.
- Newer `main` also includes path-depth/BFS changes from commits such as `5861100ea`, `c060d9095`, and `6a2cdcfc3`.
- The resolved merge therefore takes the newer `main` iSTA code, including:
  - default STA propagation method changed from DFS to BFS;
  - per-propagation BFS queues and duplicate suppression;
  - memoized `StaVertex::getPathDepth`;
  - ideal-clock path-depth lookup avoidance;
  - cycle-only path-depth handling and `StaPathDepthTest`.

Because iCTS uses its own `FastSTA` adapter and not the global `Sta::updateTiming()` propagation switch directly, the visible iCTS interaction risk is
limited to shared STA data structures and timing assumptions. The GCC 11 full build confirms the combined C++ code compiles with the newer iSTA state;
runtime regression still needs CTS case execution if timing behavior must be validated.
