#!/usr/bin/env python3
"""Collect iCTS benchmark metrics into CSV."""

from __future__ import annotations

import argparse
import csv
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[4]
BENCH_ROOT = REPO_ROOT / "scripts" / "design" / "ics55_cts_bench"

REQUESTED_COLUMNS = [
    "case_name",
    "clock_name",
    "Latency (ps)",
    "Skew (ps)",
    "#Buffer",
    "Buf-Area (um^2)",
    "Pow. (uW)",
    "Clk-Cap (fF)",
    "Clk-WL (um)",
    "Runtime (s)",
    "#Fanout Vio.",
    "#Cap Vio.",
    "#Slew Vio.",
    "WNS (ps)",
    "TNS (ps)",
]

EXTRA_COLUMNS = [
    "clock_port",
    "status",
    "error",
    "power_source",
    "clk_cap_source",
    "violation_source",
    "clock_pin_cap_fF",
    "clock_wire_cap_fF",
    "metric_notes",
]


@dataclass
class PinInfo:
    direction: str = ""
    capacitance_pf: float | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench-root", type=Path, default=BENCH_ROOT)
    parser.add_argument("--case", action="append", dest="cases", help="case name to collect; repeatable")
    return parser.parse_args()


def numeric(value: str | None) -> float | None:
    if value is None:
        return None
    match = re.search(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?", value)
    if not match:
        return None
    return float(match.group(0))


def fmt(value: float | int | None, digits: int = 3) -> str:
    if value is None:
        return ""
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"


def split_table_row(line: str) -> list[str] | None:
    stripped = line.strip()
    if not stripped.startswith("|") or not stripped.endswith("|"):
        return None
    return [cell.strip() for cell in stripped.strip("|").split("|")]


def rows_after_title(lines: list[str], title: str) -> list[list[str]]:
    start = None
    for idx, line in enumerate(lines):
        if title in line:
            start = idx + 1
            break
    if start is None:
        return []

    rows: list[list[str]] = []
    seen_header = False
    for line in lines[start:]:
        if line.startswith("====") and rows:
            break
        row = split_table_row(line)
        if row is None:
            if rows and line.strip() == "":
                break
            continue
        if not seen_header:
            if any(cell for cell in row):
                seen_header = True
            continue
        if row and row[0] in {"Field", "Clock", "Stage"}:
            continue
        if all(set(cell) <= {"-"} for cell in row if cell):
            continue
        rows.append(row)
    return rows


def key_value_table(lines: list[str], title: str) -> dict[str, str]:
    data: dict[str, str] = {}
    for row in rows_after_title(lines, title):
        if len(row) >= 2 and row[0] and row[0] not in {"Field", "Item"}:
            data[row[0]] = row[1]
    return data


def parse_cts_log(cts_log: Path, clock_name: str) -> tuple[dict[str, float | int | str], list[str]]:
    if not cts_log.exists():
        return {}, [f"missing cts log: {cts_log}"]
    lines = cts_log.read_text(encoding="utf-8", errors="ignore").splitlines()
    metrics: dict[str, float | int | str] = {}
    notes: list[str] = []

    key_results = key_value_table(lines, "CTS Key Results")
    if key_results:
        metrics["cts_status"] = key_results.get("status", "")
        metrics["#Buffer"] = int(numeric(key_results.get("final_clock_buffer_count")) or 0)
        metrics["Buf-Area (um^2)"] = numeric(key_results.get("final_buffer_area"))
        metrics["Clk-WL (um)"] = numeric(key_results.get("total_clock_network_wirelength"))
        metrics["Runtime (s)"] = numeric(key_results.get("elapsed_time"))
    else:
        notes.append("missing CTS Key Results")

    timing_rows = rows_after_title(lines, "CTS Clock Timing Overview")
    timing_row = next((row for row in timing_rows if row and row[0] == clock_name), timing_rows[0] if timing_rows else None)
    if timing_row and len(timing_row) >= 3:
        metrics["TNS (ps)"] = (numeric(timing_row[1]) or 0.0) * 1000.0
        metrics["WNS (ps)"] = (numeric(timing_row[2]) or 0.0) * 1000.0
    else:
        notes.append("missing CTS Clock Timing Overview")

    latency_rows = rows_after_title(lines, "CTS Clock Latency Skew Overview")
    selected_latency_rows = [row for row in latency_rows if row and row[0] == clock_name] or latency_rows
    latency_candidates: list[float] = []
    skew_candidates: list[float] = []
    for row in selected_latency_rows:
        if len(row) >= 7:
            launch = numeric(row[4])
            capture = numeric(row[5])
            skew = numeric(row[6])
            if launch is not None:
                latency_candidates.append(launch)
            if capture is not None:
                latency_candidates.append(capture)
            if skew is not None:
                skew_candidates.append(abs(skew))
    if latency_candidates:
        metrics["Latency (ps)"] = max(latency_candidates) * 1000.0
    else:
        notes.append("missing clock latency")
    if skew_candidates:
        metrics["Skew (ps)"] = max(skew_candidates) * 1000.0
    else:
        notes.append("missing clock skew")

    routing_rc = key_value_table(lines, "Runtime Routing / Wire RC")
    unit_cap = numeric(routing_rc.get("unit_capacitance"))
    if unit_cap is not None:
        metrics["unit_capacitance_pf_per_um"] = unit_cap
    else:
        notes.append("missing unit_capacitance")

    return metrics, notes


def load_clock_selection(case_dir: Path) -> tuple[str, str, dict]:
    manifest_path = case_dir / "clock_selection.json"
    if not manifest_path.exists():
        return "", "", {}
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    selected = data.get("selected_clock", {})
    sdc_target = data.get("sdc_target", {})
    clock_name = sdc_target.get("clock_name") or selected.get("net_name", "")
    clock_port = sdc_target.get("target_name") or selected.get("pin_name", "")
    return clock_name, clock_port, data


def load_run_status(bench_root: Path) -> dict[str, dict[str, str]]:
    status_path = bench_root / "reports" / "run_status.csv"
    if not status_path.exists():
        return {}
    with status_path.open(newline="", encoding="utf-8") as fp:
        return {row["case_name"]: row for row in csv.DictReader(fp)}


def parse_power_uW(power_dir: Path) -> tuple[float | None, str, str]:
    json_files = sorted(power_dir.glob("*.pwr.json"))
    for path in json_files:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            return None, "iPA_json_parse_error", str(exc)
        power_w = numeric(str(data.get("total_power", "")))
        if power_w is not None:
            return power_w * 1_000_000.0, f"iPA_json:{path.name}", ""
    if (power_dir / "power_failed.txt").exists():
        return None, "iPA_failed", (power_dir / "power_failed.txt").read_text(encoding="utf-8", errors="ignore").strip()
    if (power_dir / "power_unavailable.txt").exists():
        return None, "iPA_unavailable", "report_power command unavailable"
    return None, "missing_iPA_json", "missing iPA power json"


def parse_cap_unit_to_pf(lib_text: str) -> float:
    match = re.search(r"capacitive_load_unit\s*\(\s*([0-9.eE+-]+)\s*,\s*([a-zA-Z]+)\s*\)", lib_text)
    if not match:
        return 1.0
    magnitude = float(match.group(1))
    unit = match.group(2).lower()
    if unit in {"pf", "picofarad", "picofarads"}:
        return magnitude
    if unit in {"ff", "femtofarad", "femtofarads"}:
        return magnitude * 0.001
    if unit in {"nf", "nanofarad", "nanofarads"}:
        return magnitude * 1000.0
    return magnitude


def parse_liberty_pin_caps(lib_paths: Iterable[Path]) -> dict[str, dict[str, PinInfo]]:
    cells: dict[str, dict[str, PinInfo]] = {}
    for path in lib_paths:
        text = path.read_text(encoding="utf-8", errors="ignore")
        cap_scale = parse_cap_unit_to_pf(text)
        current_cell: str | None = None
        current_pin: str | None = None
        cell_depth = 0
        pin_depth = 0
        for raw_line in text.splitlines():
            line = raw_line.split("//", 1)[0].strip()
            if not line:
                continue

            if current_cell is None:
                match = re.search(r"cell\s*\(\s*\"?([^)\"]+)\"?\s*\)", line)
                if match:
                    current_cell = match.group(1).strip()
                    cells.setdefault(current_cell, {})
                    cell_depth = line.count("{") - line.count("}")
                continue

            cell_depth += line.count("{") - line.count("}")
            if current_pin is None:
                match = re.search(r"pin\s*\(\s*\"?([^)\"]+)\"?\s*\)", line)
                if match:
                    current_pin = match.group(1).strip()
                    cells[current_cell].setdefault(current_pin, PinInfo())
                    pin_depth = line.count("{") - line.count("}")
                    remainder = line[match.end() :]
                    update_pin_info(cells[current_cell][current_pin], remainder, cap_scale)
                    if pin_depth <= 0:
                        current_pin = None
                    continue
            else:
                update_pin_info(cells[current_cell][current_pin], line, cap_scale)
                pin_depth += line.count("{") - line.count("}")
                if pin_depth <= 0:
                    current_pin = None

            if cell_depth <= 0 and current_pin is None:
                current_cell = None
                cell_depth = 0
    return cells


def update_pin_info(pin_info: PinInfo, line: str, cap_scale: float) -> None:
    direction_match = re.search(r"direction\s*:\s*([a-zA-Z_]+)", line)
    if direction_match:
        pin_info.direction = direction_match.group(1).lower()
    cap_match = re.search(r"\bcapacitance\s*:\s*([0-9.eE+-]+)", line)
    if cap_match:
        pin_info.capacitance_pf = float(cap_match.group(1)) * cap_scale


def load_lib_paths(bench_root: Path, case_name: str) -> list[Path]:
    config_path = bench_root / "cases" / case_name / "run_config" / "db_default_config.json"
    if not config_path.exists():
        config_path = bench_root / "iEDA_config" / "db_default_config.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    return [Path(path) for path in config.get("INPUT", {}).get("lib_path", [])]


def iter_verilog_statements(verilog_path: Path) -> Iterable[str]:
    current: list[str] = []
    in_block_comment = False
    with verilog_path.open(encoding="utf-8", errors="ignore") as fp:
        for raw_line in fp:
            line = raw_line
            if in_block_comment:
                if "*/" in line:
                    line = line.split("*/", 1)[1]
                    in_block_comment = False
                else:
                    continue
            while "/*" in line:
                before, after = line.split("/*", 1)
                if "*/" in after:
                    line = before + after.split("*/", 1)[1]
                else:
                    line = before
                    in_block_comment = True
                    break
            line = line.split("//", 1)[0].strip()
            if not line:
                continue
            current.append(line)
            if ";" in line:
                joined = " ".join(current)
                parts = joined.split(";")
                for part in parts[:-1]:
                    yield part.strip()
                current = [parts[-1].strip()] if parts[-1].strip() else []


NET_NAME_RE = re.compile(r"\b(?:wire|tri)\s+([^;]+)")
CONNECTION_RE = re.compile(r"\.([A-Za-z_][A-Za-z0-9_$]*|\\[^\s()]+)\s*\(\s*([^)]+?)\s*\)")


def clean_connection_net(net: str) -> str:
    cleaned = net.strip()
    if cleaned.startswith("{") or "'" in cleaned or "," in cleaned:
        return ""
    return cleaned


def clock_net_names_from_verilog(verilog_path: Path, selected_clock: str) -> set[str]:
    clock_nets = {selected_clock} if selected_clock else set()
    for statement in iter_verilog_statements(verilog_path):
        if not statement.startswith(("wire ", "tri ")):
            continue
        match = NET_NAME_RE.search(statement + ";")
        if not match:
            continue
        names = match.group(1)
        names = re.sub(r"\[[^]]+\]", " ", names)
        for raw_name in names.split(","):
            name = raw_name.strip()
            if not name:
                continue
            if name.startswith("cts_") or "cts_flow" in name:
                clock_nets.add(name)
    return clock_nets


def calc_clock_pin_cap_fF(verilog_path: Path, selected_clock: str, lib_cells: dict[str, dict[str, PinInfo]]) -> tuple[float | None, str]:
    if not verilog_path.exists():
        return None, "missing final cts.v"
    clock_nets = clock_net_names_from_verilog(verilog_path, selected_clock)
    if not clock_nets:
        return None, "no clock nets found in cts.v"

    total_pf = 0.0
    unknown_caps = 0
    load_pins = 0
    cell_names = set(lib_cells)
    for statement in iter_verilog_statements(verilog_path):
        stripped = statement.strip()
        if not stripped or stripped.startswith(("module ", "endmodule", "input ", "output ", "inout ", "wire ", "assign ", "supply")):
            continue
        match = re.match(r"([A-Za-z_][A-Za-z0-9_$]*|\\[^\s]+)\s+([A-Za-z_][A-Za-z0-9_$]*|\\[^\s]+)\s*\((.*)\)\s*$", stripped)
        if not match:
            continue
        cell_name = match.group(1)
        if cell_name not in cell_names:
            continue
        port_blob = match.group(3)
        for port_name, net_name_raw in CONNECTION_RE.findall(port_blob):
            net_name = clean_connection_net(net_name_raw)
            if net_name not in clock_nets:
                continue
            pin_info = lib_cells[cell_name].get(port_name)
            if pin_info is None or pin_info.direction != "input":
                continue
            load_pins += 1
            if pin_info.capacitance_pf is None:
                unknown_caps += 1
            else:
                total_pf += pin_info.capacitance_pf

    if load_pins == 0:
        return None, f"no load pins matched {len(clock_nets)} clock nets"
    note = f"liberty_input_pin_caps: load_pins={load_pins}, unknown_caps={unknown_caps}, clock_nets={len(clock_nets)}"
    return total_pf * 1000.0, note


def parse_violation_report(path: Path, slack_column_name: str) -> tuple[int | None, str]:
    if not path.exists():
        return None, f"missing {path.name}"
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    header: list[str] | None = None
    slack_idx = -1
    count = 0
    for line in lines:
        row = split_table_row(line)
        if row is None:
            continue
        if header is None and slack_column_name in row:
            header = row
            slack_idx = row.index(slack_column_name)
            continue
        if header is None or slack_idx < 0 or len(row) <= slack_idx:
            continue
        first = row[0]
        if not first or first in {"Net / Pin"}:
            continue
        slack = row[slack_idx]
        if re.search(r"(^|/)-\d", slack):
            count += 1
    return count, "report_top_n_parse"


def find_one(directory: Path, suffix: str) -> Path | None:
    matches = sorted(directory.glob(f"*{suffix}"))
    return matches[0] if matches else None


def collect_case(bench_root: Path, case_name: str, status_rows: dict[str, dict[str, str]], lib_cache: dict[tuple[str, ...], dict[str, dict[str, PinInfo]]]) -> dict[str, str]:
    case_dir = bench_root / "cases" / case_name
    result_dir = case_dir / "result"
    cts_dir = result_dir / "cts"
    clock_name, clock_port, _manifest = load_clock_selection(case_dir)
    status = status_rows.get(case_name, {}).get("status", "not_run")
    error = status_rows.get(case_name, {}).get("error", "")
    notes: list[str] = []

    metrics, log_notes = parse_cts_log(cts_dir / "cts.log", clock_name)
    notes.extend(log_notes)
    cts_status = str(metrics.get("cts_status") or "")
    if status == "passed" and cts_status and cts_status != "finished":
        status = f"cts_{cts_status}"
        error = f"CTS Key Results status is {cts_status}"

    power_uW, power_source, power_note = parse_power_uW(cts_dir / "power")
    if power_note:
        notes.append(power_note)

    lib_paths = load_lib_paths(bench_root, case_name)
    lib_key = tuple(str(path) for path in lib_paths)
    if lib_key not in lib_cache:
        lib_cache[lib_key] = parse_liberty_pin_caps(lib_paths)
    pin_cap_fF, pin_cap_note = calc_clock_pin_cap_fF(result_dir / "cts.v", clock_name, lib_cache[lib_key])
    if pin_cap_note:
        notes.append(pin_cap_note)

    unit_cap = metrics.get("unit_capacitance_pf_per_um")
    clk_wl = metrics.get("Clk-WL (um)")
    wire_cap_fF = None
    if isinstance(unit_cap, float) and isinstance(clk_wl, float):
        wire_cap_fF = unit_cap * clk_wl * 1000.0
    else:
        notes.append("clock wire cap unavailable")

    clk_cap_fF = None
    clk_cap_source = "derived_from_liberty_and_cts_rc"
    if pin_cap_fF is not None and wire_cap_fF is not None:
        clk_cap_fF = pin_cap_fF + wire_cap_fF
    elif pin_cap_fF is not None:
        clk_cap_fF = pin_cap_fF
        clk_cap_source = "derived_pin_cap_only"
    elif wire_cap_fF is not None:
        clk_cap_fF = wire_cap_fF
        clk_cap_source = "derived_wire_cap_only"
    else:
        clk_cap_source = "unavailable"

    sta_dir = cts_dir / "sta"
    fanout_count, fanout_source = parse_violation_report(find_one(sta_dir, ".fanout") or sta_dir / "missing.fanout", "FanoutSlack")
    cap_count, cap_source = parse_violation_report(find_one(sta_dir, ".cap") or sta_dir / "missing.cap", "CapacitanceSlack")
    slew_count, slew_source = parse_violation_report(find_one(sta_dir, ".trans") or sta_dir / "missing.trans", "SlewSlack")
    violation_source = ",".join(sorted({fanout_source, cap_source, slew_source}))

    row: dict[str, str] = {
        "case_name": case_name,
        "clock_name": clock_name,
        "clock_port": clock_port,
        "status": status,
        "error": error,
        "power_source": power_source,
        "clk_cap_source": clk_cap_source,
        "violation_source": violation_source,
        "metric_notes": "; ".join(note for note in notes if note),
        "Pow. (uW)": fmt(power_uW),
        "Clk-Cap (fF)": fmt(clk_cap_fF),
        "clock_pin_cap_fF": fmt(pin_cap_fF),
        "clock_wire_cap_fF": fmt(wire_cap_fF),
        "#Fanout Vio.": "" if fanout_count is None else str(fanout_count),
        "#Cap Vio.": "" if cap_count is None else str(cap_count),
        "#Slew Vio.": "" if slew_count is None else str(slew_count),
    }
    for column in REQUESTED_COLUMNS:
        if column not in row:
            value = metrics.get(column)
            row[column] = fmt(value) if isinstance(value, (float, int)) else str(value or "")
    return row


def write_csv(path: Path, rows: list[dict[str, str]], columns: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    bench_root = args.bench_root.resolve()
    cases_dir = bench_root / "cases"
    all_cases = sorted(path.name for path in cases_dir.iterdir() if path.is_dir())
    selected_cases = sorted(set(args.cases or all_cases))
    status_rows = load_run_status(bench_root)
    lib_cache: dict[tuple[str, ...], dict[str, dict[str, PinInfo]]] = {}

    rows = [collect_case(bench_root, case_name, status_rows, lib_cache) for case_name in selected_cases]
    columns = REQUESTED_COLUMNS + EXTRA_COLUMNS
    write_csv(bench_root / "reports" / "cts_bench_summary.csv", rows, columns)
    failures = [row for row in rows if row.get("status") != "passed" or row.get("error")]
    write_csv(bench_root / "reports" / "cts_bench_failures.csv", failures, columns)
    print(f"Wrote {len(rows)} rows to {bench_root / 'reports' / 'cts_bench_summary.csv'}")
    if failures:
        print(f"Wrote {len(failures)} failure rows to {bench_root / 'reports' / 'cts_bench_failures.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
