# Thinking Guides

Use these files as short pre-coding checklists, not as implementation specs.

## Read Which Guide
- Read [cross-layer-thinking-guide.md](./cross-layer-thinking-guide.md) when a CTS change touches `interface/`, `platform/`, `api/`, config, or report outputs.
- Read [code-reuse-thinking-guide.md](./code-reuse-thinking-guide.md) when adding helpers, config fields, wrapper commands, or repeated flow logic.

## CTS Triggers
- The same CTS feature appears in Tcl, Python, GUI, and `tool_manager`.
- A config key or output path is read in more than one layer.
- You are changing a public command like `run_cts` or `cts_report`.
- You are adding a new file and are not sure whether `api`, `module`, `solver`, or `interface` should own it.

## Search First Rule
```bash
rg -n "run_cts|cts_report|config_key|class_name" src .trellis
```

## Core Principle
- Search before editing.
- Trace the full call path before moving logic.
- Prefer one shared flow over multiple near-identical wrappers.
