# Update CTS spec boundaries and AI preflight

## Goal

Update the CTS Trellis specs for the three confirmed gaps:

1. clarify raw SDC/iDB boundary rules;
2. document accepted legacy/spec exceptions as narrow executable policy;
3. add AI-friendly preflight heuristics for CTS spec compliance.

## Requirements

- Keep updates scoped to `.trellis/spec`.
- Prefer executable contracts, examples, and decision rules over broad principles.
- Avoid repetitive prose that duplicates existing CTS specs.
- Do not run the ECC dev checker for this spec-only task, per user request.

## Acceptance Criteria

- [x] Boundary spec distinguishes forbidden raw database leakage from allowed CTS ingress/projection adapters.
- [x] Known exceptions are documented as narrow rules with conditions, not blanket waivers.
- [x] AI preflight content gives targeted scans and interpretation rules for future CTS work.
- [x] Spec update is committed separately from the code performance fix.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
