# Backend Development Guidelines

Backend rules for `src/operation/iCTS/`.

## Read Order

1. [Project Constraints](../project-constraints.md)
2. [Directory Structure](./directory-structure.md)
3. [Quality Guidelines](./quality-guidelines.md)
4. The topic-specific docs below as needed

## Authority Map

| Doc | Owns |
|-----|------|
| [Project Constraints](../project-constraints.md) | Repository-wide hard constraints |
| [Directory Structure](./directory-structure.md) | Code placement, layers, CMake target structure |
| [Database Guidelines](./database-guidelines.md) | Runtime ownership, dependency boundaries, data-model rules |
| [Logging Guidelines](./logging-guidelines.md) | Runtime `LOG_*`, structured report output, and log levels |
| [Error Handling](./error-handling.md) | No-exception policy and severity decisions |
| [Quality Guidelines](./quality-guidelines.md) | Naming, includes, dependency visibility, review checks |
| [Quality Workflow](../../ecc_dev_tools/README.md) | `ecc_dev_tools` commands, outputs, suppressions, tool behavior |

## Topic Guide

- Adding or moving modules/targets -> [Directory Structure](./directory-structure.md)
- Working with `Design`, `Config`, `Wrapper`, or `STAAdapter` -> [Database Guidelines](./database-guidelines.md)
- Refactoring CTS flow, synthesis, evaluation, or report code -> [Directory Structure](./directory-structure.md), [Database Guidelines](./database-guidelines.md), and [Quality Guidelines](./quality-guidelines.md)
- Choosing log level or logging style -> [Logging Guidelines](./logging-guidelines.md)
- Deciding whether to return, warn, or terminate -> [Error Handling](./error-handling.md)
- Naming, include cleanup, CMake visibility, or validation flow -> [Quality Guidelines](./quality-guidelines.md)

## Note

This page is navigation only. Do not duplicate rules here. Update the authority doc for the topic instead.

**Language**: All spec documents are written in English.
