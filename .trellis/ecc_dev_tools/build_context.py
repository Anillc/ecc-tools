from __future__ import annotations

import json
from pathlib import Path

try:
    from .models import BuildContext, CMakeTarget, CompileCommand, Profile, Scope
    from .utils import read_json, run_command
except ImportError:
    from models import BuildContext, CMakeTarget, CompileCommand, Profile, Scope
    from utils import read_json, run_command


def ensure_build_context(repo_root: Path, build_dir: Path, profile: Profile, scope: Scope, jobs: int) -> BuildContext:
    del scope

    build_dir.mkdir(parents=True, exist_ok=True)
    compile_commands_path = build_dir / "compile_commands.json"
    reply_dir = build_dir / ".cmake" / "api" / "v1" / "reply"
    trace_path = build_dir / ".ecc_dev_tools" / "cmake_trace.json"

    refresh_reason = _needs_refresh(repo_root, build_dir, compile_commands_path, reply_dir, trace_path, profile)
    refreshed = False
    if refresh_reason is not None:
        _refresh_build_metadata(repo_root, build_dir, profile, jobs)
        refreshed = True

    targets = _load_targets(repo_root, reply_dir)
    compile_commands = _load_compile_commands(compile_commands_path)

    trace_links, skipped_trace_generator_expressions = _load_trace_link_metadata(trace_path, repo_root)
    _apply_trace_link_metadata(targets, trace_links)
    declared_graph, cmake_text_graph, cmake_public_graph = _load_declared_graph(targets)

    return BuildContext(
        build_dir=build_dir,
        compile_commands_path=compile_commands_path,
        file_api_reply_dir=reply_dir,
        compile_commands=compile_commands,
        targets=targets,
        declared_graph=declared_graph,
        cmake_text_graph=cmake_text_graph,
        cmake_public_graph=cmake_public_graph,
        profile_name=profile.name,
        skipped_trace_generator_expressions=skipped_trace_generator_expressions,
        refreshed=refreshed,
        refresh_reason=refresh_reason,
    )


def _needs_refresh(
    repo_root: Path,
    build_dir: Path,
    compile_commands_path: Path,
    reply_dir: Path,
    trace_path: Path,
    profile: Profile,
) -> str | None:
    if not compile_commands_path.is_file():
        return "compile_commands.json is missing"
    if not reply_dir.is_dir() or not any(reply_dir.glob("index-*.json")):
        return "CMake File API reply is missing"
    if not trace_path.is_file():
        return "CMake trace metadata is missing"
    cmake_cache = build_dir / "CMakeCache.txt"
    if not cmake_cache.is_file():
        return "CMakeCache.txt is missing"
    root_cmake = repo_root / "CMakeLists.txt"
    json_files = list(reply_dir.glob("*.json"))
    if not json_files:
        return "CMake File API reply directory contains no JSON files"
    newest_reply_mtime = max(path.stat().st_mtime for path in json_files)
    newest_reference_mtime = max(root_cmake.stat().st_mtime, cmake_cache.stat().st_mtime)
    if trace_path.stat().st_mtime < newest_reply_mtime:
        return "CMake trace metadata is older than CMake File API reply"
    if newest_reference_mtime > newest_reply_mtime:
        return "build metadata is older than top-level CMake inputs"
    profile_cmake_paths: list[Path] = []
    for scope_root in profile.scope_roots:
        scope_path = repo_root / scope_root
        if not scope_path.exists():
            continue
        profile_cmake_paths.extend(scope_path.rglob("CMakeLists.txt"))
    if profile_cmake_paths:
        newest_profile_cmake_mtime = max(path.stat().st_mtime for path in profile_cmake_paths)
        if newest_profile_cmake_mtime > newest_reply_mtime:
            return f"build metadata is older than {profile.name} CMake inputs"
    return None


def _refresh_build_metadata(repo_root: Path, build_dir: Path, profile: Profile, jobs: int) -> None:
    query_dir = build_dir / ".cmake" / "api" / "v1" / "query" / "client-ecc-dev-tools"
    query_dir.mkdir(parents=True, exist_ok=True)
    query_file = query_dir / "query.json"
    query_file.write_text(
        '{"requests":[{"kind":"cache","version":2},{"kind":"codemodel","version":2},{"kind":"cmakeFiles","version":1}]}' + "\n",
        encoding="utf-8",
    )

    trace_dir = build_dir / ".ecc_dev_tools"
    trace_dir.mkdir(parents=True, exist_ok=True)
    trace_path = trace_dir / "cmake_trace.json"

    configure_command = [
        "cmake",
        "-S",
        str(repo_root),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "--trace-expand",
        "--trace-format=json-v1",
        f"--trace-redirect={trace_path}",
    ]
    run_command(configure_command, cwd=repo_root, check=True)

    build_command = [
        "cmake",
        "--build",
        str(build_dir),
        "-j",
        str(max(1, jobs)),
        "--target",
        profile.build_target,
    ]
    run_command(build_command, cwd=repo_root, check=True)

    file_api_check = build_dir / ".cmake" / "api" / "v1" / "reply"
    if not file_api_check.is_dir() or not any(file_api_check.glob("index-*.json")):
        file_api_reason = "CMake File API reply is still missing after configure"
        raise RuntimeError(file_api_reason)
    if not trace_path.is_file():
        raise RuntimeError("CMake trace metadata is still missing after configure")


def _latest_reply_index(reply_dir: Path) -> Path:
    indexes = sorted(reply_dir.glob("index-*.json"), key=lambda path: path.stat().st_mtime, reverse=True)
    if not indexes:
        raise RuntimeError(f"No CMake File API index found in {reply_dir}")
    return indexes[0]


def _load_targets(repo_root: Path, reply_dir: Path) -> dict[str, CMakeTarget]:
    index_data = read_json(_latest_reply_index(reply_dir))
    objects = index_data.get("objects", []) if isinstance(index_data, dict) else []
    codemodel_json = None
    for obj in objects:
        if obj.get("kind") == "codemodel":
            codemodel_json = obj.get("jsonFile")
            break
    if not codemodel_json:
        raise RuntimeError("CMake codemodel reply is missing.")

    codemodel = read_json(reply_dir / codemodel_json)
    configurations = codemodel.get("configurations", []) if isinstance(codemodel, dict) else []
    if not configurations:
        raise RuntimeError("CMake codemodel has no configurations.")

    config = configurations[0]
    targets: dict[str, CMakeTarget] = {}
    for item in config.get("targets", []):
        target = CMakeTarget(
            name=item["name"],
            json_file=reply_dir / item["jsonFile"],
        )
        targets[target.name] = target

    for target in targets.values():
        if target.json_file is None:
            continue
        target_json = read_json(target.json_file)
        if not isinstance(target_json, dict):
            continue
        target.target_type = target_json.get("type")
        paths = target_json.get("paths", {})
        source_dir = paths.get("source")
        build_dir = paths.get("build")
        if source_dir is not None:
            source_dir_path = Path(source_dir)
            target.source_dir = source_dir_path.resolve() if source_dir_path.is_absolute() else (repo_root / source_dir_path).resolve()
        if build_dir is not None:
            build_dir_path = Path(build_dir)
            target.build_dir = build_dir_path.resolve() if build_dir_path.is_absolute() else (reply_dir.parents[3] / build_dir_path).resolve()
        target.sources = []
        for source in target_json.get("sources", []):
            if not isinstance(source, dict) or "path" not in source:
                continue
            source_path = Path(source["path"])
            target.sources.append(source_path.resolve() if source_path.is_absolute() else (repo_root / source_path).resolve())
        compile_groups = target_json.get("compileGroups", [])
        include_dirs: list[Path] = []
        for group in compile_groups:
            for include in group.get("includes", []):
                include_path = include.get("path")
                if include_path:
                    include_dirs.append(Path(include_path).resolve())
        target.include_dirs = include_dirs
    return targets


def _load_trace_link_metadata(trace_path: Path, repo_root: Path) -> tuple[dict[str, list[dict[str, str]]], int]:
    parsed: dict[str, list[dict[str, str]]] = {}
    skipped_generator_expressions = 0
    valid_roots = [repo_root / "src" / "operation" / "iCTS", repo_root / "src" / "operation" / "iSTA"]
    with trace_path.open(encoding="utf-8", errors="replace") as trace_file:
        for raw_line in trace_file:
            raw_line = raw_line.strip()
            if not raw_line:
                continue
            try:
                item = json.loads(raw_line)
            except json.JSONDecodeError:
                continue
            if not isinstance(item, dict):
                continue
            if item.get("cmd") != "target_link_libraries":
                continue
            file_text = item.get("file")
            args = item.get("args")
            if not isinstance(file_text, str) or not isinstance(args, list) or not args:
                continue
            file_path = Path(file_text)
            if not file_path.is_absolute():
                file_path = (repo_root / file_path).resolve()
            else:
                file_path = file_path.resolve()
            if not any(root == file_path or root in file_path.parents for root in valid_roots):
                continue
            target_name = args[0]
            if not isinstance(target_name, str):
                continue
            current_scope = ""
            for token in args[1:]:
                if not isinstance(token, str):
                    continue
                if token in {"PRIVATE", "PUBLIC", "INTERFACE"}:
                    current_scope = token.lower()
                    continue
                if token.startswith("$"):
                    skipped_generator_expressions += 1
                    continue
                if token.startswith("-"):
                    continue
                effective_scope = current_scope or "public"
                parsed.setdefault(target_name, []).append({"name": token, "scope": effective_scope})
    return parsed, skipped_generator_expressions


def _apply_trace_link_metadata(targets: dict[str, CMakeTarget], trace_links: dict[str, list[dict[str, str]]]) -> None:
    for target in targets.values():
        target.declared_links = []
        target.declared_link_scopes = {}
    for target_name, links in trace_links.items():
        target = targets.get(target_name)
        if target is None:
            continue
        for link in links:
            dep_name = link["name"]
            scope = link["scope"]
            if dep_name not in target.declared_links:
                target.declared_links.append(dep_name)
            existing_scope = target.declared_link_scopes.get(dep_name)
            target.declared_link_scopes[dep_name] = _merge_link_scope(existing_scope, scope)


def _load_declared_graph(targets: dict[str, CMakeTarget]) -> tuple[dict[str, list[str]], dict[str, list[str]], dict[str, list[str]]]:
    merged_graph: dict[str, list[str]] = {}
    cmake_text_graph: dict[str, list[str]] = {}
    cmake_public_graph: dict[str, list[str]] = {}

    for target_name, target in targets.items():
        merged_graph[target_name] = list(target.declared_links)
        cmake_text_graph[target_name] = list(target.declared_links)
        cmake_public_graph[target_name] = [
            dep_name
            for dep_name, scope in target.declared_link_scopes.items()
            if scope in {"public", "interface"}
        ]

    return merged_graph, cmake_text_graph, cmake_public_graph


def _merge_link_scope(existing_scope: str | None, new_scope: str) -> str:
    if existing_scope is None:
        return new_scope
    if existing_scope == new_scope:
        return existing_scope
    if {existing_scope, new_scope} == {"private", "interface"}:
        return "public"
    if new_scope == "public" or existing_scope == "public":
        return "public"
    return new_scope


def _load_compile_commands(compile_commands_path: Path) -> list[CompileCommand]:
    data = read_json(compile_commands_path)
    commands: list[CompileCommand] = []
    for item in data if isinstance(data, list) else []:
        file_path = Path(item["file"]).resolve()
        directory = Path(item["directory"]).resolve()
        command = item.get("command") or " ".join(item.get("arguments", []))
        commands.append(CompileCommand(file=file_path, directory=directory, command=command))
    return commands


def targets_for_scope(context: BuildContext, scope: Scope) -> list[CMakeTarget]:
    selected: list[CMakeTarget] = []
    for target in context.targets.values():
        if any(scope.contains(source) for source in target.sources):
            selected.append(target)
            continue
        if target.source_dir is not None and scope.contains(target.source_dir):
            selected.append(target)
    return selected


def compile_commands_for_scope(context: BuildContext, scope: Scope) -> list[CompileCommand]:
    return [command for command in context.compile_commands if scope.contains(command.file)]
