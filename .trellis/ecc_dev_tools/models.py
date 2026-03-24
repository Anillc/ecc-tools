from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

Severity = Literal["error", "warning", "info"]
ScopeClass = Literal["in_scope", "out_of_scope"]
CheckKind = Literal["format", "tidy", "headers", "cmake", "iwyu"]
Confidence = Literal["high", "medium", "low"]
TidyMode = Literal["deep", "naming"]
PassPlanName = Literal["complete", "legacy", "tidy-only"]


@dataclass(frozen=True)
class ToolRequirement:
    name: str
    min_version: tuple[int, ...] | None = None
    required: bool = True
    notes: str = ""


@dataclass
class ToolStatus:
    name: str
    required: bool
    found: bool
    executable: str | None = None
    version_text: str | None = None
    ok: bool = False
    message: str = ""
    selection_policy: str = "path-lookup"
    requested_executable: str | None = None
    selected_candidate: str | None = None


@dataclass
class EnvironmentSnapshot:
    repo_root: Path
    build_dir: Path
    jobs: int
    total_cpus: int
    idle_threads_estimate: int
    tool_statuses: list[ToolStatus] = field(default_factory=list)

    def executable_map(self) -> dict[str, str]:
        return {
            status.name: status.executable
            for status in self.tool_statuses
            if status.found and status.executable is not None
        }

    def get_tool_status(self, name: str) -> ToolStatus:
        for status in self.tool_statuses:
            if status.name == name:
                return status
        raise KeyError(name)


@dataclass
class Scope:
    repo_root: Path
    raw_paths: list[str]
    resolved_paths: list[Path]

    def contains(self, path: Path) -> bool:
        try:
            resolved = path.resolve()
        except FileNotFoundError:
            resolved = path.absolute()
        return any(resolved == scope_path or scope_path in resolved.parents for scope_path in self.resolved_paths)

    def relative_items(self) -> list[str]:
        items: list[str] = []
        for path in self.resolved_paths:
            try:
                items.append(str(path.relative_to(self.repo_root)))
            except ValueError:
                items.append(str(path))
        return items


@dataclass
class Finding:
    check: CheckKind
    severity: Severity
    path: Path | None
    message: str
    line: int | None = None
    target: str | None = None
    category: str | None = None
    subtype: str | None = None
    origin: str | None = None
    confidence: Confidence = "medium"
    trigger_path: Path | None = None
    trigger_target: str | None = None
    location_scope_class: ScopeClass = "out_of_scope"
    trigger_scope_class: ScopeClass | None = None
    tags: tuple[str, ...] = ()

    @property
    def scope_class(self) -> ScopeClass:
        return self.location_scope_class

    def sort_key(self) -> tuple[str, int, str, str, str]:
        path_str = str(self.path) if self.path else ""
        trigger_str = str(self.trigger_path) if self.trigger_path else ""
        return (path_str, self.line or 0, trigger_str, self.severity, self.message)


@dataclass
class CheckResult:
    kind: CheckKind
    findings: list[Finding] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    summary_categories: list[str] = field(default_factory=list)
    detail_limit: int = 20

    def in_scope_findings(self) -> list[Finding]:
        return [finding for finding in self.findings if finding.location_scope_class == "in_scope"]

    def out_of_scope_findings(self) -> list[Finding]:
        return [finding for finding in self.findings if finding.location_scope_class == "out_of_scope"]

    def triggered_in_scope_findings(self) -> list[Finding]:
        return [finding for finding in self.findings if finding.trigger_scope_class == "in_scope"]


@dataclass
class CompileCommand:
    file: Path
    directory: Path
    command: str


@dataclass
class CMakeTarget:
    name: str
    json_file: Path | None = None
    target_type: str | None = None
    source_dir: Path | None = None
    build_dir: Path | None = None
    sources: list[Path] = field(default_factory=list)
    declared_links: list[str] = field(default_factory=list)
    declared_link_scopes: dict[str, str] = field(default_factory=dict)
    include_dirs: list[Path] = field(default_factory=list)

    def owns_path(self, path: Path) -> bool:
        if path in self.sources:
            return True
        if self.source_dir is None:
            return False
        return path == self.source_dir or self.source_dir in path.parents


@dataclass
class BuildContext:
    build_dir: Path
    compile_commands_path: Path
    file_api_reply_dir: Path
    compile_commands: list[CompileCommand]
    targets: dict[str, CMakeTarget]
    declared_graph: dict[str, list[str]]
    cmake_text_graph: dict[str, list[str]]
    cmake_public_graph: dict[str, list[str]]
    profile_name: str
    skipped_trace_generator_expressions: int = 0
    refreshed: bool = False
    refresh_reason: str | None = None


@dataclass(frozen=True)
class ValidationPreset:
    name: str
    description: str
    kinds: tuple[CheckKind, ...]

    def has_kind(self, kind: CheckKind) -> bool:
        return kind in self.kinds


@dataclass(frozen=True)
class TidyPass:
    name: str
    description: str
    tool_name: str
    runner: str
    checks_arg: str | None = None
    on_demand: bool = False
    dedupe_priority: int = 0


@dataclass(frozen=True)
class ExecutionPlan:
    validation_preset: str
    kinds: tuple[CheckKind, ...]
    tidy_mode: TidyMode
    pass_plan: PassPlanName
    tidy_passes: tuple[TidyPass, ...] = ()
    compatibility_notes: tuple[str, ...] = ()

    def has_kind(self, kind: CheckKind) -> bool:
        return kind in self.kinds

    def uses_deep_tidy(self) -> bool:
        return self.tidy_mode == "deep"

    def requires_build_context(self) -> bool:
        return any(kind in {"tidy", "headers", "cmake", "iwyu"} for kind in self.kinds)


@dataclass(frozen=True)
class Profile:
    name: str
    description: str
    scope_roots: tuple[str, ...]
    target_prefixes: tuple[str, ...]
    clang_tidy_config: str
    deep_tidy_checks: tuple[str, ...] = ()
    analyzer_checks: str = "-*,clang-analyzer-*"
    default_validation_preset: str = "default"
    default_tidy_mode: TidyMode = "deep"
    default_pass_plan: PassPlanName = "complete"
    build_target: str = "iEDA"
    graph_root: str = ""
    include_inference_roots: tuple[str, ...] = ()
    source_extensions: tuple[str, ...] = (".cc",)
    header_extensions: tuple[str, ...] = (".hh",)


@dataclass(frozen=True)
class Suppression:
    path: str | None
    category: str | None
    subtype: str | None
    pattern: str | None
    reason: str

    def matches(self, finding: Finding, repo_root: Path) -> bool:
        if self.path is not None:
            finding_path = str(finding.path) if finding.path else ""
            if not finding_path.endswith(self.path) and self.path not in finding_path:
                return False
        if self.category is not None and finding.category != self.category:
            return False
        if self.subtype is not None and finding.subtype != self.subtype:
            return False
        if self.pattern is not None and self.pattern not in (finding.message or ""):
            return False
        return True


def load_suppressions(repo_root: Path) -> list[Suppression]:
    """Load suppression rules from the suppressions.jsonl file.

    Each line in the file is a JSON object describing a suppression rule.
    Lines that are empty, start with '#', or start with '//' are skipped.
    """
    suppression_file = repo_root / ".trellis" / "ecc_dev_tools" / "suppressions.jsonl"
    if not suppression_file.exists():
        return []
    suppressions: list[Suppression] = []
    for line in suppression_file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        data = json.loads(line)
        suppressions.append(Suppression(
            path=data.get("path"),
            category=data.get("category"),
            subtype=data.get("subtype"),
            pattern=data.get("pattern"),
            reason=data.get("reason", ""),
        ))
    return suppressions
