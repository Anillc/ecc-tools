# W6 baseline counts (CMake target consolidation)

> Captured: 2026-05-20 (W6 in/out counts in one document for review).

## Total add_library counts

```bash
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source | wc -l
# baseline: 105 (pre-W6)
# final:    51 (post-W6, including the 11 W4-locked fast_sta sub-targets)
```

| Region | Baseline | Final | Delta |
|---|---:|---:|---:|
| `flow/` | 54 | 16 | -38 |
| `database/` | 23 | 21 | -2 (qor, adapter wrapper) |
| `module/` | 22 | 9 | -13 |
| `utils/` | 5 | 5 | 0 |
| `source/` aggregator | 1 | 1 | 0 |
| **Total** | **105** | **52 (incl. comment-only adapter shell)** | **-54** |

The literal `grep -c '^[[:space:]]*add_library'` is 51; the 52nd shows up only
because `database/adapter/CMakeLists.txt` retains a comment block (the
`add_library` text appears inside a comment). The actual configured archive
count is 51.

## INTERFACE-only targets

```bash
grep -rEn '^[[:space:]]*add_library\(.*INTERFACE' src/operation/iCTS/source | wc -l
# baseline: 20
# final:    11
```

## Source-bearing archives in `lib/`

```bash
cd build && ninja -t targets all | grep -c '^lib/libicts_source'
# baseline: 86
# final:    40
```

## Per-area sub-counts

```bash
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source/flow/synthesis/htree         | wc -l  # 13 -> 3
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source/flow/optimization            | wc -l  # 9  -> 2
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source/flow/synthesis/topology      | wc -l  # 4  -> 0 (merged into synthesis)
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source/flow/synthesis/trace         | wc -l  # 5  -> 1
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source/flow/report                  | wc -l  # 13 -> 2 (report + report_visualization)
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source/module/routing               | wc -l  # 9  -> 2 (routing + routing_bst)
grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source/module/topology              | wc -l  # 7  -> 3 (topology + cluster_constraints + fast_clustering)
```

## CMakeLists.txt files

| Phase | CMakeLists.txt count |
|---|---:|
| Baseline | 105 |
| Final | 49 |

## Step-by-step consolidation log

| Step | Action | add_library after step |
|---|---|---:|
| baseline | (no edits) | 105 |
| W6a | `cmake/icts_targets.cmake` helpers added | 105 |
| W6f | `module/routing/database/` empty INTERFACE removed | 104 |
| W6b | `flow/synthesis/htree/` 10 subs collapsed -> htree / htree_solver / htree_topology | 94 |
| W6c | `flow/optimization/` 8 subs collapsed -> optimization + optimization_stages | 87 |
| W6d | `flow/synthesis/topology` + `flow/synthesis/trace` subs collapsed | 80 |
| W6e | `flow/report/visualization/` 8 subs + report subs collapsed | 69 |
| W6g | flow/instantiation + flow/evaluation small subs collapsed | 67 |
| W6g | flow/synthesis/distribution merged into synthesis facade | 66 |
| W6g | module/topology config/kmeans/mcf/clustering collapsed into topology | 62 |
| W6g | module/routing 6 small router subs collapsed; bst 3 subs collapsed | 54 |
| W6g | database INTERFACE wrappers (qor, adapter) removed | 52 |
| W6g | flow/synthesis/topology merged into synthesis facade | 51 |

## Verification

- `bash build.sh` -> `bin/iEDA` linked clean.
- `ninja iEDA -j 8` -> 0 errors, no new warnings.
- 10 representative test executables (`icts_test_database_adapter_fast_sta`,
  `icts_test_flow_synthesis_htree_analytical_solver`,
  `icts_test_common_visualization`, `icts_test_flow_synthesis`,
  `icts_test_module_routing`, ...) all link cleanly.
- No circular dependencies introduced (mergers preserved topological order:
  topology -> solver -> facade, adapter -> algorithm -> geometry, etc.).

## Distance from PRD ceiling

- PRD §6.1 A9 ceiling: **35** archives.
- Soft acceptable: 45.
- Final: 51 archives = 35 ceiling + 16; 16 = 11 locked fast_sta sub-targets
  (W4 deliverable) + 4 database INTERFACE wrappers (routing / timing /
  characterization / spatial whose include paths are consumed by many
  external sites) + 1 flow_interface base.

Excluding the 11 W4-locked fast_sta sub-targets, the iCTS source tree now
ships **40** archives, which is within 5 of the ideal 35-target target.
