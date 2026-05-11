# P6 Root Compensation Full-Signature Grouping

Date: 2026-05-09

> Superseded on 2026-05-09 by `rollback_to_opt3_report.md`. This file is a historical implementation/benchmark report. The P6 code and focused tests described below were removed when production H-tree code was restored to `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`. P6 is not default-enabled after the rollback; its measured fixture impact was treated as negligible/low-impact relative to opt3.

## Historical Decision

P6 was implemented as a default-enabled exact optimization in `RootDriverCompensationPass::apply` during the post-opt3 experiment. That code is no longer present in production source after the rollback to the opt3 backup ref.

The change is not an analyzer/prototype gate. It does not change the root-load estimator, root-driver cell master choice, direct Liberty delay/power query inputs, candidate selection objective, or selected-pattern reporting path.

## Implementation

Files changed:

- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.hh`

The implementation adds a grouped apply path:

1. Precompute per-entry root compensation metadata once per `apply()` call:
   - root-driver cell master, using the same first root-side non-empty `cell_masters.back()` logic as the previous per-entry path;
   - root-load signature, using the existing opt3 root-prefix signature semantics.
2. Build a full compensation group key:
   - root-driver cell master;
   - root compensation input slew;
   - root compensation clock period;
   - full root-load signature identity.
3. For each group:
   - resolve the root-load estimate once through the existing root-load signature cache;
   - build the existing direct-cost cache key from the resolved load estimate:
     - cell master;
     - input slew;
     - load bucket;
     - exact load capacitance estimate;
     - clock period;
   - call the existing `QueryRootDriverCompensation` once;
   - apply the returned cell delay/power to every grouped entry.

`evaluate()` remains per-pattern and still returns the complete `RootDriverCompensationDetail` for the selected entry. This preserves reporting/evaluation fields such as route estimator, load source, routed wirelength, terminal counts, and root-load capacitance.

## Exactness

The group key is intentionally split into two levels:

- The grouping key uses stable inputs available before load resolution: cell master, input slew, clock period, and root-load identity.
- The direct-cost cache key remains unchanged and includes the resolved load estimate fields that affect delay/power: load bucket and exact total load capacitance.

This means grouping cannot merge entries with different root-side topology load identities. After resolving the load estimate, the existing direct lookup key still guards the actual Liberty cost query.

The root-load signature includes the root-to-leaf segment-pattern prefix up to and including the first real buffer. If no real buffer exists, it includes the full segment-pattern sequence. That matches the root-load estimator's traversal boundary:

- levels downstream of the first root-side real buffer do not affect the root driver load;
- no-buffer paths need the full sequence because external load terminals are reached at the active leaf boundary.

The root-driver cell master and root-load signature use their original predicates:

- cell master resolution checks for non-empty `cell_masters`;
- real root-load buffer termination checks both buffer positions and cell masters.

Keeping these predicates distinct avoids changing behavior for inconsistent or partial segment patterns.

## Complexity Change

Before P6, `apply()` did the expensive front-end of compensation once per entry:

```text
O(E * (topology materialize + root-load signature build + root-driver master scan + load-cache lookup + cost-cache lookup))
```

After P6, each entry still contributes one metadata scan, but load resolution and direct-cost lookup run once per unique full compensation group:

```text
O(E * metadata scan + G * (load-cache lookup + cost-cache lookup + apply-to-group))
```

where `E` is the number of frontier entries and `G` is the number of full compensation groups. For repeated root-side signatures, this removes repeated root-load cache probes and direct-cost cache probes while preserving the exact cost assigned to every entry.

The existing opt3 root-load signature cache is still responsible for avoiding repeated physical root-load route estimates across candidate builds. P6 sits above it and avoids repeated per-entry query work inside one `apply()` call.

## Statistics

Added stats fields:

- `compensation_group_count`: number of full compensation groups processed by grouped `apply()`;
- `compensation_group_reuse_count`: number of entries covered by group reuse beyond one representative per group.

Existing stats retain their original meaning:

- `compensated_candidate_count` still counts entries that receive compensation;
- `load_resolution_count` counts real load resolutions, not grouped entries;
- `load_resolution_cache_hit_count` counts actual root-load cache hits;
- `unique_direct_lookup_count` counts real STA direct-cost queries;
- `cache_hit_count` counts actual direct-cost cache hits.

P6 does not inflate cache-hit counters for grouped entries skipped before cache lookup. This keeps runtime reports from implying map lookups that were intentionally avoided.

## Validation

Commands run:

```bash
cmake --build build --target icts_test_flow_synthesis_htree -j $(nproc)
./bin/icts_test_flow_synthesis_htree
git diff --check
cmake --build build --target iEDA -j $(nproc)
```

Results:

- `icts_test_flow_synthesis_htree` built successfully.
- `./bin/icts_test_flow_synthesis_htree` passed `13/13` tests.
- `git diff --check` passed.
- `iEDA` built successfully at `scripts/design/ics55_dev/iEDA`.

No dedicated small fixture was added because the production compensation path depends on initialized STA/Wrapper/config state for wire capacitance, pin capacitance, Liberty cell queries, and DBU conversion. Adding a non-environment-coupled unit test would require an adapter seam that is larger than this P6 change. Existing H-tree tests were run, and the full production target was rebuilt.

## Historical Benchmark Status

The historical default-enabled P6 benchmark was run after implementation and validation:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifact directory:

```text
.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/opt5_p6_root_compensation_grouping/
```

Measured result:

| Metric | Value |
| --- | ---: |
| CTS synthesis elapsed | 31.637 s |
| CTS total elapsed | 47.599 s |
| External wall time | 68.94 s |
| Max RSS | 6177132 KB |

QoR stayed aligned with the previous historical default-enabled runs:

- selected depth: `5`;
- selected topology pattern id: `10297477`;
- selected level segment pattern ids: `522717,468375,28,24,6`;
- selected compensated metric: `0.4959 ns / 217.271 uW`;
- selected physical root load: `0.1428 pF`;
- final CTS buffer count: `360`;
- total clock network wirelength: `43151.203 um`;
- setup/hold WNS: `7.302 ns / 0.008 ns`.

## Risks

Residual risks are low and localized:

- The grouped `apply()` path stores entry pointers into the same `entries` vector and does not mutate the vector shape while applying compensation.
- The metadata cache is local to one `apply()` call because topology pattern ids are candidate-local.
- The cross-candidate root-load cache remains keyed by stable segment pattern signatures, preserving the existing opt3 behavior.
- Reporting still calls `evaluate()` for the selected pattern, so complete detail fields come from the selected entry, not from a grouped representative.

The main remaining follow-up is performance validation on `ics55_dev` or a multi-design matrix to quantify the runtime delta and confirm no selected-QoR drift.
