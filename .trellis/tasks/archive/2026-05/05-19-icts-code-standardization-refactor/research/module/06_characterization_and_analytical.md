# Sub-Module: `characterization/` and `analytical_characterization/`

- **Query**: characterization 子模块结构、职责、命名
- **Scope**: internal
- **Date**: 2026-05-19

## `characterization/` — Flat Directory, One Mega-Class

`module/characterization/` has **no sub-directories**. All 20 files
(12 .cc + 7 .hh + 1 CMakeLists) live at the root level.

```
characterization/
├── CMakeLists.txt              (single target `icts_source_module_characterization`)
├── CharBuilder.hh              (231 lines, class declaration)
├── CharBuilder.cc              (29 lines, anchor TU — empty namespace)
├── CharBuilderBuild.cc         (top-level CharBuilder::build with helpers)
├── CharBuilderCircuit.cc
├── CharBuilderConfig.cc        (21152 bytes, largest)
├── CharBuilderFeasibility.cc
├── CharBuilderPatternEnumeration.cc
├── CharBuilderPatternStorage.cc
├── CharBuilderSampleStorage.cc
├── CharBuilderSampling.cc
├── CharBuilderSlewSampling.cc
├── CharBuilderStaSampling.cc
├── CharBuilderTopology.cc
├── Frontier.hh                 (320 lines, template helpers)
├── HashJoinEngine.hh           (207 lines)
├── HTreeTopologyCharTable.hh   (97 lines)
├── HTreeTraits.hh              (73 lines)
├── PatternCombiner.hh          (72 lines)
├── SegmentCharTable.hh         (96 lines)
└── SegmentTraits.hh            (64 lines)
```

### Role in CTS Flow

The sub-module characterizes wire **segments** and **buffering patterns** by
enumerating topology bits, sampling input slew × load cap pairs, and running
STA via `CTSAPI` to record delay/power.

### Public Class

| File / Line                              | Symbol                                                                                       |
|------------------------------------------|----------------------------------------------------------------------------------------------|
| `characterization/CharBuilder.hh:41`     | `struct CharBufferInfo` (sorted-buffer info)                                                 |
| `characterization/CharBuilder.hh:57`     | `class CharBuilder` (instance class, mutable state, lifecycle: `init(opts) → build()`)        |
| `characterization/Frontier.hh:42-300`    | `enum class TerminalSemantic`, `struct PatternCompositionState`, `struct SegmentFrontierStateKey`, `struct HTreeFrontierStateKey`, `class StateFrontierPruner`, template helpers `MakeSegmentStateFrontierPruner` / `MakeHTreeStateFrontierPruner` / `BuildSegmentStateFrontier` / `BuildHTreeStateFrontier` |
| `characterization/HashJoinEngine.hh:37`  | `namespace icts::detail` with templated `HashJoinConcat`, `NullPruner`                       |
| `characterization/SegmentCharTable.hh:39`| `class SegmentCharTable` (header-only)                                                       |
| `characterization/SegmentTraits.hh:38`   | `struct SegmentTraits`                                                                       |
| `characterization/HTreeTopologyCharTable.hh:39` | `class HTreeTopologyCharTable` (header-only)                                          |
| `characterization/HTreeTraits.hh:41`     | `struct HTreeTraits`                                                                         |
| `characterization/PatternCombiner.hh:37/55` | `class SegmentPatternCombiner`, `class TopologyPatternCombiner`                          |

### Implementation Scattering

`class CharBuilder` (one class) is implemented across **12 .cc files**, each
hosting 0–7 member methods:

| File                                  | `CharBuilder::` method count |
|---------------------------------------|-----------------------------:|
| `CharBuilder.cc`                      | 0 (empty anchor TU)          |
| `CharBuilderBuild.cc`                 | 1 (`build()`) + many helpers |
| `CharBuilderCircuit.cc`               | 3                            |
| `CharBuilderConfig.cc`                | 7                            |
| `CharBuilderFeasibility.cc`           | 2                            |
| `CharBuilderPatternEnumeration.cc`    | 7                            |
| `CharBuilderPatternStorage.cc`        | 1                            |
| `CharBuilderSampleStorage.cc`         | 1                            |
| `CharBuilderSampling.cc`              | 1                            |
| `CharBuilderSlewSampling.cc`          | 1                            |
| `CharBuilderStaSampling.cc`           | 1                            |
| `CharBuilderTopology.cc`              | 1                            |

**Total: 26 `CharBuilder::xxx` definitions** distributed over 11 files; one
file (`CharBuilder.cc`) exists only as a translation unit anchor and is
empty inside `namespace icts {}`.

### Private Header Footprint

`CharBuilder.hh:113-228` exposes the full mutable state of the builder
plus 6 private nested structs (`BuildProgress`, `TopologyBits`,
`TopologyDesc`, `StoredSampleIndices`, `PatternFeasibility`), 18 private
methods, and 24 private data members in the public header. Pimpl is not
used.

### Naming Issues

| Element                                                  | Issue                                                                          |
|----------------------------------------------------------|--------------------------------------------------------------------------------|
| `CharBuilder` — 1 class with 11 split .cc files          | "Builder" used as catch-all; pattern of `CharBuilderXxx.cc` is split-file convention, not natural class decomposition |
| `Frontier.hh`                                            | "Frontier" is a generic term from search/Pareto literature; CTS-semantic is `ParetoFront` or `EfficiencyFrontier` |
| `HashJoinEngine.hh`                                      | Generic DB / batch-processing term; "Engine" suffix used for what is essentially a templated free function |
| `SegmentCharTable`, `HTreeTopologyCharTable`             | "Table" used for value-objects; might be `SegmentCharCatalog`, `HTreeTopologyCharCatalog` |
| `SegmentTraits`, `HTreeTraits`                           | "Traits" is OK in C++ template usage                                            |
| `PatternCombiner`, `SegmentPatternCombiner`, `TopologyPatternCombiner` | OK in CTS context                                                  |
| `MonotonicBoundaryState` (referenced from `Frontier.hh`) | "State" + "Boundary" both generic                                              |
| `BuildProgress` (in `CharBuilder.hh`)                    | Generic; could be `CharBuildStats` or `BufferCharRunMetrics`                   |
| `TopologyDesc` (in `CharBuilder.hh`)                     | "Desc" abbreviation; could be `BufferingPlan` or `SegmentPlan`                  |
| `CharBuilderStaSampling.cc`, `CharBuilderSlewSampling.cc`, `CharBuilderSampleStorage.cc` | Filenames stitch class name + topic ("split-file" convention) |
| File pair `Frontier.hh` + helper template functions      | No `Frontier.cc`; functions are all `inline` / `template` in header             |

## `analytical_characterization/` — Flat, 3 Files

`module/analytical_characterization/` is also flat. Three .cc/.hh pairs in
`namespace icts::analytical`.

```
analytical_characterization/
├── AnalyticalCharacterization.hh / .cc   (top-level facade)
├── AnalyticalFit.hh / .cc                (least-squares fit)
└── AnalyticalModel.hh / .cc              (analytical surface model + structural cap operator)
```

### Role in CTS Flow

Builds an *analytical* (closed-form) surface model from the discrete
samples produced by `CharBuilder`, then enables fast `evaluate(slew, cap)`
queries at flow time.

### Public Symbols

| File / Line                                                | Symbol                                                                                                            |
|------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| `analytical_characterization/AnalyticalModel.hh:36/44/50/63/79/96/116/124/129/142` | `enum AnalyticalMetric`, `enum AnalyticalModelBasis`, `struct AnalyticalDomain`, `struct AnalyticalFitQuality`, `struct AnalyticalSurfaceModel`, `struct StructuralCapOperator`, `struct AnalyticalModelKey`, `struct AnalyticalModelKeyHash`, `struct AnalyticalModelSet`, `class AnalyticalModelCatalog`. |
| `analytical_characterization/AnalyticalFit.hh:34/41/55/62`  | `struct AnalyticalFitSample`, `struct AnalyticalFitOptions`, `struct AnalyticalFitResult`, `FitAnalyticalSurface()` free fn. |
| `analytical_characterization/AnalyticalCharacterization.hh:43/65/72/82/92` | `struct AnalyticalCharacterizationOptions`, `struct AnalyticalCharacterizationFailure`, `struct AnalyticalCharacterizationResult`, `class AnalyticalCharacterization` (static), `BuildBucketCompatibleStructuralCapOperator()` free fn. |

### Why It Is Separated From `characterization/`

`analytical_characterization` depends on `characterization`
(`AnalyticalCharacterization.cc:37` includes `CharBuilder.hh`) but the
reverse is not true. The CMake target `_module_analytical_characterization`
PRIVATE-links `_module_characterization`. The split is reasonable: the
sample producer (`CharBuilder`) is decoupled from the model fitter
(`AnalyticalCharacterization`).

### Naming Issues

| Element                                              | Issue                                                                                                |
|------------------------------------------------------|------------------------------------------------------------------------------------------------------|
| `AnalyticalCharacterization` vs. `characterization/` | Two top-level sub-modules with overlapping names; might consolidate as `characterization/analytical/` |
| `StructuralCapOperator`                              | OK; "operator" applies to closed-form transform                                                     |
| `BuildBucketCompatibleStructuralCapOperator()`       | 53-char free fn name; could be a static factory `StructuralCapOperator::fromBuckets(...)`            |
| `AnalyticalModelKey`, `AnalyticalModelKeyHash`       | Standard hash-key pair                                                                              |
| `AnalyticalSurfaceModel`                             | Could be `SurfaceModel` if namespaced as `icts::analytical::SurfaceModel`                            |

## Caveats / Not Found

- `characterization/` is the only sub-module in `module/` that bundles
  template-only header utilities (`HashJoinEngine.hh`,
  `SegmentCharTable.hh`, `HTreeTopologyCharTable.hh`, `Frontier.hh`,
  `*Traits.hh`) alongside its instance class — the directory effectively
  doubles as a private utility library.
- `analytical_characterization` is **not linked** into
  `icts_source_module` INTERFACE target (see
  `01_top_level_cmake_and_architecture.md`).
