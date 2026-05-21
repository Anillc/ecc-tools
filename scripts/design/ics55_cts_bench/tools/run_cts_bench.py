#!/usr/bin/env python3
"""Run iCTS benchmark cases one by one."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import time
from dataclasses import dataclass, asdict
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[4]
BENCH_ROOT = REPO_ROOT / "scripts" / "design" / "ics55_cts_bench"


@dataclass
class RunResult:
    case_name: str
    status: str
    exit_code: int | None
    runtime_s: float
    result_dir: str
    log_path: str
    error: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--case", action="append", dest="cases", help="case name to run; repeatable")
    group.add_argument("--all", action="store_true", help="run all prepared cases")
    group.add_argument("--list-cases", action="store_true", help="list prepared cases and exit")
    parser.add_argument("--bench-root", type=Path, default=BENCH_ROOT)
    parser.add_argument("--force", action="store_true", help="remove and rerun existing result directories")
    parser.add_argument("--timeout", type=float, default=0.0, help="per-case timeout in seconds; 0 disables timeout")
    parser.add_argument("--skip-power", action="store_true", help="skip post-CTS report_power")
    return parser.parse_args()


def prepared_cases(bench_root: Path) -> list[str]:
    cases_dir = bench_root / "cases"
    if not cases_dir.exists():
        return []
    return [
        path.name
        for path in sorted(cases_dir.iterdir())
        if path.is_dir() and (path / "place.def").exists() and (path / "place.v").exists() and (path / "default.sdc").exists()
    ]


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as fp:
        return json.load(fp)


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def shared_library_path(bench_root: Path, base_env: dict[str, str]) -> str:
    repo_root = bench_root.parents[2]
    dirs: set[Path] = set()
    for build_dir_name in ("build", "build_release"):
        build_dir = repo_root / build_dir_name
        if not build_dir.exists():
            continue
        for shared_lib in build_dir.rglob("*.so"):
            dirs.add(shared_lib.parent)
    onnx_dir = repo_root / "src" / "third_party" / "onnxruntime"
    if onnx_dir.exists():
        dirs.add(onnx_dir)
    ordered_dirs = [str(path) for path in sorted(dirs)]
    existing = base_env.get("LD_LIBRARY_PATH", "")
    if existing:
        ordered_dirs.append(existing)
    return ":".join(ordered_dirs)


def make_case_db_config(bench_root: Path, case_name: str) -> Path:
    base_config_path = bench_root / "iEDA_config" / "db_default_config.json"
    case_dir = bench_root / "cases" / case_name
    result_dir = case_dir / "result"
    run_config_dir = case_dir / "run_config"
    run_config_dir.mkdir(parents=True, exist_ok=True)

    config = load_json(base_config_path)
    config.setdefault("INPUT", {})
    config["INPUT"]["def_path"] = str((case_dir / "place.def").resolve())
    config["INPUT"]["verilog_path"] = str((case_dir / "place.v").resolve())
    config["INPUT"]["sdc_path"] = str((case_dir / "default.sdc").resolve())
    config["INPUT"]["spef"] = ""
    config.setdefault("OUTPUT", {})
    config["OUTPUT"]["output_dir_path"] = str(result_dir.resolve())

    output_path = run_config_dir / "db_default_config.json"
    write_json(output_path, config)
    return output_path


def update_status_csv(bench_root: Path, result: RunResult) -> None:
    reports_dir = bench_root / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)
    status_path = reports_dir / "run_status.csv"
    rows: dict[str, dict[str, object]] = {}
    if status_path.exists():
        with status_path.open(newline="", encoding="utf-8") as fp:
            reader = csv.DictReader(fp)
            for row in reader:
                rows[row["case_name"]] = row
    rows[result.case_name] = asdict(result)

    fieldnames = ["case_name", "status", "exit_code", "runtime_s", "result_dir", "log_path", "error"]
    with status_path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        for case_name in sorted(rows):
            writer.writerow(rows[case_name])


def cts_key_result_status(cts_log: Path) -> str:
    if not cts_log.exists():
        return ""
    lines = cts_log.read_text(encoding="utf-8", errors="ignore").splitlines()
    for idx, line in enumerate(lines):
        if "CTS Key Results" not in line:
            continue
        for row_line in lines[idx + 1 :]:
            stripped = row_line.strip()
            if stripped.startswith("## ") or stripped.startswith("====="):
                break
            if not stripped.startswith("|") or not stripped.endswith("|"):
                continue
            cells = [cell.strip() for cell in stripped.strip("|").split("|")]
            if len(cells) >= 2 and cells[0] == "status":
                return cells[1]
    return ""


def run_case(bench_root: Path, case_name: str, force: bool, timeout: float, skip_power: bool) -> RunResult:
    case_dir = bench_root / "cases" / case_name
    result_dir = case_dir / "result"
    run_log = case_dir / "run_iCTS_bench.stdout.log"
    ieda_binary = bench_root / "iEDA"
    bench_tcl = bench_root / "script" / "iCTS_script" / "run_iCTS_bench.tcl"

    if force and result_dir.exists():
        shutil.rmtree(result_dir)
    result_dir.mkdir(parents=True, exist_ok=True)

    db_config = make_case_db_config(bench_root, case_name)
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = shared_library_path(bench_root, env)
    env.update(
        {
            "BENCH_CASE_NAME": case_name,
            "BENCH_FLOW_CONFIG": str((bench_root / "iEDA_config" / "flow_config.json").resolve()),
            "BENCH_DB_CONFIG": str(db_config.resolve()),
            "BENCH_CTS_CONFIG": str((bench_root / "iEDA_config" / "cts_default_config.json").resolve()),
            "BENCH_RESULT_DIR": str(result_dir.resolve()),
        }
    )
    if skip_power:
        env["BENCH_SKIP_POWER"] = "1"

    start = time.monotonic()
    exit_code: int | None = None
    error = ""
    status = "failed"

    with run_log.open("w", encoding="utf-8") as log_fp:
        log_fp.write(f"Running case {case_name}\n")
        log_fp.write(f"Command: {ieda_binary} -script {bench_tcl}\n\n")
        log_fp.flush()
        try:
            completed = subprocess.run(
                [str(ieda_binary), "-script", str(bench_tcl)],
                cwd=str(bench_root),
                env=env,
                stdout=log_fp,
                stderr=subprocess.STDOUT,
                timeout=timeout if timeout > 0 else None,
                check=False,
            )
            exit_code = completed.returncode
            cts_log = result_dir / "cts" / "cts.log"
            cts_status = cts_key_result_status(cts_log)
            if completed.returncode == 0 and cts_status == "finished":
                status = "passed"
            elif completed.returncode == 0 and cts_log.exists():
                status = f"cts_{cts_status or 'unknown'}"
                error = f"CTS Key Results status is {cts_status or 'unavailable'}"
            else:
                error = f"iEDA exited with code {completed.returncode}"
        except subprocess.TimeoutExpired as exc:
            exit_code = None
            status = "timeout"
            error = f"timeout after {timeout:.1f}s"
            log_fp.write(f"\nERROR: {error}\n{exc}\n")
        except OSError as exc:
            exit_code = None
            status = "failed"
            error = str(exc)
            log_fp.write(f"\nERROR: {error}\n")

    runtime_s = time.monotonic() - start
    return RunResult(
        case_name=case_name,
        status=status,
        exit_code=exit_code,
        runtime_s=runtime_s,
        result_dir=str(result_dir),
        log_path=str(run_log),
        error=error,
    )


def main() -> int:
    args = parse_args()
    bench_root = args.bench_root.resolve()
    cases = prepared_cases(bench_root)
    if args.list_cases:
        for case_name in cases:
            print(case_name)
        return 0

    selected = cases if args.all else sorted(set(args.cases or []))
    missing = sorted(set(selected) - set(cases))
    if missing:
        raise SystemExit(f"Cases are not prepared: {', '.join(missing)}")

    any_failed = False
    for idx, case_name in enumerate(selected, start=1):
        print(f"[{idx}/{len(selected)}] running {case_name}", flush=True)
        result = run_case(bench_root, case_name, args.force, args.timeout, args.skip_power)
        update_status_csv(bench_root, result)
        print(
            f"[{idx}/{len(selected)}] {case_name}: {result.status}, "
            f"runtime={result.runtime_s:.2f}s, exit={result.exit_code}",
            flush=True,
        )
        if result.status != "passed":
            any_failed = True

    return 1 if any_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
