from __future__ import annotations

import shutil
import sys
from pathlib import Path
from typing import Iterable

try:
    from .models import CheckKind, EnvironmentSnapshot, ExecutionPlan, ToolRequirement, ToolStatus
    from .utils import (
        default_jobs,
        detect_idle_threads,
        parse_version_suffix,
        parse_version_text,
        run_command,
        version_meets_minimum,
    )
except ImportError:
    from models import CheckKind, EnvironmentSnapshot, ExecutionPlan, ToolRequirement, ToolStatus
    from utils import (
        default_jobs,
        detect_idle_threads,
        parse_version_suffix,
        parse_version_text,
        run_command,
        version_meets_minimum,
    )


TOOL_REQUIREMENTS: tuple[ToolRequirement, ...] = (
    ToolRequirement("python", None, True, "Current interpreter running the checker."),
    ToolRequirement("cmake", (3, 16), True, "Needed for File API metadata and refresh."),
    ToolRequirement("ninja", None, True, "Required configure/build generator for this checker."),
    ToolRequirement("clang-format", None, True, "Used for format checks and fixes."),
    ToolRequirement("clang-tidy", None, True, "Used for tidy checks."),
    ToolRequirement("clang++", None, True, "Used for explicit Clang frontend syntax-only passes."),
    ToolRequirement("g++", (10, 0), True, "Used for header self-contained compilation checks."),
    ToolRequirement("clang-scan-deps", None, False, "Optional: accurate include dependency scanning for header checks."),
    ToolRequirement("include-what-you-use", None, False, "Optional: IWYU include analysis for detecting missing/unnecessary includes."),
)


def inspect_environment(repo_root: Path, build_dir: Path, jobs: int | None = None, clang_tidy_binary: str | None = None) -> EnvironmentSnapshot:
    total_cpus = max(1, __import__("os").cpu_count() or 1)
    idle_threads = detect_idle_threads(total_cpus)
    computed_jobs = jobs if jobs is not None else default_jobs(total_cpus, idle_threads)

    statuses: list[ToolStatus] = []
    for requirement in TOOL_REQUIREMENTS:
        if requirement.name == "python":
            statuses.append(_probe_python(requirement))
        elif requirement.name == "clang-tidy":
            statuses.append(_discover_latest_binary(repo_root, requirement, override=clang_tidy_binary))
        elif requirement.name in {"clang-format", "clang++", "g++", "clang-scan-deps"}:
            statuses.append(_discover_latest_binary(repo_root, requirement))
        elif requirement.name == "include-what-you-use":
            statuses.append(_probe_tool(repo_root, requirement))
        else:
            statuses.append(_probe_tool(repo_root, requirement))

    return EnvironmentSnapshot(
        repo_root=repo_root,
        build_dir=build_dir,
        jobs=computed_jobs,
        total_cpus=total_cpus,
        idle_threads_estimate=idle_threads,
        tool_statuses=statuses,
    )


def validate_required_tools(snapshot: EnvironmentSnapshot, kinds: Iterable[CheckKind], plan: ExecutionPlan | None = None) -> None:
    required = required_tool_names_for_run(kinds, plan)
    failures = [status for status in snapshot.tool_statuses if status.name in required and not status.ok]
    if failures:
        lines = ["Required tool checks failed:"]
        for status in failures:
            version = f" ({status.version_text})" if status.version_text else ""
            lines.append(f"- {status.name}{version}: {status.message}")
        raise RuntimeError("\n".join(lines))


def required_tool_names_for_run(kinds: Iterable[CheckKind], plan: ExecutionPlan | None = None) -> set[str]:
    return _required_tool_names(set(kinds), plan)


def _required_tool_names(kinds: set[CheckKind], plan: ExecutionPlan | None = None) -> set[str]:
    required = {"python"}
    if "format" in kinds:
        required.add("clang-format")
    if {"tidy", "headers", "cmake"} & kinds:
        required.update({"cmake", "ninja"})
    if "tidy" in kinds:
        required.add("clang-tidy")
        if plan is not None:
            for tidy_pass in plan.tidy_passes:
                if tidy_pass.tool_name != "clang-tidy":
                    required.add(tidy_pass.tool_name)
    if "headers" in kinds:
        required.add("g++")
    if "iwyu" in kinds:
        required.update({"cmake", "ninja"})
    return required


def _probe_python(requirement: ToolRequirement) -> ToolStatus:
    executable = Path(sys.executable or "python3").resolve()
    version_info = sys.version.split()[0] if sys.version else "unknown"
    return ToolStatus(
        name=requirement.name,
        required=requirement.required,
        found=True,
        executable=str(executable),
        version_text=f"Python {version_info}",
        ok=True,
        message="using current interpreter",
        selection_policy="current-interpreter",
        requested_executable=str(executable),
        selected_candidate=executable.name,
    )


def _discover_latest_binary(repo_root: Path, requirement: ToolRequirement, override: str | None = None) -> ToolStatus:
    if override:
        status = _probe_tool(repo_root, requirement, executable_name=override)
        status.selection_policy = "explicit-override"
        status.requested_executable = override
        status.selected_candidate = Path(override).name
        if not status.found:
            status.message = f"override not found: {override}"
        else:
            status.message = f"using explicit override: {override}"
        return status

    best_status: ToolStatus | None = None
    best_candidate: str | None = None
    for candidate in _versioned_candidates(requirement.name):
        status = _probe_tool(repo_root, requirement, executable_name=candidate)
        if not status.found:
            continue
        if best_status is None or _binary_sort_key(status, requirement.name) > _binary_sort_key(best_status, requirement.name):
            best_status = status
            best_candidate = candidate

    if best_status is not None:
        best_status.name = requirement.name
        best_status.selection_policy = "newest-available"
        best_status.requested_executable = requirement.name
        best_status.selected_candidate = Path(best_status.executable).name if best_status.executable else best_candidate
        best_status.message = f"auto-selected newest available {requirement.name}"
        return best_status

    return ToolStatus(
        name=requirement.name,
        required=requirement.required,
        found=False,
        executable=None,
        ok=not requirement.required,
        message="not found in PATH",
        selection_policy="newest-available",
        requested_executable=requirement.name,
    )


def _versioned_candidates(base_name: str) -> list[str]:
    candidates = [base_name]
    for version in range(30, 3, -1):
        candidates.append(f"{base_name}-{version}")
    return candidates


def _binary_sort_key(status: ToolStatus, base_name: str) -> tuple[tuple[int, ...], int, str]:
    candidate_name = Path(status.executable).name if status.executable else status.selected_candidate or status.requested_executable or ""
    suffix_version = parse_version_suffix(candidate_name, base_name)
    reported_version = parse_version_text(status.version_text or "")
    effective_version = max(reported_version, suffix_version)
    direct_match = 1 if status.executable and Path(status.executable).name == status.name else 0
    executable = status.executable or ""
    return (effective_version, direct_match, executable)


def _probe_tool(repo_root: Path, requirement: ToolRequirement, executable_name: str | None = None) -> ToolStatus:
    lookup_name = executable_name or requirement.name
    executable = shutil.which(lookup_name)
    if executable is None:
        return ToolStatus(
            name=requirement.name,
            required=requirement.required,
            found=False,
            executable=None,
            ok=not requirement.required,
            message="not found in PATH",
            selection_policy="path-lookup",
            requested_executable=lookup_name,
            selected_candidate=lookup_name,
        )

    result = run_command([executable, "--version"], cwd=repo_root)
    version_output = "\n".join(part for part in (result.stdout, result.stderr) if part).strip()
    version_text = version_output if version_output else "unknown version"

    ok = result.returncode == 0
    message = "available"
    if requirement.min_version is not None:
        parsed = parse_version_text(version_text)
        ok = ok and version_meets_minimum(parsed, requirement.min_version)
        minimum = ".".join(str(x) for x in requirement.min_version)
        message = f">= {minimum}" if ok else f"requires >= {minimum}"
    return ToolStatus(
        name=requirement.name,
        required=requirement.required,
        found=True,
        executable=executable,
        version_text=version_text,
        ok=ok,
        message=message,
        selection_policy="path-lookup",
        requested_executable=lookup_name,
        selected_candidate=Path(executable).name,
    )
