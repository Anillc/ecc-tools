# Technical Design

## Scope

This task covers iCTS clock-data tracing robustness, latest-binary rebuild, and six-case ics55 ECC CTS rerun analysis. It does not tune CTS QoR parameters or modify benchmark source configs beyond local output path relocation.

## Runtime Setup

The existing `scripts/design/ics55_ecc_dev` workspace is the baseline runner. Its generated local config copies preserve source input paths and source CTS JSON content, while `db_default_config.json` and `flow_config.json` point output/config paths to the local workspace.

The final rerun will use the current-source binary at `scripts/design/ics55_dev/iEDA` after rebuilding. The older `bin/iEDA` is retained only as historical evidence for the previous failed reproduction.

## Suspected Code Boundary

Current-source binary crashes in iCTS SDC clock tracing:

- `ClockDataRead::read`
- `SdcClockReader::traceClockTargets`
- `ClockTraceResolver::resolve`
- `clock_trace::TraceClock`
- `CollectSafeTransitions`
- `OutputFunctionUsesInput`
- `LibertyExpressionUsesPort`

The immediate fault is treating `LibertyExpr::port_name` as a valid C string when it can contain non-null invalid or non-name sentinel data. The fix should defensively recognize usable Liberty expression port names before constructing/comparing strings.

## Compatibility

The change should preserve existing trace behavior for valid Liberty port-name expressions, while skipping malformed or non-port expression nodes safely. If the Liberty expression API exposes a typed node kind, prefer that over pointer heuristics. If not available, introduce a narrow guard that prevents dereferencing obviously invalid sentinel values and document why.

## Rerun Contract

The final six-case run must:

- use the rebuilt latest binary;
- use baseline generated configs from `scripts/design/ics55_ecc_dev/cases/<case>/run_config`;
- not modify CTS tuning parameters;
- write fresh output/logs to a clearly named report/debug directory;
- classify each case by `CTS Key Results` status and process exit status.

## Reporting

The result report should separate:

- fixed latest-binary blocker(s);
- cases that pass;
- cases that fail CTS with internal reasons;
- cases that crash or stop before CTS status;
- benchmark/post-CTS flow issues outside standalone CTS, if visible from logs.
