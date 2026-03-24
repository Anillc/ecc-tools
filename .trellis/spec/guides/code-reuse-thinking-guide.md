# Code Reuse Thinking Guide

Use this guide before adding new helpers, utilities, fallback logic, or CMake wiring.

## Scope

This guide is a prompt, not a rule document.
Use backend specs for placement, ownership, and dependency rules.

## Search First

Before writing new code, search for an existing pattern in:
- `src/operation/iCTS/source/utils/`
- `src/operation/iCTS/source/database/spatial/`
- nearby module directories
- existing CMake targets that may already expose the dependency

## Questions

Ask these questions first:
- Does a similar helper already exist?
- Am I copying logic from another module?
- Is this really shared logic, or only local logic?
- Does an existing INTERFACE or library target already provide this dependency?
- Should repeated config fallback live in one helper instead of many call sites?

## Common Reuse Targets

| Need | Look In |
|------|---------|
| geometry helpers | `source/utils/geometry/`, `source/database/spatial/` |
| singleton access patterns | nearby modules and initialization boundaries |
| config fallback logic | shared helpers near the first real use site |
| routing strategy dispatch | existing router facade patterns |
| include/CMake reuse | existing targets and INTERFACE libraries |

## Extraction Checklist

Extract shared code when:
- the same logic appears 3 or more times
- the logic is complex enough to drift or hide bugs
- multiple modules need the same preparation or conversion step

Do not extract when:
- the code is used in one place only
- the abstraction would add heavy dependencies for little benefit
- the local code is clearer than a shared wrapper

## Final Check

Before handoff, verify:
- [ ] I searched for an existing pattern first
- [ ] I did not duplicate code that should be shared
- [ ] Repeated fallback logic now lives in one place
- [ ] CMake reuse is expressed through target links, not duplicated include paths
- [ ] The new structure is simpler than the repeated local copies

## Related Docs

- `../backend/directory-structure.md`
- `../backend/database-guidelines.md`
- `../backend/quality-guidelines.md`
