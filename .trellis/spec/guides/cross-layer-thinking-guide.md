# Cross-Layer Thinking Guide

Use this guide before implementing changes that cross API, database, module, or adapter boundaries.

## Scope

This guide is a prompt, not a rule document.
Use backend specs for ownership, placement, logging, and error rules.

## When to Use It

Use this guide when:
- a feature touches 3 or more layers
- data changes type or unit across a boundary
- multiple modules consume shared data from `Design` or `Config`
- the change interacts with Wrapper or STAAdapter
- pointer ownership or initialization order feels unclear

## Map the Flow First

Write down the flow in one line before coding:

```text
iDB -> Wrapper -> Design -> Module -> Wrapper -> iDB
```

For each step, ask:
- what is the exact type?
- what unit is used?
- who owns the object?
- who validates failure?
- who logs the error?

## Boundary Checklist

| Boundary | Questions |
|----------|-----------|
| API -> Database | Are runtime-owned dependencies initialized before use? |
| Database -> Module | Are pointers borrowed safely? Are units still correct? |
| Wrapper -> iDB | Are external-tool types contained inside the adapter? |
| STAAdapter -> iSTA | Are timing units and names translated correctly? |
| Module -> Module | Are produced data structures valid for the next consumer? |

## Common Failure Modes

Watch for:
- DBU vs user-unit confusion
- cached borrowed pointers surviving owner reset
- module code reaching into iDB or iSTA directly
- config reads before initialization
- duplicated null checks that should have been enforced earlier

## Final Check

Before handoff, verify:
- [ ] I mapped the full cross-layer path
- [ ] I identified type, unit, and ownership at each boundary
- [ ] I checked initialization order assumptions
- [ ] I kept external-tool access inside the adapter layer
- [ ] I tested empty/null/fallback behavior at the boundary

## Related Docs

- `../backend/directory-structure.md`
- `../backend/database-guidelines.md`
- `../backend/logging-guidelines.md`
- `../backend/error-handling.md`
