# Implementation Checklist

## Planning / Measurement

- [x] Run fresh baseline for the target command and store artifacts under `artifacts/fresh_baseline/`.
- [x] Parse top-level CTS runtime distribution from fresh `cts.log`.
- [x] Compare fresh baseline selected topology/QoR against the known opt3 result.
- [x] Inspect HTree/CharBuilder source to map runtime to concrete functions and loops.
- [x] Decide whether existing logs are enough or whether substage instrumentation is required.
- [x] Update `research/runtime-baseline.md` with fresh measurements and bottleneck attribution.

## Optimization Slice

- [x] Add schema-backed runtime metrics with low logging noise for synthesis subregions not currently measured.
- [x] Add HTree internal timing for segment frontier synthesis, topology depth search, global coverage/filtering, global Pareto selection, selected compensation detail, embedding, and summary/report emission.
- [x] Add topology-level timing for sink load preparation/clustering, downstream HTree build, sink-domain commit/layout, source-to-root trunk synthesis, and source-trunk commit/layout.
- [x] Rerun fresh benchmark and update `research/runtime-baseline.md` with the new substage distribution.
- [x] Decide whether the first runtime-reducing implementation should target characterization reachability/lazy characterization, pre-materialization topology pruning, or source-trunk/topology commit work based on measured substage data.
- [x] Optimize source-trunk top-segment frontier synthesis by adding an all-frontier-only segment synthesis path for `SourceTrunkSegment::build`.
- [ ] If characterization remains the target, add default-off or report-only reachability counters first.
- [ ] If pruning becomes the target, add proof/equivalence harness before changing default pruning behavior.
- [x] Rebuild affected targets after structural or CMake changes.

## Validation

- [x] `git diff --check`
- [x] `cmake --build build --target icts_test_flow_synthesis_htree -j $(nproc)`
- [x] `./bin/icts_test_flow_synthesis_htree`
- [x] `cmake --build build --target iEDA -j $(nproc)`
- [x] Copy rebuilt `iEDA` into `scripts/design/ics55_dev/iEDA` if needed for the benchmark. Build target links directly to that path; no manual copy was required.
- [x] Run target command again and store after-change artifacts.
- [x] Compare runtime and QoR metrics against fresh baseline.
- [x] Final handoff validation: `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`

## Review Gates

- Do not run `task.py start` until `prd.md`, `design.md`, and `implement.md` are reviewed.
- Do not keep default-on approximate pruning without explicit user approval.
- Stop the loop on selected topology drift, global QoR drift, fallback mismatch, or weak runtime benefit that does not justify code complexity.
