#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import platform
import sys
from pathlib import Path

try:
    from .build_context import ensure_build_context
    from .checkers import run_selected_checks
    from .environment import TOOL_REQUIREMENTS, inspect_environment, required_tool_names_for_run, validate_required_tools
    from .models import CheckKind, ToolStatus, load_suppressions
    from .profiles import (
        DEFAULT_PASS_PLAN,
        DEFAULT_PROFILE,
        DEFAULT_TIDY_MODE,
        DEFAULT_VALIDATION_PRESET,
        available_validation_presets,
        get_profile,
        resolve_execution_plan,
    )
    from .reporting import (
        format_build_context,
        format_check_result,
        format_environment,
        format_execution_plan,
        format_exit_summary,
        format_results_compiler_style,
        format_results_json,
        format_scope,
    )
    from .scope import build_scope
    from .utils import default_jobs, detect_idle_threads, parse_version_text, python_executable
except ImportError:
    from build_context import ensure_build_context
    from checkers import run_selected_checks
    from environment import TOOL_REQUIREMENTS, inspect_environment, required_tool_names_for_run, validate_required_tools
    from models import CheckKind, ToolStatus, load_suppressions
    from profiles import (
        DEFAULT_PASS_PLAN,
        DEFAULT_PROFILE,
        DEFAULT_TIDY_MODE,
        DEFAULT_VALIDATION_PRESET,
        available_validation_presets,
        get_profile,
        resolve_execution_plan,
    )
    from reporting import (
        format_build_context,
        format_check_result,
        format_environment,
        format_execution_plan,
        format_exit_summary,
        format_results_compiler_style,
        format_results_json,
        format_scope,
    )
    from scope import build_scope
    from utils import default_jobs, detect_idle_threads, parse_version_text, python_executable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repository C++ code quality and CMake dependency checker",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="command")

    check_parser = subparsers.add_parser("check", help="Run selected checks")
    check_parser.add_argument("--path", dest="paths", action="append", required=True, help="Path scope to check (repeatable)")
    check_parser.add_argument("--profile", default=DEFAULT_PROFILE, help="Checker profile name")
    check_parser.add_argument(
        "--preset",
        default=None,
        choices=[preset.name for preset in available_validation_presets()],
        help=(
            "Validation preset to use. Defaults to the selected profile preset. "
            f"Built-in default preset name: {DEFAULT_VALIDATION_PRESET}."
        ),
    )
    check_parser.add_argument(
        "--tidy-mode",
        default=None,
        choices=["deep", "naming"],
        help=f"Resolved clang-tidy mode override (default: profile default / {DEFAULT_TIDY_MODE})",
    )
    check_parser.add_argument(
        "--pass-plan",
        default=None,
        choices=["complete", "legacy", "tidy-only"],
        help=f"Resolved tidy pass plan override (default: profile default / {DEFAULT_PASS_PLAN})",
    )
    check_parser.add_argument(
        "--kinds",
        default=None,
        help="Legacy comma-separated override for checks: format,tidy,headers,cmake,iwyu",
    )
    check_parser.add_argument("--fix", action="store_true", help="Apply fixes where supported (currently format only)")
    check_parser.add_argument("--build-dir", default="build", help="Build directory to reuse or refresh")
    check_parser.add_argument("--jobs", type=int, help="Parallel jobs for metadata refresh")
    check_parser.add_argument("--repo-root", default=None, help="Repository root override")
    check_parser.add_argument("--clang-tidy-binary", default=None, help="Override clang-tidy executable")
    check_parser.add_argument(
        "--output-format",
        default="text",
        choices=["text", "json", "compiler"],
        help="Output format: text (default structured report), json (machine-readable), compiler (file:line: severity: message)",
    )
    check_parser.add_argument(
        "--quiet",
        action="store_true",
        default=False,
        help="Suppress environment and plan details; show only results summary",
    )

    doctor_parser = subparsers.add_parser("doctor", help="Check environment and tool availability")
    doctor_parser.add_argument("--profile", default=DEFAULT_PROFILE, help="Profile to check")
    doctor_parser.add_argument("--build-dir", type=Path, default=Path("build"), help="Build directory")
    doctor_parser.add_argument("--repo-root", type=Path, default=None, help="Repository root override")

    legacy_tidy_mode = check_parser.add_mutually_exclusive_group()
    check_parser.set_defaults(legacy_deep_tidy=None)
    legacy_tidy_mode.add_argument(
        "--deep-tidy",
        dest="legacy_deep_tidy",
        action="store_true",
        help="Legacy compatibility flag; same as --tidy-mode deep",
    )
    legacy_tidy_mode.add_argument(
        "--naming-only",
        dest="legacy_deep_tidy",
        action="store_false",
        help="Legacy compatibility flag; same as --tidy-mode naming",
    )
    check_parser.add_argument(
        "--no-fail-on-findings",
        action="store_true",
        help="Return exit code 0 when findings are reported; environment/config/runtime errors still return non-zero",
    )
    check_parser.add_argument(
        "--show-suppressed",
        action="store_true",
        default=False,
        help="Show findings that were suppressed by the whitelist",
    )
    return parser.parse_args()


def _resolve_repo_root(raw_repo_root: str | None) -> Path:
    if raw_repo_root:
        return Path(raw_repo_root).resolve()
    return Path(__file__).resolve().parents[2]


def _parse_kinds(raw_value: str | None) -> tuple[CheckKind, ...] | None:
    if raw_value is None:
        return None

    supported = {"format", "tidy", "headers", "cmake", "iwyu"}
    items = [item.strip() for item in raw_value.split(",") if item.strip()]
    if not items:
        raise ValueError("At least one check kind must be selected.")
    invalid = [item for item in items if item not in supported]
    if invalid:
        raise ValueError(f"Unsupported check kinds: {', '.join(invalid)}")
    return tuple(items)  # type: ignore[return-value]


_INSTALL_HINTS: dict[str, str] = {
    "cmake": "apt install cmake  (or: pip install cmake)",
    "ninja": "apt install ninja-build",
    "clang-format": "apt install clang-format  (or: apt install clang-tools)",
    "clang-tidy": "apt install clang-tidy  (or: apt install clang-tools)",
    "clang++": "apt install clang",
    "g++": "apt install g++-10",
    "clang-scan-deps": "apt install clang-tools",
    "include-what-you-use": "Build from source matching your clang version. See: https://github.com/include-what-you-use/include-what-you-use",
}


def _format_tool_version(status: ToolStatus) -> str:
    """Extract a concise version string from a ToolStatus.

    Parses the version_text into numeric components and joins them with dots.
    Falls back to '-' when no version can be determined.
    """
    if not status.found or not status.version_text:
        return "-"
    parsed = parse_version_text(status.version_text)
    if parsed:
        return ".".join(str(x) for x in parsed)
    return "-"


def _run_doctor(args: argparse.Namespace) -> int:
    repo_root = _resolve_repo_root(str(args.repo_root) if args.repo_root else None)
    build_dir = (repo_root / args.build_dir).resolve() if not args.build_dir.is_absolute() else args.build_dir.resolve()

    try:
        profile = get_profile(args.profile)
    except ValueError as exc:
        print(f"ERROR: {exc}")
        return 2

    total_cpus = max(1, os.cpu_count() or 1)
    idle_threads = detect_idle_threads(total_cpus)
    computed_jobs = default_jobs(total_cpus, idle_threads)

    snapshot = inspect_environment(repo_root, build_dir, computed_jobs)

    lines: list[str] = []
    lines.append("=== ecc_dev_tools Environment Doctor ===")
    lines.append("")

    # -- Python --
    py_version = sys.version.split()[0] if sys.version else "unknown"
    py_executable = sys.executable or "python3"
    py_ok = sys.version_info >= (3, 10)
    py_status = "OK" if py_ok else "WARN: requires 3.10+"
    lines.append("Python")
    lines.append(f"  Version:  Python {py_version} ({py_executable})")
    lines.append("  Minimum:  3.10+ (for match/case, type unions)")
    lines.append(f"  Status:   {py_status}")
    lines.append("")

    # -- System --
    lines.append("System")
    lines.append(f"  CPU threads:     {total_cpus}")
    lines.append(f"  Idle estimate:   {idle_threads}")
    lines.append(f"  Default jobs:    {computed_jobs}")
    lines.append(f"  Platform:        {platform.platform()}")
    lines.append("")

    # -- Separate required and optional tools --
    required_statuses = [s for s in snapshot.tool_statuses if s.required and s.name != "python"]
    optional_statuses = [s for s in snapshot.tool_statuses if not s.required]

    any_required_missing = False

    # -- Required tools --
    lines.append("Required tools")
    if not required_statuses:
        lines.append("  (none)")
    else:
        # Compute column widths for alignment
        name_width = max(len(s.name) for s in required_statuses)
        version_texts = [_format_tool_version(s) for s in required_statuses]
        ver_width = max(len(v) for v in version_texts)
        exe_texts = [s.executable or "-" for s in required_statuses]
        exe_width = max(len(e) for e in exe_texts)

        for s, ver_text, exe_text in zip(required_statuses, version_texts, exe_texts):
            if s.found and s.ok:
                status_tag = "[OK]"
            elif s.found and not s.ok:
                status_tag = "[WARN]"
            else:
                status_tag = "[MISSING]"
                any_required_missing = True

            line = f"  {s.name:<{name_width}}  {ver_text:<{ver_width}}  -> {exe_text:<{exe_width}}  {status_tag}"
            lines.append(line)

            if status_tag == "[MISSING]":
                hint = _INSTALL_HINTS.get(s.name, "Check your package manager for installation instructions.")
                lines.append(f"  {'':>{name_width}}  Install: {hint}")
            elif status_tag == "[WARN]":
                lines.append(f"  {'':>{name_width}}  Note: {s.message}")

    lines.append("")

    # -- Optional tools --
    # Build a description map from TOOL_REQUIREMENTS
    tool_notes: dict[str, str] = {req.name: req.notes for req in TOOL_REQUIREMENTS}

    lines.append("Optional tools")
    if not optional_statuses:
        lines.append("  (none)")
    else:
        opt_name_width = max(len(s.name) for s in optional_statuses)
        opt_version_texts = [_format_tool_version(s) for s in optional_statuses]
        opt_ver_width = max(len(v) for v in opt_version_texts)
        opt_exe_texts = [s.executable or "-" for s in optional_statuses]
        opt_exe_width = max(len(e) for e in opt_exe_texts)

        for s, ver_text, exe_text in zip(optional_statuses, opt_version_texts, opt_exe_texts):
            if s.found:
                status_tag = "[OK]"
            else:
                status_tag = "[MISSING]"

            note = tool_notes.get(s.name, "")
            note_suffix = f" ({note})" if note else ""

            line = f"  {s.name:<{opt_name_width}}  {ver_text:<{opt_ver_width}}  -> {exe_text:<{opt_exe_width}}  {status_tag}{note_suffix}"
            lines.append(line)

            if status_tag == "[MISSING]":
                hint = _INSTALL_HINTS.get(s.name, "Check your package manager for installation instructions.")
                lines.append(f"  {'':>{opt_name_width}}  Install: {hint}")

    lines.append("")

    # -- Build context --
    compile_commands_path = build_dir / "compile_commands.json"
    file_api_reply_dir = build_dir / ".cmake" / "api" / "v1" / "reply"

    cc_exists = compile_commands_path.is_file()
    reply_exists = file_api_reply_dir.is_dir() and any(file_api_reply_dir.glob("*.json"))

    lines.append("Build context")
    lines.append(f"  compile_commands.json:  {compile_commands_path}  [{'EXISTS' if cc_exists else 'MISSING'}]")
    lines.append(f"  CMake File API reply:   {file_api_reply_dir}  [{'EXISTS' if reply_exists else 'MISSING'}]")
    lines.append("")

    # -- Profile --
    lines.append(f"Profile: {profile.name}")
    lines.append(f"  Scope roots:        {', '.join(profile.scope_roots)}")
    lines.append(f"  Source extensions:   {', '.join(profile.source_extensions)}")
    lines.append(f"  Header extensions:   {', '.join(profile.header_extensions)}")
    lines.append(f"  Build target:        {profile.build_target}")

    print("\n".join(lines))

    if any_required_missing:
        return 1
    return 0


def main() -> int:
    args = parse_args()
    if args.command == "doctor":
        return _run_doctor(args)
    if args.command != "check":
        script_path = Path(__file__).resolve()
        print(f"Usage: {python_executable()} {script_path} <command>")
        print("Commands:")
        print("  check   Run selected checks")
        print("  doctor  Check environment and tool availability")
        return 1

    try:
        repo_root = _resolve_repo_root(args.repo_root)
        profile = get_profile(args.profile)
        plan = resolve_execution_plan(
            profile=profile,
            validation_preset=args.preset,
            tidy_mode=args.tidy_mode,
            pass_plan=args.pass_plan,
            kinds_override=_parse_kinds(args.kinds),
            legacy_deep_tidy=args.legacy_deep_tidy,
        )
        scope = build_scope(repo_root, args.paths)
        build_dir = (repo_root / args.build_dir).resolve() if not Path(args.build_dir).is_absolute() else Path(args.build_dir).resolve()
        snapshot = inspect_environment(repo_root, build_dir, args.jobs, args.clang_tidy_binary)
        validate_required_tools(snapshot, plan.kinds, plan)
        required_tools = required_tool_names_for_run(plan.kinds, plan)
        # Also keep optional tools that are relevant to the selected check kinds
        optional_tools_for_run: set[str] = set()
        if "iwyu" in plan.kinds:
            optional_tools_for_run.add("include-what-you-use")
        if "headers" in plan.kinds:
            optional_tools_for_run.add("clang-scan-deps")
        keep_tools = required_tools | optional_tools_for_run
        snapshot.tool_statuses = [status for status in snapshot.tool_statuses if status.name in keep_tools]

        context = None
        if plan.requires_build_context():
            context = ensure_build_context(repo_root, build_dir, profile, scope, snapshot.jobs)

        if not getattr(args, 'quiet', False) and args.output_format == "text":
            print(f"Executing {len(plan.kinds)} check(s): {', '.join(plan.kinds)}...", file=sys.stderr)

        results = run_selected_checks(
            plan,
            repo_root=repo_root,
            scope=scope,
            context=context,
            profile=profile,
            jobs=snapshot.jobs,
            fix=args.fix,
            snapshot=snapshot,
        )
    except (RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}")
        return 2

    # -- Apply suppression whitelist --
    suppressions = load_suppressions(repo_root)
    suppressed_findings: list[tuple[str, object]] = []  # list of (check_kind, finding) for --show-suppressed
    if suppressions:
        for result in results:
            original = result.findings[:]
            result.findings = [f for f in result.findings if not any(s.matches(f, repo_root) for s in suppressions)]
            suppressed_count = len(original) - len(result.findings)
            if suppressed_count > 0:
                result.notes.append(f"Suppressed {suppressed_count} finding(s) matching whitelist rules.")
                if args.show_suppressed:
                    for f in original:
                        if any(s.matches(f, repo_root) for s in suppressions):
                            suppressed_findings.append((result.kind, f))

    if args.output_format == "json":
        print(format_results_json(results, plan, profile))
    elif args.output_format == "compiler":
        compiler_output = format_results_compiler_style(results)
        if compiler_output:
            print(compiler_output)
    else:
        if not args.quiet:
            print(format_environment(snapshot))
            print()
            print(format_execution_plan(plan, profile))
            print()
            print(format_scope(scope))
            print()
            if context is not None:
                print(format_build_context(context.refreshed, context.refresh_reason, context.compile_commands_path, context.file_api_reply_dir))
            else:
                print("Build metadata\n- not required for selected checks")
            print()
        for result in results:
            print(format_check_result(result, repo_root))
            print()
        print(format_exit_summary(results, fail_on_findings=not args.no_fail_on_findings))

    if suppressed_findings and args.show_suppressed:
        print()
        print(f"Suppressed findings ({len(suppressed_findings)} total):")
        for check_kind, finding in suppressed_findings:
            loc = str(finding.path) if finding.path else "<unknown>"
            if finding.line:
                loc = f"{loc}:{finding.line}"
            cat = finding.category or "general"
            sub = finding.subtype or "general"
            print(f"  [SUPPRESSED] [{check_kind}] {loc}: {finding.severity}: [{cat}/{sub}] {finding.message}")

    has_in_scope_findings = any(result.in_scope_findings() for result in results)
    if has_in_scope_findings and not args.no_fail_on_findings:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
