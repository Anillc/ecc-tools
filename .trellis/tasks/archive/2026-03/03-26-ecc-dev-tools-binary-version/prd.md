# Improve ecc_dev_tools binary version selection

## Goal
Make ecc_dev_tools choose tool binaries more gracefully when multiple versioned clang-related binaries exist on the system.

## Requirements
- Remove hard-coded package/version assumptions such as `clang-format-18`, `clang-tidy-18`, `clang-18` where runtime selection should be dynamic.
- Detect available clang-related binaries and prefer the newest compatible installed binary.
- Keep explicit CLI overrides working.
- Keep doctor/check output understandable for users.

## Acceptance Criteria
- [ ] When multiple versioned clang binaries are installed, the tool auto-selects the highest available version.
- [ ] The code no longer relies on fixed version suffixes like `-18` for runtime tool resolution.
- [ ] Existing explicit binary override behavior still works.
- [ ] Doctor/check flows still pass with the updated resolution logic.

## Technical Notes
- Focus on `.trellis/ecc_dev_tools/`.
- Inspect environment/tool discovery logic first.
- Prefer minimal changes over refactoring unrelated code.
