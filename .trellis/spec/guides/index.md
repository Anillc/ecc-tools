# Thinking Guides

> **Purpose**: Expand your thinking to catch things you might not have considered.

---

## Why Thinking Guides?

**Most bugs and tech debt come from "didn't think of that"**, not from lack of skill:

- Didn't think about what happens at layer boundaries → cross-layer bugs
- Didn't think about code patterns repeating → duplicated code everywhere
- Didn't think about pointer ownership when passing data between singletons → use-after-free or dangling pointer
- Didn't think about unit/coordinate conventions → silent numerical errors

These guides help you **ask the right questions before coding**.

---

## Available Guides

| Guide | Purpose | When to Use |
|-------|---------|-------------|
| [Cross-Layer Thinking Guide](./cross-layer-thinking-guide.md) | Think through data flow across iCTS layers | Features spanning API, Database, Module, or external tool boundaries |
| [Code Reuse Thinking Guide](./code-reuse-thinking-guide.md) | Identify existing patterns and reduce duplication | When adding new algorithms, utilities, or data structures |

---

## Quick Reference: Thinking Triggers

### When to Think About Cross-Layer Issues

- [ ] Feature touches 3+ layers (API, Database, Module, Utils)
- [ ] Data representation changes between layers (e.g., iDB types vs CTS types)
- [ ] Multiple modules consume the same Design/Config data
- [ ] You are unsure whether logic belongs in Database or Module layer
- [ ] Feature involves an external adapter (Wrapper for iDB, STAAdapter for iSTA)

> Read [Cross-Layer Thinking Guide](./cross-layer-thinking-guide.md)

### When to Think About Code Reuse

- [ ] You are writing geometric or mathematical logic similar to existing code
- [ ] You see the same singleton access + validation pattern repeated 3+ times
- [ ] You are adding a new field to Config, Design, or another singleton
- [ ] **You are creating a new utility/helper function** -- search `utils/` and `spatial/` first
- [ ] **You are adding a new CMake target** -- check if an existing INTERFACE library already covers the dependency

> Read [Code Reuse Thinking Guide](./code-reuse-thinking-guide.md)

---

## Pre-Modification Rule (CRITICAL)

> **Before changing ANY value, constant, or singleton member, ALWAYS search first!**

```bash
# Search for the value you are about to change
grep -r "value_to_change" src/operation/iCTS/

# Search across singletons for related access
grep -r "CONFIG_INST\.\|WRAPPER_INST\.\|DESIGN_INST\." src/operation/iCTS/
```

This single habit prevents most "forgot to update X" bugs.

---

## How to Use This Directory

1. **Before coding**: Skim the relevant thinking guide
2. **During coding**: If something feels repetitive or crosses a layer boundary, check the guides
3. **After bugs**: Add new insights to the relevant guide (learn from mistakes)

---

## Contributing

Found a new "didn't think of that" moment? Add it to the relevant guide.

---

**Core Principle**: 30 minutes of thinking saves 3 hours of debugging.
