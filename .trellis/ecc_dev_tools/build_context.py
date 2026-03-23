from __future__ import annotations

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

    refresh_reason = _needs_refresh(repo_root, build_dir, compile_commands_path, reply_dir, profile)
    refreshed = False
    if refresh_reason is not None:
        _refresh_build_metadata(repo_root, build_dir, profile, jobs)
        refreshed = True

    targets = _load_targets(repo_root, reply_dir)
    compile_commands = _load_compile_commands(compile_commands_path)

    declared_graph, cmake_text_graph, cmake_public_graph = _load_declared_graph(repo_root, reply_dir, profile)

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
        refreshed=refreshed,
        refresh_reason=refresh_reason,
    )


def _needs_refresh(repo_root: Path, build_dir: Path, compile_commands_path: Path, reply_dir: Path, profile: Profile) -> str | None:
    if not compile_commands_path.is_file():
        return "compile_commands.json is missing"
    if not reply_dir.is_dir() or not any(reply_dir.glob("index-*.json")):
        return "CMake File API reply is missing"
    cmake_cache = build_dir / "CMakeCache.txt"
    if not cmake_cache.is_file():
        return "CMakeCache.txt is missing"
    root_cmake = repo_root / "CMakeLists.txt"
    json_files = list(reply_dir.glob("*.json"))
    if not json_files:
        return "CMake File API reply directory contains no JSON files"
    newest_reply_mtime = max(path.stat().st_mtime for path in json_files)
    newest_reference_mtime = max(root_cmake.stat().st_mtime, cmake_cache.stat().st_mtime)
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

    configure_command = [
        "cmake",
        "-S",
        str(repo_root),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
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
        for dependency in target_json.get("dependencies", []):
            dep_id = dependency.get("id", "")
            dep_name = dep_id.split("::@", 1)[0] if dep_id else dep_id
            if dep_name:
                target.declared_links.append(dep_name)
        if target.json_file.exists():
            scopes = _parse_declared_link_scopes(target.json_file, repo_root)
            target.declared_link_scopes.update(scopes)
    return targets


def _parse_declared_link_scopes(path: Path, repo_root: Path) -> dict[str, str]:
    target_json = read_json(path)
    if not isinstance(target_json, dict):
        return {}

    backtrace_graph = target_json.get("backtraceGraph", {})
    files = backtrace_graph.get("files", []) if isinstance(backtrace_graph, dict) else []
    cmake_path = None
    for candidate in files:
        if isinstance(candidate, str) and candidate.endswith("CMakeLists.txt"):
            candidate_path = Path(candidate)
            cmake_path = candidate_path if candidate_path.is_absolute() else (repo_root / candidate_path)
            break
    if cmake_path is None or not cmake_path.is_file():
        return {}

    content = cmake_path.read_text(encoding="utf-8", errors="replace")
    mapping: dict[str, str] = {}
    current_target = None
    current_scope = None
    active = False
    for raw_line in content.splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        if not active and line.startswith("target_link_libraries("):
            active = True
            current_target = None
            current_scope = None
            continue
        if active:
            if line == ")":
                active = False
                current_target = None
                current_scope = None
                continue
            if current_target is None:
                current_target = line
                continue
            if line in {"PRIVATE", "PUBLIC", "INTERFACE"}:
                current_scope = line.lower()
                continue
            if current_scope is None:
                continue
            for token in line.replace(")", " ").split():
                if token in {"PRIVATE", "PUBLIC", "INTERFACE"}:
                    current_scope = token.lower()
                    continue
                if token.startswith("$"):
                    continue
                mapping[token] = current_scope
    return mapping


def _load_declared_graph(repo_root: Path, reply_dir: Path, profile: Profile) -> tuple[dict[str, list[str]], dict[str, list[str]], dict[str, list[str]]]:
    """Return (merged_graph, cmake_text_graph, cmake_public_graph).

    merged_graph: union of File API transitive deps + CMakeLists.txt text parsing.
    cmake_text_graph: all edges parsed from CMakeLists.txt target_link_libraries() calls.
    cmake_public_graph: only PUBLIC/INTERFACE edges (propagatable dependencies).
    """
    merged_graph: dict[str, list[str]] = {}
    for target_json_path in reply_dir.glob("target-*.json"):
        data = read_json(target_json_path)
        if not isinstance(data, dict):
            continue
        name = data.get("name")
        if not isinstance(name, str) or not name:
            continue
        merged_graph.setdefault(name, [])
        for dependency in data.get("dependencies", []) or []:
            dep_id = dependency.get("id", "")
            dep_name = dep_id.split("::@", 1)[0] if dep_id else dep_id
            if dep_name:
                merged_graph[name].append(dep_name)

    cmake_text_graph: dict[str, list[str]] = {}
    cmake_public_graph: dict[str, list[str]] = {}
    if profile.graph_root:
        cmake_root = repo_root / profile.graph_root
    else:
        cmake_root = repo_root
    for path in sorted(cmake_root.rglob("*")):
        if not path.is_file() or (path.name != "CMakeLists.txt" and path.suffix != ".cmake"):
            continue
        file_all: dict[str, list[str]] = {}
        file_public: dict[str, list[str]] = {}
        _parse_declared_graph_entries(path, file_all, file_public)
        for target_name, deps in file_all.items():
            cmake_text_graph.setdefault(target_name, []).extend(deps)
            merged_graph.setdefault(target_name, []).extend(deps)
        for target_name, deps in file_public.items():
            cmake_public_graph.setdefault(target_name, []).extend(deps)

    def _dedupe(graph: dict[str, list[str]]) -> None:
        for target_name, deps in graph.items():
            deduped: list[str] = []
            seen: set[str] = set()
            for dep in deps:
                if dep in seen:
                    continue
                seen.add(dep)
                deduped.append(dep)
            graph[target_name] = deduped

    _dedupe(merged_graph)
    _dedupe(cmake_text_graph)
    _dedupe(cmake_public_graph)
    return merged_graph, cmake_text_graph, cmake_public_graph


def _parse_declared_graph_entries(cmake_path: Path, all_entries: dict[str, list[str]], public_entries: dict[str, list[str]]) -> dict[str, list[str]]:
    lines = cmake_path.read_text(encoding="utf-8", errors="replace").splitlines()
    active = False
    depth = 0
    buffer = ""

    for raw_line in lines:
        line = raw_line.split("#", 1)[0]
        if not active:
            marker = "target_link_libraries("
            if marker not in line:
                continue
            active = True
            start = line.index(marker) + len(marker)
            buffer = line[start:] + " "
            depth = 1 + line[start:].count("(") - line[start:].count(")")
            if depth <= 0:
                _consume_declared_graph_block(buffer, all_entries, public_entries)
                active = False
                buffer = ""
            continue

        buffer += line + " "
        depth += line.count("(") - line.count(")")
        if depth <= 0:
            _consume_declared_graph_block(buffer, all_entries, public_entries)
            active = False
            buffer = ""
            depth = 0

    return all_entries


def _consume_declared_graph_block(buffer: str, all_entries: dict[str, list[str]], public_entries: dict[str, list[str]]) -> None:
    tokens = buffer.replace("(", " ").replace(")", " ").split()
    if not tokens:
        return
    target_name = tokens[0]
    all_deps: list[str] = []
    public_deps: list[str] = []
    scope = "PUBLIC"  # CMake default when no scope keyword is given
    for token in tokens[1:]:
        if token in {"PRIVATE", "PUBLIC", "INTERFACE"}:
            scope = token
            continue
        if token.startswith("$"):
            continue
        all_deps.append(token)
        if scope in {"PUBLIC", "INTERFACE"}:
            public_deps.append(token)
    if all_deps:
        all_entries.setdefault(target_name, []).extend(all_deps)
    if public_deps:
        public_entries.setdefault(target_name, []).extend(public_deps)


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
