# Thinking Guides

Use guides to decide what to think about before coding.

## Scope

Guides are prompts and checklists.
Backend specs remain the authority for actual rules.

## Guide List

| Guide | Use When |
|------|----------|
| [Cross-Layer Thinking Guide](./cross-layer-thinking-guide.md) | A change crosses API, database, module, or external-adapter boundaries |
| [Code Reuse Thinking Guide](./code-reuse-thinking-guide.md) | A change may duplicate logic, helpers, config resolution, or CMake wiring |

## Trigger Checklist

Read the cross-layer guide when:
- the change touches 3 or more layers
- data types or units change across a boundary
- multiple modules consume the same shared data
- external adapters such as Wrapper or STAAdapter are involved

Read the reuse guide when:
- you are about to create a new helper or utility
- you are copying logic from another module
- you are adding a new CMake target or include relationship
- you suspect a similar pattern already exists elsewhere
