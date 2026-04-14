# Code Reuse Thinking Guide

Use this when a CTS change feels repetitive.

## Search Hotspots First
```bash
rg -n "runCTS|reportCTS|run_cts|cts_report|set_status_stage|get_icts_path" src
rg -n "registerTclCmd|m\\.def\\(\" src/interface
```

## Common CTS Duplication Risks
- The same flow wrapper copied across Tcl, Python, GUI, and `tool_manager`.
- Config defaults or key names duplicated in parser, config, and interface layers.
- Repeated logging or timing-stage code across multiple entrypoints.
- New utility code that already exists in `CTSAPI`, `CtsDBWrapper`, or solver tools.

## Decision Checklist
- Does `CTSAPI` already own this lifecycle step?
- Does `tool_manager` already expose a matching operation?
- Is there already a Tcl or Python registration pattern for this command?
- Should this live in `module/` or `solver/tools/` instead of a new helper file?

## After Batch Changes
- Search all public names again.
- Check the nearest `CMakeLists.txt`.
- Check registration files and default config.
- If the same edit appears 3+ times, stop and extract a shared path.
