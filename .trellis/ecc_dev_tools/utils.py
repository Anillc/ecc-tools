from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence


def parse_version_text(version_text: str) -> tuple[int, ...]:
    match = re.search(r"(\d+(?:\.\d+)+)", version_text)
    if match:
        return tuple(int(part) for part in match.group(1).split("."))

    major_only_match = re.search(r"\bversion\s+(\d+)\b", version_text, flags=re.IGNORECASE)
    if major_only_match:
        return (int(major_only_match.group(1)),)

    return tuple()


def parse_version_suffix(candidate_name: str, base_name: str) -> tuple[int, ...]:
    match = re.fullmatch(rf"{re.escape(base_name)}-(\d+(?:\.\d+)*)", candidate_name)
    if not match:
        return tuple()
    return tuple(int(part) for part in match.group(1).split("."))


def version_meets_minimum(actual: tuple[int, ...], minimum: tuple[int, ...]) -> bool:
    if not actual:
        return False
    width = max(len(actual), len(minimum))
    actual_padded = actual + (0,) * (width - len(actual))
    minimum_padded = minimum + (0,) * (width - len(minimum))
    return actual_padded >= minimum_padded


def run_command(
    command: Sequence[str],
    *,
    cwd: Path,
    check: bool = False,
    capture_output: bool = True,
    timeout: int = 300,
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            list(command),
            cwd=cwd,
            text=True,
            capture_output=capture_output,
            encoding="utf-8",
            errors="replace",
            check=False,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(
            f"Command timed out after {timeout}s: {' '.join(command)}"
        ) from exc
    if check and result.returncode != 0:
        stderr = result.stderr.strip() if result.stderr else ""
        stdout = result.stdout.strip() if result.stdout else ""
        detail = stderr or stdout or f"command failed with exit code {result.returncode}"
        raise RuntimeError(f"{' '.join(command)}: {detail}")
    return result


def read_json(path: Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise RuntimeError(f"JSON file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid JSON in {path}: {exc}") from exc


def relative_to_repo(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root.resolve()))
    except ValueError:
        return str(path)


def detect_idle_threads(total_cpus: int) -> int:
    if total_cpus <= 1:
        return 1
    load1, _, _ = os.getloadavg()
    busy_estimate = int(round(load1))
    idle = total_cpus - busy_estimate
    return max(1, idle)


def default_jobs(total_cpus: int, idle_threads: int) -> int:
    return max(1, min(total_cpus, idle_threads // 2 or 1))


def dedupe_keep_order(items: Iterable[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for item in items:
        if item in seen:
            continue
        seen.add(item)
        result.append(item)
    return result


def python_executable() -> str:
    return sys.executable or "python3"
