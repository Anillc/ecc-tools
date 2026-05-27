# Research notes

## Local Code Observations

### H-tree flow

`HTree.cc` is currently the central orchestration point. It does common
preparation, discrete frontier/depth search, analytical dispatch, discrete selection,
and final embedding/reporting.

The analytical path is called before discrete depth search through:

```text
htree::analytical_solution::SelectAnalyticalHTreeSolution(...)
```

If analytical succeeds or fails, it returns `true`, so the discrete path is not
used as a fallback when analytical mode is enabled.

### Duplication

Discrete and analytical both perform these finalization steps:

- transfer selected evaluation into `HTree::Build`,
- materialize `best_pattern`,
- call `ApplySelectedPatternToLevelPlans`,
- validate root-driver sizing,
- call `BuildEmbedding`,
- apply root-driver sizing,
- call `LogSynthesisSummary`,
- finish/fail the H-tree build stage.

This should become shared code.

### Analytical model boundary

The mathematical model is already separated reasonably well:

```text
analytical_solver/model/MathHtreeModel.hh
analytical_solver/model/MathHtreeProblemBuilder.cc
analytical_solver/solver/ScipMathHtreeSolver.cc
```

The weak point is backend adaptation: `ScipMathHtreeSolver` both emits LP,
manages process execution, resolves developer-local paths, parses SCIP files,
and maps status. That should be split into model serialization, process/backend
adapter, and status mapping.

### Existing third-party state

`src/third_party` already contains LEMON, Abseil, yaml-cpp, pybind11, SALT, and
other dependencies. `src/third_party/CMakeLists.txt` unconditionally adds many
third-party subdirectories and applies `add_compile_options(-w)`.

LEMON includes LP/MIP wrapper headers such as `glpk.h`, `cbc.h`, and `lp_base.h`,
but `src/third_party/lemon/config.h` has:

```text
LEMON_HAVE_LP undefined
LEMON_HAVE_MIP undefined
LEMON_HAVE_GLPK undefined
LEMON_HAVE_CBC undefined
```

Therefore LEMON is not an available MILP backend in the current build.

## Local OR-Tools Package

Package:

```text
/home/liweiguo/download/or-tools_x86_64_Ubuntu-20.04_cpp_v9.14.6206
```

Available libraries include:

- `libortools.so.9.14.6206`
- `libscip.so.9.2.2.0`
- `libhighs.so.1.11.0`
- CBC/CLP/OSI/CGL libraries
- many Abseil shared libraries
- Protobuf, RE2, Boost, SoPlex, zlib/bzip2

The package contains CMake configs for OR-Tools, CBC, CLP, CoinUtils, Osi,
Abseil, ZLIB, and others.

The local `ortoolsConfig.cmake` imports:

```text
ZLIB, BZip2, absl, Protobuf, re2, Clp, Cbc, highs, Eigen3, SCIP
```

The imported `ortools::ortools` target links:

```text
absl::* targets
protobuf::libprotobuf
re2::re2
Coin::CbcSolver
Coin::OsiCbc
Coin::ClpSolver
Coin::OsiClp
highs::highs
Eigen3::Eigen
SCIP::libscip
Threads::Threads
```

This confirms the prior concern: direct OR-Tools linking is broad and likely to
interact poorly with the repository's existing `src/third_party/abseil-cpp`.

## Official Solver Capability Notes

### OR-Tools

Official OR-Tools MIP documentation shows C++ `MPSolver` use with a SCIP backend
and states that SCIP is recommended for mixed integer problems with both integer
and continuous variables:

- https://developers.google.com/optimization/mip/mip_example
- https://developers.google.com/optimization/install/cpp/binary_linux

Local interpretation: OR-Tools is capable, but as a project dependency it is
heavy. The direct CMake target imports multiple solver and support stacks.

### HiGHS

Official HiGHS documentation says HiGHS supports large-scale sparse LP, MIP, and
QP; is C++11; is MIT-licensed; source installation requires CMake 3.15 and no
other third-party utilities; and provides a C++ library API through `HighsModel`
and `HighsLp`:

- https://highs.dev/
- https://ergo-code.github.io/HiGHS/stable/interfaces/cpp/library/

Local interpretation: HiGHS is the strongest first in-process candidate because
it is narrow, permissive, and already appears inside the local OR-Tools package.

### SCIP

Official SCIP documentation states that SCIP is Apache 2.0 since version 8.0.3,
has C/C++ APIs, and supports static model building, iterative modification,
solution-pool querying, and parameter setting:

- https://scipopt.org/

Local interpretation: direct SCIP is technically strong and closer to current
runtime behavior than HiGHS, but still heavier than HiGHS because SCIP/SoPlex and
optional dependencies must be managed cleanly.

### CBC

Official CBC documentation describes CBC as an open-source C++ MIP solver and
callable library, but notes that it relies on other COIN-OR components such as
OSI, CLP, and CGL:

- https://coin-or.github.io/Cbc/intro.html

Local interpretation: CBC is viable but not as clean as HiGHS for a first
in-process backend.

## Initial Decision Matrix

| Candidate | Strength | Integration Risk | Recommendation |
| --- | --- | --- | --- |
| External SCIP executable | Already implemented, strong MILP | Runtime packaging/path management | Keep as compatibility backend |
| HiGHS C++ API | Small, MIT, CMake, LP/MIP/QP | Need compare status/gap/runtime vs SCIP | First in-process spike |
| Direct SCIP C API | Strong MILP, closest to current solver | More dependencies and API complexity | Second in-process spike |
| OR-Tools C++ | High-level MPSolver, many backends | Heavy Abseil/Protobuf/Coin/HiGHS/SCIP graph | Defer/reject direct submodule |
| CBC / COIN-OR | Mature open-source MIP | Multiple COIN libs and build wiring | Fallback candidate |
| Existing LEMON | Already in repo | No backend enabled; wrapper only | Reject as backend |
| GLPK | Light and common | License/runtime concerns | Reject unless explicitly approved |

## Recommended Next Step

Use the H-tree architecture refactor first, but keep the current external SCIP
backend behavior stable. Then add a tiny HiGHS in-process backend spike behind
the new facade. Compare HiGHS and external SCIP on a micro MILP and on the
current analytical H-tree model before replacing production default behavior.

## HiGHS Vendoring Result

The user accepted direct source vendoring so analytical H-tree can compile
without requiring a local solver installation. HiGHS was cloned under:

```text
src/third_party/highs
```

Version:

```text
HiGHS v1.11.0, commit 364c83a51e44ba6c27def9c8fc1a49b1daf5ad5c
```

The nested `.git` directory was removed so the parent repository can track the
vendored source directly rather than treating it as an embedded git repository.

The integration uses a scoped helper in `src/third_party/CMakeLists.txt` and
sets HiGHS options before `add_subdirectory(highs EXCLUDE_FROM_ALL)`:

- `FAST_BUILD ON`
- `BUILD_CXX ON`
- `BUILD_CXX_EXE OFF`
- `BUILD_TESTING OFF`
- `BUILD_EXAMPLES OFF`
- `BUILD_SHARED_LIBS OFF`
- `FORTRAN OFF`
- `CSHARP OFF`
- `PYTHON_BUILD_SETUP OFF`
- `ZLIB OFF`
- `CUPDLP_GPU OFF`
- `HIGHS_NO_DEFAULT_THREADS ON`

This produces the `highs` static library target and does not build the HiGHS
CLI, examples, tests, Python, CSharp, Fortran, CUDA, or zlib paths.

Implementation note: the first direct `HighsModel` builder attempt incorrectly
used the default `HighsSparseMatrix::start_` vector without clearing it. HiGHS
initializes that vector to `{0}`; appending another leading zero shifted all
matrix columns by one and made the analytical micro-model infeasible. The fixed
builder clears `start_`, `index_`, `value_`, and `p_end_` before writing the
column-wise sparse matrix.

The default and only production `MathHtreeMilpSolver` backend is now HiGHS
in-process. The temporary external SCIP backend was used for A/B comparison and
then removed.

## Implementation Notes From First Refactor Pass

The first implementation pass chose the smaller H-tree-local backend facade
instead of a generic `source/module/optimization/milp` module. The reason is
blast-radius control: the current model is still H-tree-specific, and a generic
MILP module would require stabilizing a broader model schema before the discrete
/ analytical H-tree architecture is settled.

Current code shape:

```text
HTree.cc
  -> analytical_solution::SelectAnalyticalHTreeSolution(...)
  -> discrete_solution::SelectDiscreteHTreeSolution(...)
  -> FinalizeSelectedHTreeSolution(...)
```

Both selection engines now return `HTreeSelectedSolution`, and shared
finalization owns selected-pattern application, root-driver sizing, embedding,
and synthesis summary reporting.

The solver facade is:

```text
analytical_solver/solver/MathHtreeMilpSolver.hh/.cc
  -> HiGHS in-process default backend
```

The production code no longer contains OR-Tools/SCIP runtime configuration or
backend-selection environment handling.
