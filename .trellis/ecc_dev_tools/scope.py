from __future__ import annotations

from pathlib import Path

try:
    from .models import Scope
except ImportError:
    from models import Scope


def build_scope(repo_root: Path, raw_paths: list[str]) -> Scope:
    if not raw_paths:
        raise ValueError("At least one --path value is required.")

    resolved_paths: list[Path] = []
    for raw_path in raw_paths:
        candidate = Path(raw_path)
        if not candidate.is_absolute():
            candidate = repo_root / raw_path
        candidate = candidate.resolve()
        if not candidate.exists():
            raise ValueError(f"Scope path does not exist: {raw_path}")
        try:
            candidate.relative_to(repo_root.resolve())
        except ValueError as exc:
            raise ValueError(f"Scope path must stay inside the repository: {raw_path}") from exc
        resolved_paths.append(candidate)
    return Scope(repo_root=repo_root, raw_paths=raw_paths, resolved_paths=resolved_paths)
