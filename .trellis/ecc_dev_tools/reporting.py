from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

try:
    from .models import CheckResult, EnvironmentSnapshot, ExecutionPlan, Finding, Profile, Scope
    from .utils import relative_to_repo
except ImportError:
    from models import CheckResult, EnvironmentSnapshot, ExecutionPlan, Finding, Profile, Scope
    from utils import relative_to_repo


DEFAULT_TIDY_SUMMARY_ORDER = [
    "bugprone",
    "clang-analyzer",
    "modernize",
    "performance",
    "readability",
    "misc",
    "cppcoreguidelines",
    "readability-identifier-naming",
    "clang-diagnostic",
]


def format_environment(snapshot: EnvironmentSnapshot) -> str:
    lines = [
        "Environment",
        f"- Build directory: {snapshot.build_dir}",
        f"- CPU threads: total={snapshot.total_cpus}, idle-estimate={snapshot.idle_threads_estimate}, jobs={snapshot.jobs}",
        "- Tool checks:",
    ]
    for status in snapshot.tool_statuses:
        state = "OK" if status.ok else "FAIL"
        version = f" ({status.version_text})" if status.version_text else ""
        requirement = "required" if status.required else "optional"
        executable = f" -> {status.executable}" if status.executable else ""
        policy = f" policy={status.selection_policy}"
        requested = f", requested={status.requested_executable}" if status.requested_executable else ""
        selected = f", selected={status.selected_candidate}" if status.selected_candidate else ""
        lines.append(f"  - [{state}] {status.name}{version}{executable} [{requirement}] {status.message}{policy}{requested}{selected}")
    return "\n".join(lines)


def format_execution_plan(plan: ExecutionPlan, profile: Profile) -> str:
    lines = [
        "Execution plan",
        f"- Profile: {profile.name} - {profile.description}",
        f"- Preset: {plan.validation_preset}",
        f"- Kinds: {', '.join(plan.kinds)}",
        f"- Tidy mode: {plan.tidy_mode}",
        f"- Pass plan: {plan.pass_plan}",
    ]

    if plan.tidy_passes:
        lines.append("- Tidy passes:")
        for tidy_pass in plan.tidy_passes:
            checks_text = f", checks={tidy_pass.checks_arg}" if tidy_pass.checks_arg else ""
            on_demand = ", on-demand" if tidy_pass.on_demand else ""
            lines.append(
                "  - "
                f"{tidy_pass.name}: tool={tidy_pass.tool_name}, runner={tidy_pass.runner}, "
                f"priority={tidy_pass.dedupe_priority}{on_demand}{checks_text}"
            )
    else:
        lines.append("- Tidy passes: none")

    if plan.compatibility_notes:
        lines.append("- Compatibility notes:")
        for note in plan.compatibility_notes:
            lines.append(f"  - {note}")
    else:
        lines.append("- Compatibility notes: none")

    return "\n".join(lines)


def format_scope(scope: Scope) -> str:
    lines = ["Scope"]
    for item in scope.relative_items():
        lines.append(f"- {item}")
    return "\n".join(lines)


def format_build_context(refreshed: bool, refresh_reason: str | None, compile_commands_path: Path, reply_dir: Path) -> str:
    lines = ["Build metadata"]
    lines.append(f"- compile_commands: {compile_commands_path}")
    lines.append(f"- file-api reply: {reply_dir}")
    if refreshed:
        lines.append(f"- refreshed: yes ({refresh_reason})")
    else:
        lines.append("- refreshed: no (existing metadata reused)")
    return "\n".join(lines)


def format_check_result(result: CheckResult, repo_root: Path) -> str:
    lines = [f"[{result.kind}] Summary"]
    if result.notes:
        for note in result.notes:
            lines.append(f"- {note}")

    in_scope = sorted(result.in_scope_findings(), key=Finding.sort_key)
    out_of_scope = sorted(result.out_of_scope_findings(), key=Finding.sort_key)
    triggered_in_scope = len(result.triggered_in_scope_findings())
    if triggered_in_scope:
        lines.append(f"- Triggered from in-scope translation units: {triggered_in_scope}")
    lines.extend(_format_finding_bucket("In-scope findings", in_scope, repo_root, result))
    lines.extend(_format_finding_bucket("Out-of-scope findings", out_of_scope, repo_root, result))
    return "\n".join(lines)


def _format_finding_bucket(title: str, findings: list[Finding], repo_root: Path, result: CheckResult) -> list[str]:
    lines = [f"- {title}: {len(findings)}"]
    if not findings:
        return lines

    counts = Counter((finding.severity, finding.category or "general") for finding in findings)
    subtype_counts = Counter((finding.category or "general", finding.subtype or "general") for finding in findings)
    file_counts = Counter(_display_path(finding.path, repo_root) for finding in findings if finding.path is not None)
    trigger_counts = Counter(_display_path(finding.trigger_path, repo_root) for finding in findings if finding.trigger_path is not None)
    confidence_counts = Counter(finding.confidence for finding in findings)

    lines.append("  - Summary by severity/category:")
    for severity, category, count in _ordered_counts(counts, result):
        lines.append(f"    - {severity}/{category}: {count}")

    lines.append("  - Confidence:")
    for confidence in ["high", "medium", "low"]:
        count = confidence_counts.get(confidence, 0)
        if count:
            lines.append(f"    - {confidence}: {count}")

    top_subtypes = sorted(subtype_counts.items(), key=lambda item: (-item[1], item[0][0], item[0][1]))[:8]
    if top_subtypes:
        lines.append("  - Top subtypes:")
        for (category, subtype), count in top_subtypes:
            lines.append(f"    - {category}/{subtype}: {count}")

    top_files = file_counts.most_common(5)
    if top_files:
        lines.append("  - Top files:")
        for file_name, count in top_files:
            lines.append(f"    - {file_name}: {count}")

    top_targets = Counter(finding.target for finding in findings if finding.target).most_common(5)
    if top_targets:
        lines.append("  - Top targets:")
        for target_name, count in top_targets:
            lines.append(f"    - {target_name}: {count}")

    top_triggers = trigger_counts.most_common(5)
    if top_triggers:
        lines.append("  - Top triggering translation units:")
        for file_name, count in top_triggers:
            lines.append(f"    - {file_name}: {count}")

    lines.append("  - Details:")
    for finding in findings[: result.detail_limit]:
        lines.append(f"    - {_format_finding_detail(finding, repo_root)}")
    if len(findings) > result.detail_limit:
        lines.append(f"    - ... {len(findings) - result.detail_limit} more")
    return lines


def _format_finding_detail(finding: Finding, repo_root: Path) -> str:
    location = _display_path(finding.path, repo_root)
    if location and finding.line is not None:
        location = f"{location}:{finding.line}"
    trigger = _display_path(finding.trigger_path, repo_root)

    labels: list[str] = [finding.severity]
    if finding.category:
        labels.append(finding.category)
    if finding.subtype:
        labels.append(finding.subtype)
    if finding.origin:
        labels.append(f"origin={finding.origin}")
    labels.append(f"confidence={finding.confidence}")
    if finding.target:
        labels.append(f"target={finding.target}")
    if finding.trigger_target:
        labels.append(f"trigger-target={finding.trigger_target}")
    if finding.tags:
        labels.append(f"tags={','.join(finding.tags[:3])}")

    location_part = f"location={location}" if location else "location=<none>"
    trigger_part = ""
    if trigger:
        trigger_scope = finding.trigger_scope_class or "unknown"
        trigger_part = f"; trigger={trigger} ({trigger_scope})"
    return f"[{'; '.join(labels)}] {location_part}{trigger_part}: {finding.message}"


def _display_path(path: Path | None, repo_root: Path) -> str:
    if path is None:
        return ""
    return relative_to_repo(path, repo_root)


def _ordered_counts(counts: Counter[tuple[str, str]], result: CheckResult) -> list[tuple[str, str, int]]:
    summary_order = result.summary_categories or DEFAULT_TIDY_SUMMARY_ORDER
    order_map = {name: index for index, name in enumerate(summary_order)}

    ordered = []
    for (severity, category), count in counts.items():
        ordered.append((order_map.get(category, len(order_map)), severity, category, count))
    ordered.sort(key=lambda item: (item[0], item[1], item[2]))
    return [(severity, category, count) for _, severity, category, count in ordered]


def format_exit_summary(results: list[CheckResult], fail_on_findings: bool = True) -> str:
    in_scope_count = sum(len(result.in_scope_findings()) for result in results)
    out_of_scope_count = sum(len(result.out_of_scope_findings()) for result in results)
    triggered_in_scope_count = sum(len(result.triggered_in_scope_findings()) for result in results)
    exit_policy = (
        "- Exit code is 1 when in-scope findings exist; environment/config/runtime errors still return non-zero."
        if fail_on_findings
        else "- Exit code is forced to 0 for reported findings; environment/config/runtime errors still return non-zero."
    )
    return "\n".join(
        [
            "Overall summary",
            f"- In-scope findings: {in_scope_count}",
            f"- Out-of-scope findings: {out_of_scope_count}",
            f"- Triggered by in-scope translation units: {triggered_in_scope_count}",
            exit_policy,
        ]
    )


def format_results_json(results: list[CheckResult], plan: ExecutionPlan, profile: Profile) -> str:
    """Format all check results as a JSON string for machine consumption."""
    findings_list = []
    for result in results:
        for finding in result.findings:
            findings_list.append({
                "check": finding.check,
                "severity": finding.severity,
                "category": finding.category,
                "subtype": finding.subtype,
                "path": str(finding.path) if finding.path else None,
                "line": finding.line,
                "message": finding.message,
                "origin": finding.origin,
                "confidence": finding.confidence,
                "scope": finding.location_scope_class,
                "trigger_path": str(finding.trigger_path) if finding.trigger_path else None,
                "trigger_scope": finding.trigger_scope_class,
                "target": finding.target,
                "tags": list(finding.tags) if finding.tags else [],
            })

    output = {
        "profile": profile.name,
        "preset": plan.validation_preset,
        "tidy_mode": plan.tidy_mode,
        "pass_plan": plan.pass_plan,
        "kinds": list(plan.kinds),
        "summary": {
            "in_scope": sum(1 for f in findings_list if f["scope"] == "in_scope"),
            "out_of_scope": sum(1 for f in findings_list if f["scope"] != "in_scope"),
            "total": len(findings_list),
        },
        "findings": findings_list,
    }
    return json.dumps(output, indent=2, ensure_ascii=False)


def format_results_compiler_style(results: list[CheckResult]) -> str:
    """Format findings in compiler-compatible style: file:line: severity: message."""
    lines = []
    for result in results:
        for finding in sorted(result.findings, key=lambda f: f.sort_key()):
            if finding.location_scope_class != "in_scope":
                continue
            loc = str(finding.path) if finding.path else "<unknown>"
            if finding.line:
                loc = f"{finding.path}:{finding.line}"
            lines.append(f"{loc}: {finding.severity}: [{finding.category}/{finding.subtype}] {finding.message}")
    return "\n".join(lines)
