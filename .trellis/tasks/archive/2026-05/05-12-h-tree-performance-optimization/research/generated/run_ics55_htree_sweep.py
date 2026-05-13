#!/usr/bin/env python3
"""Run the ics55_dev H-tree iteration/step sweep.

The script generates per-case CTS configs from the reference dev-flow config,
runs the generated Tcl harness, and writes machine-readable summaries under
the task research directory.
"""

from __future__ import annotations

import csv
import json
import os
import re
import subprocess
import sys
import time
from argparse import ArgumentParser
from pathlib import Path


REPO = Path("/home/liweiguo/project/ecc-tools-dev")
TASK_DIR = REPO / ".trellis/tasks/05-12-h-tree-performance-optimization"
DESIGN_DIR = REPO / "scripts/design/ics55_dev"
REFERENCE_CONFIG = DESIGN_DIR / "iEDA_config/cts_default_config.json"
REFERENCE_INPUT_DEF = DESIGN_DIR / "result/bp_be_top_place.def.gz"
IEDA_BIN = DESIGN_DIR / "iEDA"
TCL_SCRIPT = TASK_DIR / "research/generated/run_iCTS_dev_sweep.tcl"
GENERATED_DIR = TASK_DIR / "research/generated/configs"
RESULTS_DIR = TASK_DIR / "research/results/ics55_dev_htree_sweep"
ITERATIONS = (1, 2, 3, 4, 5)
STEPS = (5, 10, 15)


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2)
        stream.write("\n")


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def first_float(text: str, patterns: tuple[str, ...]) -> float | None:
    for pattern in patterns:
        match = re.search(pattern, text, flags=re.IGNORECASE)
        if match:
            try:
                return float(match.group(1))
            except ValueError:
                return None
    return None


def first_int(text: str, patterns: tuple[str, ...]) -> int | None:
    value = first_float(text, patterns)
    if value is None:
        return None
    return int(value)


def parse_scalar_with_unit(raw: str) -> float | None:
    value_match = re.search(r"([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)", raw)
    if not value_match:
        return None
    value = float(value_match.group(1))
    unit_text = raw[value_match.end() :].strip().split()
    unit = unit_text[0] if unit_text else ""
    if unit == "uW":
        return value * 1.0e-6
    if unit == "mW":
        return value * 1.0e-3
    if unit == "nW":
        return value * 1.0e-9
    return value


def extract_overview_rows(text: str) -> dict[str, str]:
    marker = "HTree Synthesis Overview"
    marker_index = text.find(marker)
    if marker_index < 0:
        return {}

    rows: dict[str, str] = {}
    for line in text[marker_index:].splitlines():
        stripped = line.strip()
        if not stripped.startswith("|") or " | " not in stripped:
            if rows and stripped.startswith("+"):
                continue
            if rows and stripped == "":
                break
            continue

        columns = [column.strip() for column in stripped.strip("|").split("|")]
        if len(columns) < 2 or columns[0] in {"Item", "Field"}:
            continue
        rows[columns[0]] = columns[1]
    return rows


def parse_int_row(rows: dict[str, str], key: str) -> int | None:
    value = rows.get(key)
    if value is None:
        return None
    match = re.search(r"[0-9]+", value)
    return int(match.group(0)) if match else None


def parse_float_row(rows: dict[str, str], key: str) -> float | None:
    value = rows.get(key)
    if value is None:
        return None
    return parse_scalar_with_unit(value)


def parse_delay_power_row(rows: dict[str, str], key: str) -> tuple[float | None, float | None]:
    value = rows.get(key)
    if value is None:
        return None, None
    parts = [part.strip() for part in value.split("/", maxsplit=1)]
    delay = parse_scalar_with_unit(parts[0]) if parts else None
    power = parse_scalar_with_unit(parts[1]) if len(parts) > 1 else None
    return delay, power


def extract_cts_log_metrics(cts_log: Path) -> dict:
    text = read_text(cts_log)
    if not text:
        return {}

    overview_rows = extract_overview_rows(text)
    raw_delay_ns, raw_power_w = parse_delay_power_row(overview_rows, "raw_htree_char_metric")
    compensation_delay_ns, compensation_power_w = parse_delay_power_row(overview_rows, "root_driver_compensation")
    compensated_delay_ns, compensated_power_w = parse_delay_power_row(overview_rows, "compensated_htree_metric")

    return {
        "selected_depth": first_int(text, (r"selected_depth\s*\|?\s*([0-9]+)", r"selected_depth[^0-9]+([0-9]+)")),
        "selected_final_frontier_count": first_int(
            text,
            (
                r"selected_final_frontier_count\s*\|?\s*([0-9]+)",
                r"final_frontier_count\s*\|?\s*([0-9]+)",
                r"frontier_entries[^0-9]+([0-9]+)",
            ),
        ),
        "candidate_frontier_entry_count": parse_int_row(overview_rows, "candidate_frontier_entry_count"),
        "feasible_solutions": parse_int_row(overview_rows, "feasible_solutions"),
        "feasible_frontier_entry_count": parse_int_row(overview_rows, "feasible_frontier_entry_count"),
        "inserted_insts": parse_int_row(overview_rows, "inserted_insts"),
        "inserted_nets": parse_int_row(overview_rows, "inserted_nets"),
        "best_delay_ns": parse_float_row(overview_rows, "delay")
        or first_float(text, (r"best_delay_ns\s*\|?\s*([0-9.eE+-]+)", r"delay[^0-9]+([0-9.eE+-]+)\s*ns")),
        "best_power_w": parse_float_row(overview_rows, "power")
        or first_float(text, (r"best_power_w\s*\|?\s*([0-9.eE+-]+)", r"power[^0-9]+([0-9.eE+-]+)\s*W")),
        "raw_delay_ns": raw_delay_ns,
        "raw_power_w": raw_power_w,
        "root_driver_compensation_delay_ns": compensation_delay_ns,
        "root_driver_compensation_power_w": compensation_power_w,
        "compensated_delay_ns": compensated_delay_ns,
        "compensated_power_w": compensated_power_w,
        "htree_load_group_count": parse_int_row(overview_rows, "htree_load_group_count"),
        "htree_load_cap_min_pf": parse_float_row(overview_rows, "htree_load_cap_min"),
        "htree_load_cap_max_pf": parse_float_row(overview_rows, "htree_load_cap_max"),
        "htree_load_cap_mean_pf": parse_float_row(overview_rows, "htree_load_cap_mean"),
        "htree_load_cap_median_pf": parse_float_row(overview_rows, "htree_load_cap_median"),
        "selected_root_driver_cell_master": overview_rows.get("selected_root_driver_cell_master"),
        "used_boundary_fallback": overview_rows.get("used_boundary_fallback"),
        "selection_policy": overview_rows.get("selection_policy"),
        "raw_log_mentions_boundary_fallback": "boundary_fallback" in text,
        "has_htree_synthesis_overview": "HTree Synthesis Overview" in text,
    }


def extract_json_metrics(case_result_dir: Path) -> dict:
    metrics_path = case_result_dir / "metric/iCTS_metrics.json"
    stat_path = case_result_dir / "report/cts_stat.json"
    metrics = load_json(metrics_path) if metrics_path.exists() else {}
    stat = load_json(stat_path) if stat_path.exists() else {}

    cts = metrics.get("CTS", {}) if isinstance(metrics.get("CTS", {}), dict) else {}
    timing = {}
    clock_timings = cts.get("clocks_timing")
    if isinstance(clock_timings, list) and clock_timings:
        first_clock = clock_timings[0]
        if isinstance(first_clock, dict):
            timing = first_clock

    design_info = stat.get("Design Information", {}) if isinstance(stat.get("Design Information", {}), dict) else {}
    return {
        "buffer_area": cts.get("buffer_area"),
        "buffer_num": cts.get("buffer_num"),
        "clock_path_min_buffer": cts.get("clock_path_min_buffer"),
        "clock_path_max_buffer": cts.get("clock_path_max_buffer"),
        "max_clock_wirelength": cts.get("max_clock_wirelength"),
        "total_clock_wirelength": cts.get("total_clock_wirelength"),
        "max_level_of_clock_tree": cts.get("max_level_of_clock_tree"),
        "setup_wns": timing.get("setup_wns"),
        "setup_tns": timing.get("setup_tns"),
        "hold_wns": timing.get("hold_wns"),
        "hold_tns": timing.get("hold_tns"),
        "suggest_freq": timing.get("suggest_freq"),
        "flow_runtime": design_info.get("flow_runtime"),
        "flow_memory": design_info.get("flow_memory"),
    }


def make_config(iteration: int, steps: int) -> Path:
    config = load_json(REFERENCE_CONFIG)
    config["wirelength_iterations"] = iteration
    config["slew_steps"] = steps
    config["cap_steps"] = steps
    path = GENERATED_DIR / f"cts_iter{iteration}_step{steps}.json"
    write_json(path, config)
    return path


def run_case(iteration: int, steps: int) -> dict:
    case_name = f"iter{iteration}_step{steps}"
    case_dir = RESULTS_DIR / case_name
    result_dir = case_dir / "result"
    log_path = case_dir / "run.log"
    config_path = make_config(iteration, steps)

    (result_dir / "report").mkdir(parents=True, exist_ok=True)
    (result_dir / "metric").mkdir(parents=True, exist_ok=True)
    (result_dir / "cts").mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.update(
        {
            "CTS_CONFIG": str(config_path),
            "RESULT_DIR": str(result_dir),
            "INPUT_DEF": str(REFERENCE_INPUT_DEF),
            "TOOL_REPORT_DIR": str(result_dir / "cts"),
            "OUTPUT_DEF": str(result_dir / "iCTS_result.def"),
            "OUTPUT_VERILOG": str(result_dir / "iCTS_result.v"),
            "DESIGN_STAT_TEXT": str(result_dir / "report/cts_stat.rpt"),
            "DESIGN_STAT_JSON": str(result_dir / "report/cts_stat.json"),
            "TOOL_METRICS_JSON": str(result_dir / "metric/iCTS_metrics.json"),
        }
    )

    start = time.monotonic()
    with log_path.open("w", encoding="utf-8") as log_stream:
        process = subprocess.run(
            [str(IEDA_BIN), "-script", str(TCL_SCRIPT)],
            cwd=DESIGN_DIR,
            env=env,
            stdout=log_stream,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    wall_runtime_s = time.monotonic() - start

    record = {
        "case": case_name,
        "wirelength_iterations": iteration,
        "steps": steps,
        "exit_code": process.returncode,
        "wall_runtime_s": round(wall_runtime_s, 6),
        "config_path": str(config_path),
        "case_dir": str(case_dir),
        "run_log": str(log_path),
    }
    if process.returncode == 0:
        record.update(extract_json_metrics(result_dir))
        record.update(extract_cts_log_metrics(result_dir / "cts/cts.log"))
    else:
        tail = "\n".join(read_text(log_path).splitlines()[-40:])
        record["failure_tail"] = tail
    return record


def write_summary(records: list[dict]) -> None:
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    write_json(RESULTS_DIR / "summary.json", {"records": records})

    fieldnames = [
        "case",
        "wirelength_iterations",
        "steps",
        "exit_code",
        "wall_runtime_s",
        "flow_runtime",
        "flow_memory",
        "buffer_num",
        "buffer_area",
        "total_clock_wirelength",
        "max_clock_wirelength",
        "clock_path_min_buffer",
        "clock_path_max_buffer",
        "max_level_of_clock_tree",
        "setup_wns",
        "hold_wns",
        "suggest_freq",
        "selected_depth",
        "selected_final_frontier_count",
        "candidate_frontier_entry_count",
        "feasible_solutions",
        "feasible_frontier_entry_count",
        "inserted_insts",
        "inserted_nets",
        "best_delay_ns",
        "best_power_w",
        "raw_delay_ns",
        "raw_power_w",
        "root_driver_compensation_delay_ns",
        "root_driver_compensation_power_w",
        "compensated_delay_ns",
        "compensated_power_w",
        "htree_load_group_count",
        "htree_load_cap_min_pf",
        "htree_load_cap_max_pf",
        "htree_load_cap_mean_pf",
        "htree_load_cap_median_pf",
        "selected_root_driver_cell_master",
        "used_boundary_fallback",
        "selection_policy",
        "config_path",
        "run_log",
        "case_dir",
    ]
    with (RESULTS_DIR / "summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(records)


def validate_inputs() -> None:
    missing = [path for path in (REFERENCE_CONFIG, REFERENCE_INPUT_DEF, IEDA_BIN, TCL_SCRIPT) if not path.exists()]
    if missing:
        missing_text = "\n".join(str(path) for path in missing)
        raise FileNotFoundError(f"Missing required input(s):\n{missing_text}")


def rebuild_records_from_existing_results() -> list[dict]:
    records: list[dict] = []
    for iteration in ITERATIONS:
        for steps in STEPS:
            case_name = f"iter{iteration}_step{steps}"
            case_dir = RESULTS_DIR / case_name
            result_dir = case_dir / "result"
            run_log = case_dir / "run.log"
            if not run_log.exists() and run_log.with_suffix(run_log.suffix + ".gz").exists():
                run_log = run_log.with_suffix(run_log.suffix + ".gz")
            config_path = GENERATED_DIR / f"cts_iter{iteration}_step{steps}.json"
            record = {
                "case": case_name,
                "wirelength_iterations": iteration,
                "steps": steps,
                "exit_code": 0 if (result_dir / "metric/iCTS_metrics.json").exists() else 1,
                "wall_runtime_s": None,
                "config_path": str(config_path),
                "case_dir": str(case_dir),
                "run_log": str(run_log),
            }
            runtime_match = re.search(r"Completed iter=.*runtime=([0-9.]+)s", read_text(run_log))
            if runtime_match:
                record["wall_runtime_s"] = float(runtime_match.group(1))
            if record["exit_code"] == 0:
                record.update(extract_json_metrics(result_dir))
                record.update(extract_cts_log_metrics(result_dir / "cts/cts.log"))
            records.append(record)
    return records


def main() -> int:
    parser = ArgumentParser(description="Run or parse the ics55_dev H-tree sweep.")
    parser.add_argument("--parse-only", action="store_true", help="refresh summary files from existing per-case results")
    args = parser.parse_args()
    if args.parse_only:
        write_summary(rebuild_records_from_existing_results())
        return 0

    validate_inputs()
    records: list[dict] = []
    for iteration in ITERATIONS:
        for steps in STEPS:
            print(f"Running iter={iteration}, steps={steps}", flush=True)
            record = run_case(iteration, steps)
            records.append(record)
            write_summary(records)
            status = "ok" if record["exit_code"] == 0 else f"failed({record['exit_code']})"
            print(f"Completed iter={iteration}, steps={steps}: {status}, runtime={record['wall_runtime_s']:.3f}s", flush=True)
            if record["exit_code"] != 0:
                print(record.get("failure_tail", ""), file=sys.stderr)
                return record["exit_code"]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
