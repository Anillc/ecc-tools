# CTS Naming Audit

## Renamed Now

- `CTSFlowArtifact` family became `ClockTreeReportData`, `ClockTreeReportClock`, `ClockTreeReportNet`, `ClockTreeReportInst`, and `ClockTreeReportSegment`; the source directory moved from `source/flow/artifact/` to `source/flow/report_data/`.
- `CTSArtifactView` became `ClockTreeReportView`; `CTSSynthesisStageKind` became `ClockTreeSynthesisPhase`; report-data fields now use `synthesis_phase`.
- `ClockRunCoordinator` became `ClockTreeSynthesisDriver`, reflecting the per-clock clock-tree synthesis boundary.
- `CTSRuntimeSession` became `CTSRunEnvironment`, reflecting run setup/environment ownership.
- Static stage wrapper classes were renamed to step names: `CTSClockDataLoadStep`, `CTSClockTreeSynthesisStep`, `CTSClockTreeWritebackStep`, `CTSClockTreeEvaluationStep`, and `CTSClockTreeReportStep`.
- `ClockNetManager` became `ClockNetEditor`; sink-group APIs and counters became sink-domain APIs and counters.
- CTS report tables now use `Report` / `View` headings instead of `Artifact` / `Type/View`.
- `CTSSinkDomain::kTop` became `CTSSinkDomain::kSourceToRoot`.

## Extracted Now

- `ClockTreeReportDataBuilder` now owns clock-tree report data construction under `source/flow/report_data/`: segment creation, routed/flyline/fallback segment recording, report net/inst construction, topology-level resolution, sink-domain report data, source-to-root report data, direct-domain report data, and report-data merge.
- `ClockTreeSynthesisDriver` now delegates report-data geometry/report-object construction to `ClockTreeReportDataBuilder`; the driver remains focused on per-clock sequencing, sink-domain preparation, synthesis calls, commit/rollback, run-summary accounting, and flow-row status.
- Routed, flyline, and fallback segment recording now pass through a typed `ClockTreeReportSegmentSource` path inside the report-data builder, preserving `ClockTreeReportSegment`, `CTSNetRole`, `CTSSinkDomain`, and `ClockTreeSynthesisPhase` semantics without object-name parsing.

## Deferred With Reason

- `FlowManager` remains for now because it is the established internal singleton facade used by tests and source-layer callers; rename it with a dedicated facade migration rather than this focused pass.
- Static step wrappers remain classes instead of namespaces or owned objects because P2 already tracks that lifecycle cleanup.
- `ClockSynthesisNetEditor`, `SegmentBuilder`, and `HTreeMaterialization` remain until P1 item 6 consolidates temp-object and final-design commit logic.
- Generated object-name prefix `cts_flow_clk_` remains to avoid changing emitted CTS object names in the same pass as type/file renames.
- Historical research notes still mention old names where they describe the pre-rename architecture.
- `top_segment` remains only in source-to-root segment diagnostic strings, log context labels, and generated `SegmentBuilder` object-name suffixes. Rename it with a focused object-name/report-string migration because changing it now can alter emitted CTS object names and golden log text outside this report-data extraction.
- Sink-domain planning remains in `ClockTreeSynthesisDriver` for this pass because it is intertwined with root-buffer insertion, downstream-net creation, per-clock rollback, sink-domain counters, and failure-row emission. Extract it as `ClockSinkDomainBuilder` or similar after adding narrow tests around root-buffer/downstream-net failure paths.
- Synthesis commit/rollback remains in `ClockTreeSynthesisDriver` for this pass because it preserves the existing transaction order: build pending report data, commit inserted CTS objects, merge report data only after commit, and roll back source-net/load topology on failure. Extract it only with focused coverage for commit failure and source-to-root failure cases.
- Flow-row assembly remains a local helper in `ClockTreeSynthesisDriver` for now. It should move with sink-domain planning or a `ClockTreeRunRows` helper so status rows stay aligned with the transaction owner.

## Accepted Local Mechanics

- `BuildResult`, `BuildOptions`, `SourceToRootBuildResult`, `SourceToRootBuildOptions`, and similar result/options structs are narrow C++ implementation contracts, not CTS domain databases.
- Local `Context` structs such as the remaining driver-local `SinkDomainContext`, H-tree materialization contexts, and log contexts are scoped implementation carriers.
- Characterization and H-tree `Registry` types represent algorithm pattern registries and are acceptable local mechanics.
- Writer/reporter names such as `CTSGdsWriter`, `CTSStatisticsWriter`, and `ClockSynthesisReporter` identify file-output helpers rather than CTS topology domain objects.
