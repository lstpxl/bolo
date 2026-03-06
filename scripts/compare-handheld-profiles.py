#!/usr/bin/env python3
"""Compare two handheld profile logs and print key telemetry deltas.

Usage:
  python3 scripts/compare-handheld-profiles.py docs/handheld-profile-22.log docs/handheld-profile-23.log
"""

from __future__ import annotations

import argparse
import re
import statistics
from pathlib import Path
from typing import Dict, List, Tuple


PatternMap = Dict[str, re.Pattern[str]]


PATTERNS: PatternMap = {
    "stats": re.compile(
        r"\[ENEMY_STATS\].*?alive=(\d+)\s+visible=(\d+)\s+full=(\d+)\s+cheap=(\d+).*?"
        r"pairs\(frontal=(\d+)\s+checks=(\d+)\s+separation=(\d+)\s+resolved=(\d+)\)"
    ),
    "window": re.compile(r"\[ENEMY_WINDOW\].*?collisionPasses\[runs=(\d+)\s+skips=(\d+)\]"),
    "grid": re.compile(
        r"\[ENEMY_GRID\]\s+frontal\{cand/s=([0-9.]+)\s+xcell/s=([0-9.]+)\s+ins/s=([0-9.]+)\}\s+"
        r"separation\{cand/s=([0-9.]+)\}"
    ),
    "collision": re.compile(
        r"\[ENEMY_COLLISION_WINDOW\]\s+frontal\{pairs/s=([0-9.]+)\s+checks/s=([0-9.]+)\s+"
        r"baseSkip=(\d+)\s+kill=(\d+)\([0-9.]+%\)\}\s+"
        r"separation\{pairs/s=([0-9.]+)\s+resolved/s=([0-9.]+)\([0-9.]+%\)\s+"
        r"baseSkip=(\d+)\s+kill=(\d+)\s+wallBlock=(\d+)\}"
    ),
    "nav": re.compile(
        r"\[ENEMY_NAV_CACHE\]\s+playerCell\{changes=(\d+)\s+flowRebuilds=(\d+)\}\s+"
        r"flow\{hit=(\d+)\s+miss=(\d+)\s+hit%=([0-9.]+)\}\s+"
        r"pathFallback\{calls=(\d+)\s+ok=(\d+)\s+ok%=([0-9.]+)\}"
    ),
    "sap": re.compile(
        r"\[ENEMY_SAP\]\s+updates=(\d+)\s+active=(\d+)\s+pairs=(\d+)\s+repairs\{x=(\d+)\s+y=(\d+)\}"
    ),
}


def _mean(values: List[float]) -> float:
    return statistics.mean(values) if values else 0.0


def _max(values: List[float]) -> float:
    return max(values) if values else 0.0


def parse_log(path: Path) -> Dict[str, List[Tuple[float, ...]]]:
    result: Dict[str, List[Tuple[float, ...]]] = {k: [] for k in PATTERNS}
    for line in path.read_text(errors="ignore").splitlines():
        for key, pattern in PATTERNS.items():
            m = pattern.search(line)
            if m:
                result[key].append(tuple(float(v) for v in m.groups()))
    return result


def summarize(label: str, data: Dict[str, List[Tuple[float, ...]]]) -> None:
    print(f"=== {label} ===")
    print("samples:", {k: len(v) for k, v in data.items()})

    if data["stats"]:
        alive = [r[0] for r in data["stats"]]
        full = [r[2] for r in data["stats"]]
        visible = [r[1] for r in data["stats"]]
        print(f"alive mean/max/min: {_mean(alive):.2f}/{_max(alive):.0f}/{min(alive):.0f}")
        print(f"full mean/max/nonzero: {_mean(full):.2f}/{_max(full):.0f}/{sum(v > 0 for v in full)}")
        print(f"visible mean/max: {_mean(visible):.2f}/{_max(visible):.0f}")

    if data["window"]:
        runs = [r[0] for r in data["window"]]
        skips = [r[1] for r in data["window"]]
        print(f"collision passes runs/skips total: {sum(runs):.0f}/{sum(skips):.0f}")

    if data["grid"]:
        frontal = [r[0] for r in data["grid"]]
        print(
            f"grid frontal cand/s mean/max/nonzero: "
            f"{_mean(frontal):.2f}/{_max(frontal):.1f}/{sum(v > 0 for v in frontal)}"
        )

    if data["collision"]:
        frontal_pairs = [r[0] for r in data["collision"]]
        frontal_checks = [r[1] for r in data["collision"]]
        frontal_kills = [r[3] for r in data["collision"]]
        separation_pairs = [r[4] for r in data["collision"]]
        separation_resolved = [r[5] for r in data["collision"]]
        separation_kills = [r[7] for r in data["collision"]]
        print(
            f"frontal pairs/s mean/max/nonzero: "
            f"{_mean(frontal_pairs):.2f}/{_max(frontal_pairs):.1f}/{sum(v > 0 for v in frontal_pairs)}"
        )
        print(
            f"frontal checks/s mean/max/nonzero: "
            f"{_mean(frontal_checks):.2f}/{_max(frontal_checks):.1f}/{sum(v > 0 for v in frontal_checks)}"
        )
        print(f"frontal kills total: {sum(frontal_kills):.0f}")
        print(
            f"separation pairs/s mean/max/nonzero: "
            f"{_mean(separation_pairs):.2f}/{_max(separation_pairs):.1f}/{sum(v > 0 for v in separation_pairs)}"
        )
        print(
            f"separation resolved/s mean/max/nonzero: "
            f"{_mean(separation_resolved):.2f}/{_max(separation_resolved):.1f}/"
            f"{sum(v > 0 for v in separation_resolved)}"
        )
        print(f"separation kills total: {sum(separation_kills):.0f}")

    if data["nav"]:
        flow_hit = [r[2] for r in data["nav"]]
        flow_miss = [r[3] for r in data["nav"]]
        flow_hit_pct = [r[4] for r in data["nav"]]
        path_calls = [r[5] for r in data["nav"]]
        print(
            f"flow hit/miss totals: {sum(flow_hit):.0f}/{sum(flow_miss):.0f} "
            f"(avg hit% {_mean(flow_hit_pct):.2f})"
        )
        print(f"path fallback calls total: {sum(path_calls):.0f}")

    if data["sap"]:
        updates = [r[0] for r in data["sap"]]
        pairs = [r[2] for r in data["sap"]]
        repairs = [r[3] + r[4] for r in data["sap"]]
        print(
            f"sap updates mean/max/nonzero: "
            f"{_mean(updates):.1f}/{_max(updates):.0f}/{sum(v > 0 for v in updates)}"
        )
        print(f"sap pairs mean/max/nonzero: {_mean(pairs):.2f}/{_max(pairs):.0f}/{sum(v > 0 for v in pairs)}")
        print(f"sap repairs mean: {_mean(repairs):.2f}")

    print()


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare two handheld profile logs.")
    parser.add_argument("baseline", type=Path, help="Baseline log (e.g. old wiring)")
    parser.add_argument("candidate", type=Path, help="Candidate log (e.g. new wiring)")
    args = parser.parse_args()

    baseline = parse_log(args.baseline)
    candidate = parse_log(args.candidate)
    summarize(str(args.baseline), baseline)
    summarize(str(args.candidate), candidate)


if __name__ == "__main__":
    main()
