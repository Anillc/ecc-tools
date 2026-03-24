from __future__ import annotations

import re
import shlex
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path

try:
    from .build_context import compile_commands_for_scope, targets_for_scope
    from .models import BuildContext, CheckResult, CMakeTarget, CompileCommand, EnvironmentSnapshot, ExecutionPlan, Finding, Profile, Scope, TidyPass
    from .utils import dedupe_keep_order, parse_version_text, relative_to_repo, run_command
except ImportError:
    from build_context import compile_commands_for_scope, targets_for_scope
    from models import BuildContext, CheckResult, CMakeTarget, CompileCommand, EnvironmentSnapshot, ExecutionPlan, Finding, Profile, Scope, TidyPass
    from utils import dedupe_keep_order, parse_version_text, relative_to_repo, run_command


TIDY_CATEGORY_GROUPS: list[tuple[str, tuple[str, ...]]] = [
    ("bugprone", ("bugprone-",)),
    ("clang-analyzer", ("clang-analyzer-",)),
    ("modernize", ("modernize-",)),
    ("performance", ("performance-",)),
    ("readability", ("readability-",)),
    ("misc", ("misc-",)),
    ("cppcoreguidelines", ("cppcoreguidelines-",)),
    ("readability-identifier-naming", ("readability-identifier-naming",)),
]

DEEP_TIDY_FAMILIES: list[str] = [group[0] for group in TIDY_CATEGORY_GROUPS]

DIRECT_STDLIB_INCLUDE_RE = re.compile(r"#include\s+(<[^>]+>)")
GENERIC_WARNING_FLAG_TOKENS = (
    "-Wall",
    "-Wextra",
    "-Wconversion",
    "-Wsign-conversion",
)

DIAGNOSTIC_LINE_RE = re.compile(r"^(.*?):(\d+):(\d+):\s+(warning|error|note):\s+(.*)$")
CHECK_BRACKET_RE = re.compile(r"\[([^\[\]]+)\]\s*$")
ANALYZER_TRACE_PREFIXES = (
    "Assuming ",
    "Taking ",
    "Loop condition ",
    "Calling ",
    "Returning ",
    "Value stored to ",
    "Branch condition evaluates to ",
)
SUPPRESSED_WARNING_RE = re.compile(r"^(\d+) warnings? generated\.$")
SUPPRESSED_SUMMARY_RE = re.compile(r"^Suppressed\s+(\d+)\s+warnings?\s*\((.*)\)\.$")

_config_file_support_cache: dict[str, bool] = {}


def _parallel_map(func, items, jobs: int):
    """Run func on each item using up to `jobs` threads. Returns list of results."""
    if jobs <= 1 or len(items) <= 1:
        return [func(item) for item in items]
    results = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(func, item): item for item in items}
        for future in as_completed(futures):
            try:
                results.append(future.result())
            except Exception as exc:
                results.append(exc)
    return results


@dataclass
class TidyPassOutcome:
    tidy_pass: TidyPass
    findings: list[Finding] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    commands_run: int = 0
    fallback_candidates: int = 0
    candidate_commands: list[CompileCommand] = field(default_factory=list)


@dataclass
class ParsedDiagnosticSet:
    findings: list[Finding]
    parsed_count: int
    suppression_note: str | None = None


def run_selected_checks(
    plan: ExecutionPlan,
    *,
    repo_root: Path,
    scope: Scope,
    context: BuildContext | None,
    profile: Profile,
    jobs: int,
    fix: bool,
    snapshot: EnvironmentSnapshot,
) -> list[CheckResult]:
    results: list[CheckResult] = []
    cpp_files: list[Path] | None = None
    if any(kind in ("format", "headers") for kind in plan.kinds):
        cpp_files = _collect_cpp_files(scope, profile)
    for kind in plan.kinds:
        if kind == "format":
            results.append(run_format_check(repo_root, scope, fix, snapshot, profile, jobs=jobs, cpp_files=cpp_files))
        elif kind == "tidy":
            if context is None:
                raise ValueError("Build context is required for tidy checks.")
            results.append(run_tidy_check(repo_root, scope, context, profile, snapshot, plan, jobs=jobs))
        elif kind == "headers":
            if context is None:
                raise ValueError("Build context is required for header checks.")
            results.append(run_header_dependency_check(repo_root, scope, context, profile, snapshot, cpp_files=cpp_files))
        elif kind == "cmake":
            if context is None:
                raise ValueError("Build context is required for cmake checks.")
            results.append(run_cmake_graph_check(scope, context, profile))
        elif kind == "iwyu":
            if context is None:
                raise ValueError("Build context is required for IWYU checks.")
            results.append(run_iwyu_check(scope=scope, context=context, profile=profile, snapshot=snapshot, jobs=jobs))
        else:
            raise ValueError(f"Unsupported check kind: {kind}")
    return results


def run_format_check(
    repo_root: Path,
    scope: Scope,
    fix: bool,
    snapshot: EnvironmentSnapshot,
    profile: Profile | None = None,
    *,
    jobs: int = 1,
    cpp_files: list[Path] | None = None,
) -> CheckResult:
    result = CheckResult(kind="format", detail_limit=20)
    if cpp_files is not None:
        files = cpp_files
    elif profile is not None:
        files = _collect_cpp_files(scope, profile)
    else:
        files = _collect_cpp_files(scope)
    clang_format = _tool_executable(snapshot, "clang-format")
    result.notes.append(f"Checked {len(files)} C/C++ files with clang-format.")
    result.notes.append(f"Using clang-format binary: {clang_format}")

    def _check_one_file(file_path: Path) -> Finding | None:
        if fix:
            run_command([clang_format, "-i", str(file_path)], cwd=repo_root, check=True)
            return None
        original = file_path.read_text(encoding="utf-8", errors="replace")
        formatted = run_command([clang_format, str(file_path)], cwd=repo_root, check=True).stdout
        if original != formatted:
            return Finding(
                check="format",
                severity="warning",
                path=file_path,
                message="Formatting differs from repository .clang-format output.",
                category="format",
                subtype="needs-reformat",
                origin="clang-format",
                confidence="high",
                location_scope_class="in_scope",
                trigger_scope_class="in_scope",
                trigger_path=file_path,
            )
        return None

    outcomes = _parallel_map(_check_one_file, files, jobs)
    for outcome in outcomes:
        if isinstance(outcome, Exception):
            result.notes.append(f"Format check error: {outcome}")
        elif outcome is not None:
            result.findings.append(outcome)
    if fix:
        result.notes.append("Fix mode enabled: files were reformatted in place.")
    return result


def run_tidy_check(
    repo_root: Path,
    scope: Scope,
    context: BuildContext,
    profile: Profile,
    snapshot: EnvironmentSnapshot,
    plan: ExecutionPlan,
    *,
    jobs: int = 1,
) -> CheckResult:
    result = CheckResult(kind="tidy", detail_limit=25)
    commands = compile_commands_for_scope(context, scope)
    headers = _collect_scope_headers(scope, profile)

    result.notes.append(f"Checked {len(commands)} compile commands across {len(plan.tidy_passes)} planned tidy passes.")
    if not commands and not headers:
        return result

    config_path = repo_root / profile.clang_tidy_config
    result.summary_categories = [*DEEP_TIDY_FAMILIES, "clang-diagnostic"]
    result.notes.append(f"Tidy mode: {plan.tidy_mode}")
    result.notes.append(f"Pass plan: {plan.pass_plan}")
    result.notes.append("Compiler diagnostics are summarized separately under clang-diagnostic.")

    fallback_candidates: list[CompileCommand] = []

    for tidy_pass in plan.tidy_passes:
        if tidy_pass.runner == "native-compiler":
            continue
        outcome = _run_tidy_pass(
            tidy_pass,
            repo_root=repo_root,
            scope=scope,
            context=context,
            snapshot=snapshot,
            config_path=config_path,
            commands=commands,
            headers=headers,
            profile=profile,
            jobs=jobs,
        )
        result.findings.extend(outcome.findings)
        result.notes.extend(outcome.notes)
        fallback_candidates.extend(outcome.candidate_commands)

    native_pass = next((tidy_pass for tidy_pass in plan.tidy_passes if tidy_pass.runner == "native-compiler"), None)
    if native_pass is not None:
        ordered_candidates = _dedupe_compile_commands(fallback_candidates)
        ordered_candidates = [
            command
            for command in ordered_candidates
            if not _has_findings_for_trigger_from_other_passes(result.findings, command.file, native_pass.name)
        ]
        if ordered_candidates:
            native_outcome = _run_native_compiler_pass(
                native_pass,
                repo_root=repo_root,
                scope=scope,
                snapshot=snapshot,
                commands=ordered_candidates,
                profile=profile,
                jobs=jobs,
            )
            result.findings.extend(native_outcome.findings)
            result.notes.extend(native_outcome.notes)
        else:
            result.notes.append("Native compiler fallback pass was planned on-demand but was not needed.")

    if plan.tidy_mode == "deep":
        result.notes.append(f"Deep tidy categories ({len(DEEP_TIDY_FAMILIES)} configured families): {', '.join(DEEP_TIDY_FAMILIES)}")
    elif headers:
        result.notes.append("Header naming pass behavior is preserved for naming-only tidy mode.")

    if plan.compatibility_notes:
        for note in plan.compatibility_notes:
            result.notes.append(f"Compatibility note: {note}")

    result.findings = _dedupe_findings(result.findings, plan.tidy_passes)
    return result


def run_header_dependency_check(
    repo_root: Path,
    scope: Scope,
    context: BuildContext,
    profile: Profile,
    snapshot: EnvironmentSnapshot,
    *,
    cpp_files: list[Path] | None = None,
) -> CheckResult:
    result = CheckResult(kind="headers", detail_limit=20)
    targets = targets_for_scope(context, scope)
    compiler = _tool_executable(snapshot, "g++")
    scanner_binary = _optional_tool_executable(snapshot, "clang-scan-deps")
    result.notes.append(f"Resolved {len(targets)} targets for header/dependency analysis.")
    result.notes.append(f"Using compiler binary: {compiler}")
    if scanner_binary:
        result.notes.append(f"Using clang-scan-deps binary: {scanner_binary}")
    else:
        result.notes.append("clang-scan-deps not available; falling back to regex-based include scanning.")

    if cpp_files is not None:
        all_files = cpp_files
    else:
        all_files = _collect_cpp_files(scope, profile)
    header_exts = set(profile.header_extensions)
    headers = sorted(path for path in all_files if path.suffix in header_exts)
    target_include_sets = {
        target.name: _extract_include_roots(target)
        for target in context.targets.values()
    }

    def _check_one_header(header: Path) -> list[Finding]:
        findings: list[Finding] = []
        owners = [target for target in targets if target.owns_path(header)]
        owner = owners[0] if owners else None
        include_dirs = set()
        if owner is not None:
            include_dirs |= target_include_sets.get(owner.name, set())
            include_dirs |= {str(path) for path in owner.include_dirs}
            for dep in owner.declared_links:
                include_dirs |= target_include_sets.get(dep, set())
                dep_target = context.targets.get(dep)
                if dep_target is not None:
                    include_dirs |= {str(path) for path in dep_target.include_dirs}
        else:
            include_dirs |= _infer_interface_include_dirs(header, repo_root, profile)
        include_dirs.add(str(header.parent))
        include_dirs.add(str(repo_root / "src"))

        trigger_command: CompileCommand | None = None
        if owner is not None:
            trigger_command = next((command for command in context.compile_commands if any(command.file == source for source in owner.sources)), None)
            if trigger_command is not None:
                include_dirs |= {str(path) for path in _extract_include_flags(trigger_command.command, trigger_command.directory)}

        if trigger_command is not None:
            compile_cmd = _build_header_syntax_command(trigger_command, header)
            check = run_command(compile_cmd, cwd=trigger_command.directory)
        else:
            compile_cmd = [compiler, "-x", "c++-header", "-std=c++20", "-fsyntax-only", "-Winvalid-pch"]
            for include_dir in sorted(include_dirs):
                compile_cmd.extend(["-I", include_dir])
            compile_cmd.append(str(header))
            check = run_command(compile_cmd, cwd=repo_root)
        if check.returncode != 0:
            diagnostic = _clean_header_diagnostic(check.stderr or check.stdout or "header self-check failed")
            findings.append(
                Finding(
                    check="headers",
                    severity="error",
                    path=header,
                    target=owner.name if owner else None,
                    message=diagnostic,
                    category="self-contained",
                    subtype="compile-failure",
                    origin="header-self-check",
                    confidence="high",
                    location_scope_class="in_scope",
                    trigger_scope_class="in_scope" if trigger_command and scope.contains(trigger_command.file) else None,
                    trigger_path=trigger_command.file if trigger_command else None,
                    trigger_target=owner.name if owner else None,
                )
            )

        include_first_cmd, include_first_cwd, wrapper = _build_header_include_first_command(
            header=header,
            include_dirs=sorted(include_dirs),
            trigger_command=trigger_command,
            compiler=compiler,
            repo_root=repo_root,
        )
        try:
            include_first_check = run_command(include_first_cmd, cwd=include_first_cwd)
            if include_first_check.returncode != 0:
                diagnostic = _clean_header_diagnostic(include_first_check.stderr or include_first_check.stdout or "include-first header self-check failed")
                findings.append(
                    Finding(
                        check="headers",
                        severity="error",
                        path=header,
                        target=owner.name if owner else None,
                        message=diagnostic,
                        category="self-contained",
                        subtype="include-first-failure",
                        origin="header-include-first-check",
                        confidence="high",
                        location_scope_class="in_scope",
                        trigger_scope_class="in_scope" if trigger_command and scope.contains(trigger_command.file) else None,
                        trigger_path=trigger_command.file if trigger_command else None,
                        trigger_target=owner.name if owner else None,
                    )
                )
        finally:
            wrapper.unlink(missing_ok=True)
        return findings

    header_results = _parallel_map(_check_one_header, headers, snapshot.jobs)
    for item in header_results:
        if isinstance(item, Exception):
            result.notes.append(f"Header self-check error: {item}")
            continue
        result.findings.extend(item)

    # Phase 2: include dependency analysis
    # Collect all scoped compile commands for potential clang-scan-deps batch scanning
    scoped_commands = [
        command for command in context.compile_commands if scope.contains(command.file)
    ]

    if scanner_binary and scoped_commands:
        # Use clang-scan-deps for accurate transitive include scanning
        dep_map = _scan_deps_batch(scoped_commands, scanner_binary, jobs=snapshot.jobs)
        result.notes.append(f"clang-scan-deps scanned {len(dep_map)} source files for include dependencies.")
        for target in targets:
            reachable = _transitive_deps(target.name, context.declared_graph)
            for source in target.sources:
                if source not in dep_map:
                    continue
                included_headers = dep_map[source]
                missing: list[str] = []
                for header_path in included_headers:
                    provider = _provider_targets_for_path(header_path, context.targets, target.name, context.declared_graph)
                    exporters = _exporting_targets_for_path(header_path, context.targets, target.name)
                    if exporters.intersection(reachable | {target.name}):
                        continue
                    if provider and provider.isdisjoint(reachable | {target.name}):
                        header_name = header_path.name
                        missing.append(f"{header_name} -> {', '.join(sorted(provider))}")
                for item in dedupe_keep_order(missing):
                    in_scope = scope.contains(source)
                    result.findings.append(
                        Finding(
                            check="headers",
                            severity="warning",
                            path=source,
                            target=target.name,
                            message=f"Possible missing direct target dependency for include {item}",
                            category="target-dependency",
                            subtype="heuristic-provider-gap",
                            origin="clang-scan-deps",
                            confidence="high",
                            location_scope_class="in_scope" if in_scope else "out_of_scope",
                            trigger_scope_class="in_scope" if in_scope else "out_of_scope",
                            trigger_path=source,
                            trigger_target=target.name,
                        )
                    )
    else:
        # Fallback to regex-based include scanning
        for target in targets:
            reachable = _transitive_deps(target.name, context.declared_graph)
            command_map: dict[Path, CompileCommand] = {
                command.file: command for command in context.compile_commands if target.owns_path(command.file)
            }
            for source in target.sources:
                compile_command = command_map.get(source)
                if compile_command is None:
                    continue
                includes = _extract_include_flags(compile_command.command, compile_command.directory)
                missing = []
                for include in _quoted_includes_from_file(source):
                    resolved_include = _resolve_include_path(include, source, includes)
                    if resolved_include is None:
                        continue
                    provider = _provider_targets_for_path(resolved_include, context.targets, target.name, context.declared_graph)
                    exporters = _exporting_targets_for_path(resolved_include, context.targets, target.name)
                    if exporters.intersection(reachable | {target.name}):
                        continue
                    if provider and provider.isdisjoint(reachable | {target.name}):
                        missing.append(f"{include} -> {', '.join(sorted(provider))}")
                for item in dedupe_keep_order(missing):
                    in_scope = scope.contains(source)
                    result.findings.append(
                        Finding(
                            check="headers",
                            severity="warning",
                            path=source,
                            target=target.name,
                            message=f"Possible missing direct target dependency for include {item}",
                            category="target-dependency",
                            subtype="heuristic-provider-gap",
                            origin="include-heuristic",
                            confidence="medium",
                            location_scope_class="in_scope" if in_scope else "out_of_scope",
                            trigger_scope_class="in_scope" if in_scope else "out_of_scope",
                            trigger_path=source,
                            trigger_target=target.name,
                        )
                    )
    return result


def run_cmake_graph_check(scope: Scope, context: BuildContext, profile: Profile) -> CheckResult:
    result = CheckResult(kind="cmake", detail_limit=20)
    targets = [target for target in context.targets.values() if target.name.startswith(profile.target_prefixes)]
    scoped_target_names = {target.name for target in targets_for_scope(context, scope)}
    result.notes.append(f"Analyzed {len(targets)} targets under profile target prefixes.")
    if context.skipped_trace_generator_expressions:
        result.notes.append(
            f"Skipped {context.skipped_trace_generator_expressions} generator-expression link items while reconstructing direct link scopes from CMake trace."
        )

    # Use cmake_text_graph (all edges) for cycle detection — PRIVATE cycles are still cycles
    cycle_adjacency: dict[str, list[str]] = {}
    for target in targets:
        cycle_adjacency[target.name] = [dep for dep in context.cmake_text_graph.get(target.name, []) if dep in context.targets]

    # Use cmake_public_graph (PUBLIC/INTERFACE only) for redundancy — PRIVATE deps don't propagate
    public_adjacency: dict[str, list[str]] = {}
    for target in targets:
        public_adjacency[target.name] = [dep for dep in context.cmake_public_graph.get(target.name, []) if dep in context.targets]

    cycles = _detect_cycles(cycle_adjacency)
    for cycle in cycles:
        in_scope = any(node in scoped_target_names for node in cycle)
        result.findings.append(
            Finding(
                check="cmake",
                severity="error",
                path=None,
                target=" -> ".join(cycle),
                message="CMake target dependency cycle detected.",
                category="cycle",
                subtype="target-cycle",
                origin="cmake-text-graph",
                confidence="high",
                location_scope_class="in_scope" if in_scope else "out_of_scope",
                trigger_scope_class="in_scope" if in_scope else "out_of_scope",
                trigger_target=" -> ".join(cycle),
            )
        )

    for target_name, deps in cycle_adjacency.items():
        for dep in deps:
            if dep not in public_adjacency:
                continue
            if _has_indirect_path(target_name, dep, public_adjacency):
                in_scope = target_name in scoped_target_names
                result.findings.append(
                    Finding(
                        check="cmake",
                        severity="warning",
                        path=None,
                        target=target_name,
                        message=f"Direct dependency on {dep} may be redundant because a PUBLIC/INTERFACE indirect path also exists.",
                        category="simplicity",
                        subtype="redundant-direct-link",
                        origin="cmake-text-graph",
                        confidence="medium",
                        location_scope_class="in_scope" if in_scope else "out_of_scope",
                        trigger_scope_class="in_scope" if in_scope else "out_of_scope",
                        trigger_target=target_name,
                    )
                )

    # --- Link-visibility check ---
    # For each non-INTERFACE target, detect mismatches between declared link scopes
    # and actual public header usage:
    #   - PUBLIC declared but no public header includes from it -> should be PRIVATE
    #   - PRIVATE declared but a public header includes from it -> should be PUBLIC
    header_exts = set(profile.header_extensions)
    visibility_targets_checked = 0

    for target in targets:
        if not target.declared_link_scopes:
            continue
        if target.target_type and target.target_type.upper() == "INTERFACE_LIBRARY":
            continue

        public_headers = _collect_public_headers_for_target(target, header_exts)
        if not public_headers:
            continue

        visibility_targets_checked += 1

        # Build include dirs for resolving includes within public headers
        target_include_dirs: list[Path] = list(target.include_dirs)
        for dep_name in target.declared_links:
            dep_target = context.targets.get(dep_name)
            if dep_target is not None:
                target_include_dirs.extend(dep_target.include_dirs)

        # Collect the set of dependency targets that public headers actually include from.
        # Use _provider_targets_for_path (source_dir-based) instead of _exporting_targets_for_path
        # (include_dirs-based) to avoid false matches from transitive include directories.
        public_header_deps: set[str] = set()
        for header in public_headers:
            try:
                includes = _quoted_includes_from_file(header)
            except (OSError, IOError):
                continue
            for include in includes:
                resolved = _resolve_include_path(include, header, target_include_dirs)
                if resolved is None:
                    continue
                # Use source_dir-based ownership: a target "provides" a header if
                # the header is in its sources list or under its source_dir
                for dep_name, dep_target in context.targets.items():
                    if dep_name == target.name:
                        continue
                    if dep_target.owns_path(resolved):
                        public_header_deps.add(dep_name)

        # Compare declared scopes against actual public header dependencies
        for dep_name, declared_scope in target.declared_link_scopes.items():
            if dep_name not in context.targets:
                continue
            scope_lower = declared_scope.lower()
            in_scope = target.name in scoped_target_names

            if scope_lower in ("public", "interface") and dep_name not in public_header_deps:
                result.findings.append(
                    Finding(
                        check="cmake",
                        severity="warning",
                        path=None,
                        target=target.name,
                        message=f"Dependency on {dep_name} is declared {declared_scope.upper()} but no public header includes from it. Consider PRIVATE.",
                        category="visibility",
                        subtype="should-be-private",
                        origin="cmake-link-visibility",
                        confidence="medium",
                        location_scope_class="in_scope" if in_scope else "out_of_scope",
                        trigger_scope_class="in_scope" if in_scope else "out_of_scope",
                        trigger_target=target.name,
                    )
                )
            elif scope_lower == "private" and dep_name in public_header_deps:
                result.findings.append(
                    Finding(
                        check="cmake",
                        severity="warning",
                        path=None,
                        target=target.name,
                        message=f"Dependency on {dep_name} is declared PRIVATE but a public header includes from it. Consider PUBLIC.",
                        category="visibility",
                        subtype="should-be-public",
                        origin="cmake-link-visibility",
                        confidence="medium",
                        location_scope_class="in_scope" if in_scope else "out_of_scope",
                        trigger_scope_class="in_scope" if in_scope else "out_of_scope",
                        trigger_target=target.name,
                    )
                )

    if visibility_targets_checked > 0:
        result.notes.append(f"Link-visibility check analyzed {visibility_targets_checked} non-INTERFACE targets with declared link scopes.")

    return result


def run_iwyu_check(
    *,
    scope: Scope,
    context: BuildContext,
    profile: Profile,
    snapshot: EnvironmentSnapshot,
    jobs: int,
) -> CheckResult:
    _ = profile
    result = CheckResult(kind="iwyu", detail_limit=30)

    iwyu_binary = _optional_tool_executable(snapshot, "include-what-you-use")
    if iwyu_binary is None:
        result.notes.append("include-what-you-use not available; skipping IWYU analysis.")
        return result

    commands = compile_commands_for_scope(context, scope)
    result.notes.append(f"Analyzing {len(commands)} translation units with IWYU.")
    result.notes.append(f"Using IWYU binary: {iwyu_binary}")

    if not commands:
        result.notes.append("No compile commands in scope; nothing to analyze.")
        return result

    def analyze_one(cmd: CompileCommand) -> list[Finding]:
        findings: list[Finding] = []
        tokens = shlex.split(cmd.command)
        if not tokens:
            return findings
        tokens[0] = iwyu_binary

        # Add IWYU-specific flags
        iwyu_cmd = tokens[:1] + ["-Xiwyu", "--no_comments", "-Xiwyu", "--max_line_length=200"] + tokens[1:]

        try:
            proc = run_command(iwyu_cmd, cwd=cmd.directory, check=False, timeout=120)
        except RuntimeError:
            return findings
        # IWYU writes analysis to stderr
        output = proc.stderr or ""

        file_findings = _parse_iwyu_output(output, cmd.file, scope)
        findings.extend(file_findings)
        return findings

    all_results = _parallel_map(analyze_one, commands, jobs)

    # Collect all findings, then deduplicate across translation units
    raw_findings: list[Finding] = []
    for item in all_results:
        if isinstance(item, Exception):
            result.notes.append(f"IWYU analysis error: {item}")
            continue
        raw_findings.extend(item)

    # Deduplicate: the same header finding might be reported from multiple TUs
    seen_keys: set[tuple[str, str, str]] = set()
    for finding in raw_findings:
        key = (str(finding.path), finding.message, finding.subtype or "")
        if key in seen_keys:
            continue
        seen_keys.add(key)
        result.findings.append(finding)

    result.notes.append(f"IWYU analysis produced {len(result.findings)} unique findings ({len(raw_findings)} total before deduplication).")
    return result


def _parse_iwyu_output(output: str, source_file: Path, scope: Scope) -> list[Finding]:
    """Parse IWYU stderr output into findings."""
    findings: list[Finding] = []
    current_file: str | None = None
    current_section: str | None = None  # "add", "remove", or None

    for line in output.splitlines():
        line = line.rstrip()

        # Detect section headers
        if line.endswith(" should add these lines:"):
            current_file = line.rsplit(" should add", 1)[0].strip()
            current_section = "add"
            continue
        elif line.endswith(" should remove these lines:"):
            current_file = line.rsplit(" should remove", 1)[0].strip()
            current_section = "remove"
            continue
        elif line.startswith("The full include-list for "):
            current_section = None
            continue
        elif line == "---":
            current_file = None
            current_section = None
            continue

        if current_file is None or current_section is None:
            continue

        # Skip empty lines
        stripped = line.strip()
        if not stripped:
            continue

        file_path = Path(current_file)
        in_scope = scope.contains(file_path)

        if current_section == "remove" and stripped.startswith("- "):
            # "- #include "Foo.hh"  // lines X-Y"
            include_text = stripped[2:].split("//")[0].strip()
            findings.append(Finding(
                check="iwyu",
                severity="warning",
                path=file_path,
                message=f"Unnecessary include: {include_text}",
                category="iwyu",
                subtype="unnecessary-include",
                origin="include-what-you-use",
                confidence="high",
                trigger_path=source_file,
                location_scope_class="in_scope" if in_scope else "out_of_scope",
                trigger_scope_class="in_scope" if scope.contains(source_file) else "out_of_scope",
                trigger_target=None,
            ))
        elif current_section == "add":
            if stripped.startswith("#include"):
                findings.append(Finding(
                    check="iwyu",
                    severity="warning",
                    path=file_path,
                    message=f"Missing include: {stripped}",
                    category="iwyu",
                    subtype="missing-include",
                    origin="include-what-you-use",
                    confidence="high",
                    trigger_path=source_file,
                    location_scope_class="in_scope" if in_scope else "out_of_scope",
                    trigger_scope_class="in_scope" if scope.contains(source_file) else "out_of_scope",
                    trigger_target=None,
                ))
            elif "class " in stripped or "struct " in stripped:
                findings.append(Finding(
                    check="iwyu",
                    severity="warning",
                    path=file_path,
                    message=f"Missing forward declaration: {stripped}",
                    category="iwyu",
                    subtype="missing-forward-decl",
                    origin="include-what-you-use",
                    confidence="high",
                    trigger_path=source_file,
                    location_scope_class="in_scope" if in_scope else "out_of_scope",
                    trigger_scope_class="in_scope" if scope.contains(source_file) else "out_of_scope",
                    trigger_target=None,
                ))

    return findings


def _tool_executable(snapshot: EnvironmentSnapshot, name: str) -> str:
    status = snapshot.get_tool_status(name)
    if status.executable is None:
        raise RuntimeError(f"{name} executable is unavailable after environment validation.")
    return status.executable


def _optional_tool_executable(snapshot: EnvironmentSnapshot, name: str) -> str | None:
    """Return the executable path for an optional tool, or None if unavailable."""
    try:
        status = snapshot.get_tool_status(name)
    except KeyError:
        return None
    if not status.found or status.executable is None:
        return None
    return status.executable


def _scan_deps_for_file(compile_command: CompileCommand, scanner_binary: str, timeout: int = 60) -> list[Path]:
    """Use clang-scan-deps to get the complete include dependency list for a source file.

    Returns a list of absolute paths to all transitively included headers.
    """
    cmd_tokens = shlex.split(compile_command.command)
    if cmd_tokens:
        # Replace compiler with clang++ (clang-scan-deps needs clang-compatible driver)
        cmd_tokens[0] = "clang++"

    scan_cmd = [scanner_binary, "--", *cmd_tokens]
    try:
        result = run_command(scan_cmd, cwd=compile_command.directory, check=False, timeout=timeout)
    except RuntimeError:
        return []

    if result.returncode != 0:
        return []

    # clang-scan-deps in default mode outputs one dependency per line
    deps: list[Path] = []
    for line in (result.stdout or "").splitlines():
        line = line.strip()
        if not line or line.endswith(":"):
            continue
        dep_path = Path(line).resolve()
        if dep_path.exists():
            deps.append(dep_path)
    return deps


def _scan_deps_batch(
    compile_commands: list[CompileCommand], scanner_binary: str, jobs: int = 1
) -> dict[Path, list[Path]]:
    """Scan include dependencies for multiple source files.

    Returns a dict mapping source file -> list of included header paths.
    """
    def scan_one(cmd: CompileCommand) -> tuple[Path, list[Path]]:
        deps = _scan_deps_for_file(cmd, scanner_binary)
        return (cmd.file, deps)

    results = _parallel_map(scan_one, compile_commands, jobs)
    dep_map: dict[Path, list[Path]] = {}
    for item in results:
        if isinstance(item, Exception):
            continue
        source, deps = item
        dep_map[source] = deps
    return dep_map


def _collect_public_headers_for_target(target: CMakeTarget, header_exts: set[str]) -> list[Path]:
    """Collect header files that reside directly under the target's own source directory.

    Only considers the target's ``source_dir`` — NOT transitive include directories
    from dependencies (which are mixed into ``include_dirs`` by the CMake File API).
    This avoids treating dependency headers as the target's own public headers.
    Only files with extensions matching ``header_exts`` are returned.
    """
    headers: list[Path] = []
    if target.source_dir is None or not target.source_dir.is_dir():
        return headers
    seen: set[Path] = set()
    for child in target.source_dir.iterdir():
        if not child.is_file():
            continue
        if child.suffix not in header_exts:
            continue
        resolved = child.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        headers.append(resolved)
    return sorted(headers)


def _should_run_on_demand_tidy_pass(tidy_pass: TidyPass, commands: list[CompileCommand], headers: list[Path]) -> bool:
    if tidy_pass.runner != "clang-tidy-header":
        return True
    if not headers:
        return False
    if commands:
        return False
    return True



def _run_tidy_pass(
    tidy_pass: TidyPass,
    *,
    repo_root: Path,
    scope: Scope,
    context: BuildContext,
    snapshot: EnvironmentSnapshot,
    config_path: Path,
    commands: list[CompileCommand],
    headers: list[Path],
    profile: Profile | None = None,
    jobs: int = 1,
) -> TidyPassOutcome:
    if tidy_pass.on_demand and not _should_run_on_demand_tidy_pass(tidy_pass, commands, headers):
        outcome = TidyPassOutcome(tidy_pass=tidy_pass)
        outcome.notes.append(
            f"Pass {tidy_pass.name}: skipped on-demand pass because compile commands already cover the scope."
        )
        return outcome
    if tidy_pass.runner == "clang-tidy-tu":
        return _run_clang_tidy_tu_pass(
            tidy_pass,
            repo_root=repo_root,
            scope=scope,
            context=context,
            snapshot=snapshot,
            config_path=config_path,
            commands=commands,
            jobs=jobs,
        )
    if tidy_pass.runner == "clang-tidy-header":
        return _run_clang_tidy_header_pass(
            tidy_pass,
            repo_root=repo_root,
            scope=scope,
            context=context,
            snapshot=snapshot,
            config_path=config_path,
            headers=headers,
        )
    if tidy_pass.runner == "clang-frontend":
        return _run_clang_frontend_pass(
            tidy_pass,
            repo_root=repo_root,
            scope=scope,
            snapshot=snapshot,
            commands=commands,
            profile=profile,
            jobs=jobs,
        )
    raise ValueError(f"Unsupported tidy pass runner: {tidy_pass.runner}")



def _run_clang_tidy_tu_pass(
    tidy_pass: TidyPass,
    *,
    repo_root: Path,
    scope: Scope,
    context: BuildContext,
    snapshot: EnvironmentSnapshot,
    config_path: Path,
    commands: list[CompileCommand],
    jobs: int = 1,
) -> TidyPassOutcome:
    outcome = TidyPassOutcome(tidy_pass=tidy_pass)
    clang_tidy_status = snapshot.get_tool_status("clang-tidy")
    if clang_tidy_status.executable is None:
        raise RuntimeError("clang-tidy executable is unavailable after environment validation.")

    header_filter = _build_header_filter(scope)
    config_args = _clang_tidy_config_args(config_path, clang_tidy_status.executable, clang_tidy_status.version_text)
    checks_arg = tidy_pass.checks_arg

    outcome.notes.append(f"Pass {tidy_pass.name}: using {clang_tidy_status.executable}")
    outcome.notes.append(f"Pass {tidy_pass.name}: tool selection policy={clang_tidy_status.selection_policy}")
    if header_filter:
        outcome.notes.append(f"Pass {tidy_pass.name}: header filter={header_filter}")
    if checks_arg is not None:
        outcome.notes.append(f"Pass {tidy_pass.name}: checks={checks_arg}")

    def _run_one_tu(command: CompileCommand) -> tuple[list[Finding], list[str], list[CompileCommand]]:
        tidy_cmd = [
            clang_tidy_status.executable,
            str(command.file),
            f"-p={context.build_dir}",
            "--quiet",
            *config_args,
        ]
        if checks_arg is not None:
            tidy_cmd.append(f"--checks={checks_arg}")
        if header_filter:
            tidy_cmd.append(f"--header-filter={header_filter}")
        output = run_command(tidy_cmd, cwd=repo_root)
        text = "\n".join(part for part in [output.stdout, output.stderr] if part)
        parsed = _parse_clang_tidy_output(text, scope, command, origin=tidy_pass.name)
        local_findings = list(parsed.findings)
        local_notes: list[str] = []
        local_candidates: list[CompileCommand] = []

        should_run_fallback = command.file.suffix == ".cc" and parsed.parsed_count == 0
        if parsed.suppression_note is not None:
            local_notes.append(parsed.suppression_note)
            should_run_fallback = command.file.suffix == ".cc"
        if should_run_fallback:
            local_candidates.append(command)
        return local_findings, local_notes, local_candidates

    tu_results = _parallel_map(_run_one_tu, commands, jobs)
    for tu_result in tu_results:
        if isinstance(tu_result, Exception):
            outcome.notes.append(f"Tidy TU pass error: {tu_result}")
            continue
        findings, notes, candidates = tu_result
        outcome.findings.extend(findings)
        outcome.notes.extend(notes)
        outcome.candidate_commands.extend(candidates)
        outcome.commands_run += 1

    if outcome.candidate_commands:
        outcome.fallback_candidates = len(outcome.candidate_commands)
        outcome.notes.append(
            f"Pass {tidy_pass.name}: queued {outcome.fallback_candidates} source translation units for possible native fallback."
        )
    return outcome



def _run_clang_tidy_header_pass(
    tidy_pass: TidyPass,
    *,
    repo_root: Path,
    scope: Scope,
    context: BuildContext,
    snapshot: EnvironmentSnapshot,
    config_path: Path,
    headers: list[Path],
) -> TidyPassOutcome:
    outcome = TidyPassOutcome(tidy_pass=tidy_pass)
    clang_tidy_status = snapshot.get_tool_status("clang-tidy")
    if clang_tidy_status.executable is None:
        raise RuntimeError("clang-tidy executable is unavailable after environment validation.")

    config_args = _clang_tidy_config_args(config_path, clang_tidy_status.executable, clang_tidy_status.version_text)
    checks_arg = tidy_pass.checks_arg

    outcome.notes.append(f"Pass {tidy_pass.name}: header naming pass enabled for {len(headers)} headers.")
    if checks_arg is not None:
        outcome.notes.append(f"Pass {tidy_pass.name}: checks={checks_arg}")

    for header in headers:
        header_cmd = [
            clang_tidy_status.executable,
            str(header),
            f"-p={context.build_dir}",
            "--quiet",
            *config_args,
        ]
        if checks_arg is not None:
            header_cmd.append(f"--checks={checks_arg}")
        header_cmd.append(f"--header-filter={_build_exact_header_filter(header)}")
        output = run_command(header_cmd, cwd=repo_root)
        text = "\n".join(part for part in [output.stdout, output.stderr] if part)
        pseudo_command = CompileCommand(file=header, directory=repo_root, command="header-pass")
        parsed = _parse_clang_tidy_output(text, scope, pseudo_command, origin=tidy_pass.name)
        for finding in parsed.findings:
            if finding.subtype == "readability-identifier-naming":
                outcome.findings.append(finding)
        outcome.commands_run += 1
    outcome.notes.append(f"Pass {tidy_pass.name}: ran for {outcome.commands_run} scope-local headers.")
    return outcome



def _run_clang_frontend_pass(
    tidy_pass: TidyPass,
    *,
    repo_root: Path,
    scope: Scope,
    snapshot: EnvironmentSnapshot,
    commands: list[CompileCommand],
    profile: Profile | None = None,
    jobs: int = 1,
) -> TidyPassOutcome:
    outcome = TidyPassOutcome(tidy_pass=tidy_pass)
    clangxx = _tool_executable(snapshot, "clang++")
    source_exts = set(profile.source_extensions) if profile else {".cc"}
    outcome.notes.append(f"Pass {tidy_pass.name}: using {clangxx} for explicit syntax-only frontend checks.")
    outcome.notes.append("Pass clang-frontend: this complements clang-tidy by surfacing raw Clang frontend diagnostics.")

    source_commands = [command for command in commands if command.file.suffix in source_exts]

    def _run_one_frontend(command: CompileCommand) -> list[Finding]:
        frontend_cmd = _build_source_syntax_command(command, compiler_override=clangxx, extra_flags=GENERIC_WARNING_FLAG_TOKENS)
        output = run_command(frontend_cmd, cwd=command.directory)
        text = "\n".join(part for part in [output.stdout, output.stderr] if part)
        return _parse_compiler_output(text, scope, command, repo_root, origin=tidy_pass.name)

    frontend_results = _parallel_map(_run_one_frontend, source_commands, jobs)
    for frontend_result in frontend_results:
        if isinstance(frontend_result, Exception):
            outcome.notes.append(f"Clang frontend pass error: {frontend_result}")
            continue
        outcome.findings.extend(frontend_result)
        outcome.commands_run += 1
    outcome.notes.append(f"Pass {tidy_pass.name}: ran for {outcome.commands_run} source translation units.")
    return outcome



def _run_native_compiler_pass(
    tidy_pass: TidyPass,
    *,
    repo_root: Path,
    scope: Scope,
    snapshot: EnvironmentSnapshot,
    commands: list[CompileCommand],
    profile: Profile | None = None,
    jobs: int = 1,
) -> TidyPassOutcome:
    outcome = TidyPassOutcome(tidy_pass=tidy_pass)
    compiler = _tool_executable(snapshot, "g++")
    source_exts = set(profile.source_extensions) if profile else {".cc"}
    outcome.notes.append(f"Pass {tidy_pass.name}: using on-demand native fallback compiler {compiler}.")

    source_commands = [command for command in commands if command.file.suffix in source_exts]

    def _run_one_native(command: CompileCommand) -> list[Finding]:
        syntax_cmd = _build_source_syntax_command(command, compiler_override=compiler, extra_flags=GENERIC_WARNING_FLAG_TOKENS)
        output = run_command(syntax_cmd, cwd=command.directory)
        text = "\n".join(part for part in [output.stdout, output.stderr] if part)
        return _parse_compiler_output(text, scope, command, repo_root, origin=tidy_pass.name)

    native_results = _parallel_map(_run_one_native, source_commands, jobs)
    for native_result in native_results:
        if isinstance(native_result, Exception):
            outcome.notes.append(f"Native compiler pass error: {native_result}")
            continue
        if native_result:
            outcome.findings.extend(native_result)
        outcome.commands_run += 1
    outcome.notes.append(
        f"Pass {tidy_pass.name}: ran for {outcome.commands_run} queued source translation units; "
        f"{len(outcome.findings)} findings were reported."
    )
    return outcome



def _dedupe_findings(findings: list[Finding], tidy_passes: tuple[TidyPass, ...] = ()) -> list[Finding]:
    deduped: list[Finding] = []
    best_indexes: dict[tuple[str, int | None, str | None, str, str | None, str | None], int] = {}
    priority_map = {tidy_pass.name: tidy_pass.dedupe_priority for tidy_pass in tidy_passes}
    for finding in findings:
        key = (
            str(finding.path) if finding.path else '',
            finding.line,
            str(finding.trigger_path) if finding.trigger_path else None,
            finding.message,
            finding.category,
            finding.subtype,
        )
        existing_index = best_indexes.get(key)
        if existing_index is None:
            best_indexes[key] = len(deduped)
            deduped.append(finding)
            continue

        existing = deduped[existing_index]
        existing_priority = priority_map.get(existing.origin or "", 0)
        current_priority = priority_map.get(finding.origin or "", 0)
        if current_priority > existing_priority:
            deduped[existing_index] = finding
    return deduped


def _collect_cpp_files(scope: Scope, profile: Profile | None = None) -> list[Path]:
    files: list[Path] = []
    if profile is not None:
        allowed_suffixes = set(profile.source_extensions + profile.header_extensions)
    else:
        allowed_suffixes = {".cc", ".hh"}
    for root in scope.resolved_paths:
        if root.is_file() and root.suffix in allowed_suffixes:
            files.append(root)
            continue
        if root.is_dir():
            for path in root.rglob("*"):
                if "__pycache__" in path.parts:
                    continue
                if path.suffix in allowed_suffixes:
                    files.append(path)
    return sorted({path.resolve() for path in files})


def _collect_scope_headers(scope: Scope, profile: Profile | None = None) -> list[Path]:
    header_exts = set(profile.header_extensions) if profile else {".hh"}
    return [path for path in _collect_cpp_files(scope, profile) if path.suffix in header_exts]



def _dedupe_compile_commands(commands: list[CompileCommand]) -> list[CompileCommand]:
    deduped: list[CompileCommand] = []
    seen: set[Path] = set()
    for command in commands:
        if command.file in seen:
            continue
        seen.add(command.file)
        deduped.append(command)
    return deduped



def _has_findings_for_trigger_from_other_passes(findings: list[Finding], trigger_path: Path, pass_name: str) -> bool:
    return any(finding.trigger_path == trigger_path and finding.origin != pass_name for finding in findings)



def _suppressed_tidy_note(text: str, command: CompileCommand, repo_root: Path) -> str | None:
    generated_count = None
    suppressed_count = None
    suppressed_detail = None
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        generated_match = SUPPRESSED_WARNING_RE.match(line)
        if generated_match is not None:
            generated_count = int(generated_match.group(1))
            continue
        suppressed_match = SUPPRESSED_SUMMARY_RE.match(line)
        if suppressed_match is not None:
            suppressed_count = int(suppressed_match.group(1))
            suppressed_detail = suppressed_match.group(2).strip()

    if generated_count is None and suppressed_count is None:
        return None

    tu_path = relative_to_repo(command.file, repo_root)
    if suppressed_count is not None and suppressed_detail:
        return (
            f"TU analyzed with zero parsed clang-tidy findings: {tu_path}. clang-tidy reported "
            f"{generated_count or suppressed_count} generated warnings and suppressed {suppressed_count} "
            f"({suppressed_detail})."
        )
    return (
        f"TU analyzed with zero parsed clang-tidy findings: {tu_path}. "
        f"clang-tidy reported {generated_count} generated warnings suppressed by --header-filter scope restriction; "
        f"these are warnings in headers outside the checked scope and are expected."
    )



def _parse_clang_tidy_output(text: str, scope: Scope, command: CompileCommand, *, origin: str) -> ParsedDiagnosticSet:
    findings: list[Finding] = []
    parsed_count = 0
    for raw_line in text.splitlines():
        match = DIAGNOSTIC_LINE_RE.match(raw_line.strip())
        if not match:
            continue

        path = _resolve_diagnostic_path(match.group(1), command)
        severity_text = match.group(4)
        severity = "error" if severity_text == "error" else "warning"
        message_text = match.group(5).strip()
        checks = _extract_diagnostic_checks(message_text)
        message = _strip_diagnostic_suffix(message_text)
        category, subtype, _ = _classify_tidy_diagnostic(checks)

        if severity_text == "note":
            if findings and _should_attach_followup(findings[-1], path, int(match.group(2)), command.file):
                findings[-1].message = f"{findings[-1].message} | note: {message}"
            continue

        if _is_analyzer_trace_message(message):
            if findings and _should_attach_followup(findings[-1], path, int(match.group(2)), command.file):
                findings[-1].message = f"{findings[-1].message} | trace: {message}"
            continue

        parsed_count += 1
        finding = Finding(
            check="tidy",
            severity=severity,
            path=path,
            line=int(match.group(2)),
            message=message,
            category=category,
            subtype=subtype,
            origin=origin,
            confidence="high",
            location_scope_class="in_scope" if scope.contains(path) else "out_of_scope",
            trigger_scope_class="in_scope" if scope.contains(command.file) else "out_of_scope",
            trigger_path=command.file,
            trigger_target=None,
            tags=tuple(checks[:3]),
        )

        if findings and _should_attach_followup(findings[-1], path, int(match.group(2)), command.file):
            if _should_fold_into_previous(findings[-1], finding):
                findings[-1].message = f"{findings[-1].message} | context: {message}"
                continue

        findings.append(finding)
    return ParsedDiagnosticSet(
        findings=findings,
        parsed_count=parsed_count,
        suppression_note=_suppressed_tidy_note(text, command, scope.repo_root),
    )


def _extract_diagnostic_checks(message: str) -> list[str]:
    checks: list[str] = []
    remaining = message
    while True:
        match = CHECK_BRACKET_RE.search(remaining)
        if match is None:
            break
        token = match.group(1).strip()
        if token:
            checks.insert(0, token)
        remaining = remaining[: match.start()].rstrip()
    normalized: list[str] = []
    for item in checks:
        if item.startswith("-"):
            item = item[1:]
        item = item.strip().strip("]")
        if item:
            normalized.append(item)
    return normalized


def _strip_diagnostic_suffix(message: str) -> str:
    cleaned = message
    while True:
        match = CHECK_BRACKET_RE.search(cleaned)
        if match is None:
            return cleaned.strip()
        cleaned = cleaned[: match.start()].rstrip()


def _classify_tidy_diagnostic(checks: list[str]) -> tuple[str, str, str]:
    if not checks:
        return ("clang-tidy", "unclassified", "clang-tidy")

    primary = checks[0]
    if primary.startswith("clang-diagnostic-"):
        return ("clang-diagnostic", primary, "compiler-diagnostic")
    if primary == "clang-diagnostic":
        return ("clang-diagnostic", "compiler-diagnostic", "compiler-diagnostic")

    for category, prefixes in TIDY_CATEGORY_GROUPS:
        for check in checks:
            if any(check == prefix or check.startswith(prefix) for prefix in prefixes):
                return (category, check, "clang-tidy")
    return ("clang-tidy", primary, "clang-tidy")


def _is_analyzer_trace_message(message: str) -> bool:
    return any(message.startswith(prefix) for prefix in ANALYZER_TRACE_PREFIXES)


def _should_attach_followup(previous: Finding, path: Path, line: int, trigger_path: Path) -> bool:
    same_path = previous.path == path
    close_line = previous.line is not None and abs(previous.line - line) <= 2
    same_trigger = previous.trigger_path == trigger_path
    return same_path and same_trigger and close_line


def _should_fold_into_previous(previous: Finding, current: Finding) -> bool:
    if previous.origin != current.origin:
        return False
    if previous.category == current.category and previous.subtype == current.subtype:
        return True
    if previous.origin in {"tidy-tu", "analyzer-tu", "tidy-headers"} and previous.category == "bugprone" and current.category == "clang-tidy" and current.subtype == "unclassified":
        return True
    if previous.origin in {"tidy-tu", "analyzer-tu", "tidy-headers"} and previous.category == "clang-analyzer" and current.category == "clang-tidy" and current.subtype == "unclassified":
        return True
    return False


def _build_header_filter(scope: Scope) -> str:
    patterns: list[str] = []
    for path in scope.resolved_paths:
        escaped = re.escape(str(path))
        if path.is_dir():
            patterns.append(f"{escaped}(/.*)?")
        else:
            patterns.append(escaped)
    if not patterns:
        return ""
    return f"^(?:{'|'.join(patterns)})$"


def _build_exact_header_filter(header: Path) -> str:
    return f"^{re.escape(str(header.resolve()))}$"


def _resolve_diagnostic_path(raw_path: str, command: CompileCommand) -> Path:
    path = Path(raw_path)
    if path.is_absolute():
        return path.resolve()
    return (command.directory / path).resolve()


def _clang_tidy_config_args(config_path: Path, executable: str, version_text: str | None) -> list[str]:
    version = parse_version_text(version_text or "")
    if version and version < (12, 0):
        config_text = config_path.read_text(encoding="utf-8")
        return [f"--config={config_text}"]

    if executable not in _config_file_support_cache:
        help_result = run_command([executable, "--help"], cwd=config_path.parent)
        help_output = "\n".join(part for part in [help_result.stdout, help_result.stderr] if part)
        _config_file_support_cache[executable] = "--config-file" in help_output

    if _config_file_support_cache[executable]:
        return [f"--config-file={config_path}"]

    config_text = config_path.read_text(encoding="utf-8")
    return [f"--config={config_text}"]


def _build_source_syntax_command(command: CompileCommand, compiler_override: str | None = None, *, extra_flags: tuple[str, ...] = ()) -> list[str]:
    tokens = shlex.split(command.command)
    syntax_cmd: list[str] = []
    skip_next = False
    skip_compile_input = False
    seen_flags: set[str] = set()
    for index, token in enumerate(tokens):
        if index == 0:
            syntax_cmd.append(compiler_override or token)
            continue
        if skip_next:
            skip_next = False
            continue
        if skip_compile_input:
            skip_compile_input = False
            continue
        if token in {"-o", "-MF", "-MT", "-MQ", "-x"}:
            skip_next = True
            continue
        if token in {"-c", "-S", "-E", "-M", "-MM", "-MD", "-MMD", "-MP", "-MG", "-Winvalid-pch"}:
            continue
        if token.startswith("-o"):
            continue
        if token.startswith("-MF") or token.startswith("-MT") or token.startswith("-MQ"):
            continue
        if token.endswith((".cc", ".cpp", ".cxx", ".c++", ".C")) and Path(token).name == command.file.name:
            continue
        syntax_cmd.append(token)
        seen_flags.add(token)

    for flag in extra_flags:
        if flag not in seen_flags:
            syntax_cmd.append(flag)
    syntax_cmd.extend(["-fsyntax-only", str(command.file)])
    return syntax_cmd



def _parse_compiler_output(
    text: str,
    scope: Scope,
    command: CompileCommand,
    repo_root: Path,
    *,
    origin: str,
) -> list[Finding]:
    findings: list[Finding] = []
    tu_path = relative_to_repo(command.file, repo_root)
    default_tag = "clang-frontend" if origin == "clang-frontend" else "compiler-fallback"
    for raw_line in text.splitlines():
        match = DIAGNOSTIC_LINE_RE.match(raw_line.strip())
        if not match:
            continue

        severity_text = match.group(4)
        if severity_text == "note":
            continue

        path = _resolve_diagnostic_path(match.group(1), command)
        line = int(match.group(2))
        message_text = match.group(5).strip()
        checks = _extract_diagnostic_checks(message_text)
        message = _strip_diagnostic_suffix(message_text)
        category, subtype = _classify_compiler_diagnostic(message, checks)
        location_scope = "in_scope" if scope.contains(path) else "out_of_scope"
        trigger_scope = "in_scope" if scope.contains(command.file) else "out_of_scope"

        findings.append(
            Finding(
                check="tidy",
                severity="error" if severity_text == "error" else "warning",
                path=path,
                line=line,
                message=message,
                category=category,
                subtype=subtype,
                origin=origin,
                confidence="high",
                location_scope_class=location_scope,
                trigger_scope_class=trigger_scope,
                trigger_path=command.file,
                tags=tuple(checks[:3]) or (default_tag,),
            )
        )

    if findings and origin == "native-fallback":
        findings[0].message = f"{findings[0].message} [compiler syntax-only fallback; TU: {tu_path}]"
    elif findings and origin == "clang-frontend":
        findings[0].message = f"{findings[0].message} [clang frontend syntax-only pass; TU: {tu_path}]"
    return findings



def _classify_compiler_diagnostic(message: str, checks: list[str]) -> tuple[str, str]:
    _ = message
    if checks:
        primary = checks[0]
        if primary.startswith("clang-diagnostic-"):
            return ("clang-diagnostic", primary)
        if primary == "clang-diagnostic":
            return ("clang-diagnostic", "compiler-diagnostic")
    return ("clang-diagnostic", "compiler-syntax-only")



def _build_header_syntax_command(trigger_command: CompileCommand, header: Path) -> list[str]:
    tokens = shlex.split(trigger_command.command)
    command: list[str] = []
    skip_next = False
    for token in tokens:
        if skip_next:
            skip_next = False
            continue
        if token == "-o":
            skip_next = True
            continue
        if token == "-c":
            continue
        command.append(token)
    command.extend(["-x", "c++-header", "-fsyntax-only", str(header)])
    return command


def _infer_interface_include_dirs(header: Path, repo_root: Path, profile: Profile) -> set[str]:
    paths = header.resolve().parts
    roots: set[str] = set()
    if not profile.include_inference_roots:
        return roots
    inference_roots = {name: repo_root / name for name in profile.include_inference_roots}
    if "source" not in paths:
        return roots
    header_str = str(header.resolve())
    for name, root_path in inference_roots.items():
        if str(root_path) in header_str:
            roots.add(str(header.parent))
            roots.add(str(root_path))
            for other_name, other_path in inference_roots.items():
                if other_name != name:
                    roots.add(str(other_path))
            break
    return roots


def _clean_header_diagnostic(text: str) -> str:
    lines = []
    skip_next = False
    for line in text.splitlines():
        if skip_next:
            skip_next = False
            continue
        if "#pragma once in main file" in line:
            skip_next = True
            continue
        lines.append(line)
    return "\n".join(lines).strip()


def _build_header_include_first_command(
    *,
    header: Path,
    include_dirs: list[str],
    trigger_command: CompileCommand | None,
    compiler: str,
    repo_root: Path,
) -> tuple[list[str], Path, Path]:
    with tempfile.NamedTemporaryFile(mode="w", suffix=".cc", delete=False, dir=repo_root, encoding="utf-8") as tmp_file:
        tmp_file.write(f'#include "{header.name}"\n\nint main() {{ return 0; }}\n')
        wrapper = Path(tmp_file.name)
    if trigger_command is not None:
        command = _build_header_include_first_syntax_command(trigger_command, header, wrapper)
        return command, trigger_command.directory, wrapper
    command = [compiler, "-std=c++20", "-fsyntax-only"]
    for include_dir in include_dirs:
        command.extend(["-I", include_dir])
    command.append(str(wrapper))
    return command, repo_root, wrapper


def _build_header_include_first_syntax_command(trigger_command: CompileCommand, header: Path, wrapper: Path) -> list[str]:
    tokens = shlex.split(trigger_command.command)
    command: list[str] = []
    skip_next = False
    for index, token in enumerate(tokens):
        if index == 0:
            command.append(token)
            continue
        if skip_next:
            skip_next = False
            continue
        if token in {"-o", "-MF", "-MT", "-MQ", "-x"}:
            skip_next = True
            continue
        if token in {"-c", "-S", "-E", "-M", "-MM", "-MD", "-MMD", "-MP", "-MG", "-Winvalid-pch"}:
            continue
        if token.startswith("-o"):
            continue
        if token.startswith("-MF") or token.startswith("-MT") or token.startswith("-MQ"):
            continue
        if token.endswith((".cc", ".cpp", ".cxx", ".c++", ".C")) and Path(token).name == trigger_command.file.name:
            continue
        command.append(token)
    command.extend(["-I", str(header.parent), "-fsyntax-only", str(wrapper)])
    return command


def _extract_include_roots(target: CMakeTarget) -> set[str]:
    roots: set[str] = set()
    if target.source_dir is not None:
        roots.add(str(target.source_dir))
        for parent in target.source_dir.parents:
            roots.add(str(parent))
    for source in target.sources:
        roots.add(str(source.parent))
    return roots


def _extract_include_flags(command_text: str, compile_directory: Path) -> list[Path]:
    tokens = shlex.split(command_text)
    include_dirs: list[Path] = []
    idx = 0
    while idx < len(tokens):
        token = tokens[idx]
        if token in {"-I", "-isystem"} and idx + 1 < len(tokens):
            value = tokens[idx + 1]
            include_path = Path(value)
            include_dirs.append(include_path.resolve() if include_path.is_absolute() else (compile_directory / include_path).resolve())
            idx += 2
            continue
        if token.startswith("-I") and len(token) > 2:
            value = token[2:]
            include_path = Path(value)
            include_dirs.append(include_path.resolve() if include_path.is_absolute() else (compile_directory / include_path).resolve())
        idx += 1
    return include_dirs


def _quoted_includes_from_file(path: Path) -> list[str]:
    content = path.read_text(encoding="utf-8", errors="replace")
    return re.findall(r'#include\s+"([^"]+)"', content)


def _resolve_include_path(include: str, source: Path, include_dirs: list[Path]) -> Path | None:
    local_candidate = (source.parent / include).resolve()
    if local_candidate.exists():
        return local_candidate
    for include_dir in include_dirs:
        candidate = (include_dir / include).resolve()
        if candidate.exists():
            return candidate
    return None


def _provider_targets_for_path(resolved_include: Path, targets: dict[str, CMakeTarget], consumer_target: str, declared_graph: dict[str, list[str]]) -> set[str]:
    providers: set[str] = set()
    for target_name, target in targets.items():
        if target_name == consumer_target or target_name in declared_graph.get(consumer_target, []):
            continue
        if resolved_include in target.sources:
            providers.add(target_name)
            continue
        if target.source_dir and (resolved_include == target.source_dir or target.source_dir in resolved_include.parents):
            providers.add(target_name)
    return providers


def _exporting_targets_for_path(resolved_include: Path, targets: dict[str, CMakeTarget], consumer_target: str) -> set[str]:
    exporters: set[str] = set()
    for target_name, target in targets.items():
        if target_name == consumer_target:
            continue
        for include_dir in target.include_dirs:
            if resolved_include == include_dir or include_dir in resolved_include.parents:
                exporters.add(target_name)
                break
    return exporters


def _transitive_deps(root: str, declared_graph: dict[str, list[str]]) -> set[str]:
    visited: set[str] = set()
    stack = [root]
    while stack:
        current = stack.pop()
        for dep in declared_graph.get(current, []):
            if dep in visited:
                continue
            visited.add(dep)
            stack.append(dep)
    return visited


def _detect_cycles(adjacency: dict[str, list[str]]) -> list[list[str]]:
    cycles: list[list[str]] = []
    temp: set[str] = set()
    perm: set[str] = set()
    stack: list[str] = []

    def visit(node: str) -> None:
        if node in perm:
            return
        if node in temp:
            if node in stack:
                index = stack.index(node)
                cycles.append(stack[index:] + [node])
            return
        temp.add(node)
        stack.append(node)
        for dep in adjacency.get(node, []):
            if dep in adjacency:
                visit(dep)
        stack.pop()
        temp.remove(node)
        perm.add(node)

    for node in adjacency:
        visit(node)
    unique: list[list[str]] = []
    seen: set[tuple[str, ...]] = set()
    for cycle in cycles:
        key = tuple(cycle)
        if key not in seen:
            seen.add(key)
            unique.append(cycle)
    return unique


def _has_indirect_path(source: str, direct_dep: str, adjacency: dict[str, list[str]]) -> bool:
    visited: set[str] = set()
    stack = [dep for dep in adjacency.get(source, []) if dep != direct_dep]
    while stack:
        current = stack.pop()
        if current == direct_dep:
            return True
        if current in visited:
            continue
        visited.add(current)
        stack.extend(adjacency.get(current, []))
    return False
