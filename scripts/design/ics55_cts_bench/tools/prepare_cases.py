#!/usr/bin/env python3
"""Prepare one-directory-per-case iCTS benchmark inputs."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[4]
BENCH_ROOT = REPO_ROOT / "scripts" / "design" / "ics55_cts_bench"
SOURCE_ROOT = Path("/nfs/share/home/liweiguo/ecc_cts_test")


@dataclass
class DefPin:
    pin_name: str
    net_name: str
    direction: str
    use: str | None = None


@dataclass
class ClockCandidate:
    pin_name: str
    net_name: str
    net_connection_count: int
    load_count: int
    match_reason: str


@dataclass
class SdcTarget:
    clock_name: str
    target_kind: str
    target_name: str
    selection_reason: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=SOURCE_ROOT)
    parser.add_argument("--bench-root", type=Path, default=BENCH_ROOT)
    parser.add_argument("--force", action="store_true", help="overwrite existing case input files")
    return parser.parse_args()


def strip_def_comment(line: str) -> str:
    if line.lstrip().startswith("#"):
        return ""
    return line.rstrip()


def tokenize_def_entry(entry_lines: Iterable[str]) -> list[str]:
    text = " ".join(line.strip() for line in entry_lines)
    text = text.replace(";", " ")
    return text.split()


def parse_def_pins(def_path: Path) -> list[DefPin]:
    pins: list[DefPin] = []
    in_pins = False
    entry: list[str] = []

    with def_path.open(encoding="utf-8", errors="ignore") as fp:
        for raw_line in fp:
            line = strip_def_comment(raw_line).strip()
            if not line:
                continue
            if line.startswith("PINS "):
                in_pins = True
                continue
            if in_pins and line.startswith("END PINS"):
                break
            if not in_pins:
                continue

            if line.startswith("- "):
                entry = [line]
            elif entry:
                entry.append(line)

            if entry and ";" in line:
                tokens = tokenize_def_entry(entry)
                entry = []
                if len(tokens) < 2 or tokens[0] != "-":
                    continue
                pin_name = tokens[1]
                net_name = pin_name
                direction = ""
                use = None
                for idx, token in enumerate(tokens):
                    if token == "+" and idx + 2 < len(tokens):
                        key = tokens[idx + 1]
                        value = tokens[idx + 2]
                        if key == "NET":
                            net_name = value
                        elif key == "DIRECTION":
                            direction = value.upper()
                        elif key == "USE":
                            use = value.upper()
                pins.append(DefPin(pin_name=pin_name, net_name=net_name, direction=direction, use=use))

    return pins


CONNECTION_RE = re.compile(r"\(\s+([^\s()]+)\s+([^\s()]+)\s+\)")


def parse_def_net_connection_counts(def_path: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    in_nets = False
    current_net: str | None = None

    with def_path.open(encoding="utf-8", errors="ignore") as fp:
        for raw_line in fp:
            line = strip_def_comment(raw_line).strip()
            if not line:
                continue
            if line.startswith("NETS "):
                in_nets = True
                continue
            if in_nets and line.startswith("END NETS"):
                break
            if not in_nets:
                continue

            if line.startswith("- "):
                parts = line.replace(";", " ").split()
                current_net = parts[1] if len(parts) > 1 else None
                if current_net is not None:
                    counts.setdefault(current_net, 0)

            if current_net is not None:
                counts[current_net] = counts.get(current_net, 0) + len(CONNECTION_RE.findall(line))

            if ";" in line:
                current_net = None

    return counts


def clock_match_reason(pin: DefPin) -> str | None:
    names = [pin.pin_name, pin.net_name]
    for name in names:
        normalized = name.lstrip("\\")
        lower = normalized.lower()
        if lower in {"clk", "clock", "ck"}:
            return "exact_clock_name"
        if "clock" in lower:
            return "contains_clock"
        if "clk" in lower:
            return "contains_clk"
        if normalized == "CK":
            return "uppercase_ck"
    if pin.use == "CLOCK":
        return "def_use_clock"
    return None


def infer_clock_candidates(pins: list[DefPin], net_counts: dict[str, int]) -> tuple[list[ClockCandidate], str]:
    candidates: list[ClockCandidate] = []
    for pin in pins:
        if pin.direction != "INPUT":
            continue
        reason = clock_match_reason(pin)
        if reason is None:
            continue
        connection_count = net_counts.get(pin.net_name, 0)
        candidates.append(
            ClockCandidate(
                pin_name=pin.pin_name,
                net_name=pin.net_name,
                net_connection_count=connection_count,
                load_count=max(connection_count - 1, 0),
                match_reason=reason,
            )
        )

    if candidates:
        return candidates, "clock_name_match"

    input_pins = [pin for pin in pins if pin.direction == "INPUT"]
    if not input_pins:
        return [], "no_input_pins"

    # Last-resort safety net for unexpected cases: choose the largest input net
    # and make the degraded selection visible in the manifest.
    largest = max(input_pins, key=lambda pin: (net_counts.get(pin.net_name, 0), pin.pin_name))
    connection_count = net_counts.get(largest.net_name, 0)
    return [
        ClockCandidate(
            pin_name=largest.pin_name,
            net_name=largest.net_name,
            net_connection_count=connection_count,
            load_count=max(connection_count - 1, 0),
            match_reason="fallback_largest_input",
        )
    ], "fallback_largest_input"


def choose_clock(candidates: list[ClockCandidate]) -> ClockCandidate:
    reason_rank = {
        "exact_clock_name": 0,
        "uppercase_ck": 1,
        "contains_clock": 2,
        "contains_clk": 3,
        "def_use_clock": 4,
        "fallback_largest_input": 5,
    }
    return sorted(
        candidates,
        key=lambda cand: (
            -cand.net_connection_count,
            reason_rank.get(cand.match_reason, 99),
            cand.pin_name,
            cand.net_name,
        ),
    )[0]


def tcl_braced(value: str) -> str:
    return "{" + value.replace("}", r"\}") + "}"


def load_clock_overrides(bench_root: Path) -> dict[str, SdcTarget]:
    override_path = bench_root / "clock_overrides.json"
    if not override_path.exists():
        return {}

    raw_overrides = json.loads(override_path.read_text(encoding="utf-8"))
    overrides: dict[str, SdcTarget] = {}
    for case_name, data in raw_overrides.items():
        target = SdcTarget(
            clock_name=str(data["clock_name"]),
            target_kind=str(data["target_kind"]),
            target_name=str(data["target_name"]),
            selection_reason=str(data.get("selection_reason", "manual_clock_target_override")),
        )
        if target.target_kind not in {"port", "pin"}:
            raise SystemExit(f"Unsupported SDC target kind {target.target_kind!r} for case {case_name}")
        overrides[case_name] = target
    return overrides


def default_sdc_target(clock: ClockCandidate) -> SdcTarget:
    return SdcTarget(
        clock_name=clock.net_name,
        target_kind="port",
        target_name=clock.pin_name,
        selection_reason=clock.match_reason,
    )


def write_sdc(path: Path, target: SdcTarget) -> None:
    getter = "get_ports" if target.target_kind == "port" else "get_pins"
    variable_name = "clk_port_name" if target.target_kind == "port" else "clk_pin_name"
    target_variable = "clk_port" if target.target_kind == "port" else "clk_pin"
    path.write_text(
        "\n".join(
            [
                f"set clk_name  {tcl_braced(target.clock_name)}",
                f"set {variable_name} {tcl_braced(target.target_name)}",
                "set clk_expect_freq_mhz 100",
                "set clk_period [expr 1000.0 / $clk_expect_freq_mhz]",
                "set clk_io_pct 0.2",
                "",
                f"set {target_variable} [{getter} ${variable_name}]",
                "",
                f"create_clock -name $clk_name -period $clk_period  ${target_variable}",
                "",
            ]
        ),
        encoding="utf-8",
    )


def copy_case_file(src: Path, dst: Path, force: bool) -> None:
    if dst.exists() and not force:
        return
    shutil.copy2(src, dst)


def main() -> int:
    args = parse_args()
    def_dir = args.source_root / "def"
    verilog_dir = args.source_root / "verilog"
    cases_dir = args.bench_root / "cases"
    reports_dir = args.bench_root / "reports"
    cases_dir.mkdir(parents=True, exist_ok=True)
    reports_dir.mkdir(parents=True, exist_ok=True)

    def_files = {path.stem: path for path in sorted(def_dir.glob("*.def"))}
    verilog_files = {path.stem: path for path in sorted(verilog_dir.glob("*.v"))}
    missing_verilog = sorted(set(def_files) - set(verilog_files))
    missing_def = sorted(set(verilog_files) - set(def_files))
    if missing_verilog or missing_def:
        raise SystemExit(
            f"DEF/Verilog mismatch: missing_verilog={missing_verilog}, missing_def={missing_def}"
        )

    summary_rows: list[dict[str, object]] = []
    manifest: list[dict[str, object]] = []
    clock_overrides = load_clock_overrides(args.bench_root)

    for case_name in sorted(def_files):
        case_dir = cases_dir / case_name
        case_dir.mkdir(parents=True, exist_ok=True)
        copy_case_file(def_files[case_name], case_dir / "place.def", args.force)
        copy_case_file(verilog_files[case_name], case_dir / "place.v", args.force)

        pins = parse_def_pins(def_files[case_name])
        net_counts = parse_def_net_connection_counts(def_files[case_name])
        candidates, selection_policy = infer_clock_candidates(pins, net_counts)
        if not candidates:
            raise SystemExit(f"No clock candidate found for case {case_name}")
        selected = choose_clock(candidates)
        sdc_target = clock_overrides.get(case_name, default_sdc_target(selected))
        write_sdc(case_dir / "default.sdc", sdc_target)

        case_manifest = {
            "case_name": case_name,
            "selection_policy": selection_policy,
            "selected_clock": asdict(selected),
            "sdc_target": asdict(sdc_target),
            "candidates": [asdict(candidate) for candidate in candidates],
        }
        (case_dir / "clock_selection.json").write_text(
            json.dumps(case_manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        manifest.append(case_manifest)
        summary_rows.append(
            {
                "case_name": case_name,
                "clock_name": sdc_target.clock_name,
                "clock_port": sdc_target.target_name if sdc_target.target_kind == "port" else "",
                "clock_pin": sdc_target.target_name if sdc_target.target_kind == "pin" else "",
                "sdc_target_kind": sdc_target.target_kind,
                "net_connection_count": selected.net_connection_count,
                "load_count": selected.load_count,
                "candidate_count": len(candidates),
                "selection_policy": selection_policy,
                "match_reason": sdc_target.selection_reason,
            }
        )

    (reports_dir / "clock_selection.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    with (reports_dir / "clock_selection.csv").open("w", newline="", encoding="utf-8") as fp:
        fieldnames = [
            "case_name",
            "clock_name",
            "clock_port",
            "clock_pin",
            "sdc_target_kind",
            "net_connection_count",
            "load_count",
            "candidate_count",
            "selection_policy",
            "match_reason",
        ]
        writer = csv.DictWriter(fp, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(summary_rows)

    print(f"Prepared {len(summary_rows)} cases under {cases_dir}")
    print(f"Wrote clock-selection reports under {reports_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
