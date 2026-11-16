#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rapport d'usage d'OpenStitch à partir du journal local (JSON Lines).

Le journal est écrit par Studio dans `<données locales>/OpenStitch/usage/usage-AAAA-MM.jsonl`
(Windows : %LOCALAPPDATA%\\OpenStitch\\usage). Rien n'est envoyé : ce script ne fait que lire.

    python scripts/usage_report.py                      # dossier par défaut
    python scripts/usage_report.py --dir D:\\kpi --top 20
    python scripts/usage_report.py --since 2026-11-01 --json > kpi.json

Plusieurs dossiers (une machine = un dossier) : passer `--dir` plusieurs fois.
Format et confidentialité : docs/source/usage-log.md.
"""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import json
import os
import sys
from pathlib import Path
from typing import Iterable, Iterator

KINDS = ("click", "tool", "action", "space")


def default_dir(env: dict[str, str] | None = None, platform: str | None = None) -> Path:
    env = os.environ if env is None else env
    platform = sys.platform if platform is None else platform
    if env.get("OPENSTITCH_USAGE_DIR"):
        return Path(env["OPENSTITCH_USAGE_DIR"])
    if platform.startswith("win"):
        base = env.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
    elif platform == "darwin":
        base = str(Path.home() / "Library" / "Application Support")
    else:
        base = env.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "OpenStitch" / "usage"


def read_events(dirs: Iterable[Path]) -> tuple[list[dict], int]:
    """Événements valides de tous les fichiers `usage-*.jsonl`, et nombre de lignes ignorées."""
    events: list[dict] = []
    skipped = 0
    for folder in dirs:
        for file in sorted(Path(folder).glob("usage-*.jsonl")):
            with file.open(encoding="utf-8", errors="replace") as handle:
                for raw in handle:
                    raw = raw.strip()
                    if not raw:
                        continue
                    try:
                        event = json.loads(raw)
                    except json.JSONDecodeError:
                        skipped += 1
                        continue
                    if not isinstance(event, dict) or not event.get("k") or not event.get("id"):
                        skipped += 1
                        continue
                    events.append(event)
    return events, skipped


def _day(event: dict) -> str:
    return str(event.get("t", ""))[:10]


def summarize(events: list[dict], top: int = 10, since: str | None = None) -> dict:
    if since:
        events = [e for e in events if _day(e) >= since]
    sessions = {e.get("sid") for e in events if e.get("sid")}
    days = collections.Counter(_day(e) for e in events if _day(e))
    by_kind: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    by_version: dict[str, dict] = {}
    for e in events:
        by_kind[e["k"]][e["id"]] += 1
        version = f'{e.get("app", "?")} {e.get("ver", "?")}'
        slot = by_version.setdefault(version, {"events": 0, "sessions": set()})
        slot["events"] += 1
        if e.get("sid"):
            slot["sessions"].add(e["sid"])
    per_session = collections.Counter(e.get("sid") for e in events if e.get("sid"))
    return {
        "events": len(events),
        "sessions": len(sessions),
        "active_days": len(days),
        "first_day": min(days) if days else None,
        "last_day": max(days) if days else None,
        "events_per_session_median": (
            sorted(per_session.values())[len(per_session) // 2] if per_session else 0
        ),
        "top": {
            kind: [{"id": i, "count": n} for i, n in counter.most_common(top)]
            for kind, counter in sorted(by_kind.items())
        },
        "versions": {
            v: {"events": s["events"], "sessions": len(s["sessions"])}
            for v, s in sorted(by_version.items())
        },
        "per_day": dict(sorted(days.items())),
    }


def render_text(summary: dict, skipped: int = 0) -> str:
    if summary["events"] == 0:
        return "Aucun événement dans le journal."
    lines = [
        f'Événements : {summary["events"]}   Sessions : {summary["sessions"]}   '
        f'Jours actifs : {summary["active_days"]} ({summary["first_day"]} → {summary["last_day"]})',
        f'Événements par session (médiane) : {summary["events_per_session_median"]}',
    ]
    if skipped:
        lines.append(f"Lignes ignorées (illisibles) : {skipped}")
    lines.append("")
    lines.append("Versions :")
    for version, slot in summary["versions"].items():
        lines.append(f'  {version:<32} {slot["events"]:>7} événements  {slot["sessions"]:>4} sessions')
    for kind, rows in summary["top"].items():
        lines.append("")
        lines.append(f"Top « {kind} » :")
        for row in rows:
            lines.append(f'  {row["count"]:>7}  {row["id"]}')
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Rapport d'usage OpenStitch (lecture seule).")
    parser.add_argument("--dir", action="append", type=Path, help="dossier du journal (répétable)")
    parser.add_argument("--top", type=int, default=10, help="lignes par classement")
    parser.add_argument("--since", help="date minimale AAAA-MM-JJ")
    parser.add_argument("--json", action="store_true", help="sortie JSON")
    args = parser.parse_args(argv)
    if args.since:
        try:
            dt.date.fromisoformat(args.since)
        except ValueError:
            parser.error("--since attend une date AAAA-MM-JJ")
    dirs = args.dir or [default_dir()]
    events, skipped = read_events(dirs)
    summary = summarize(events, top=max(1, args.top), since=args.since)
    if args.json:
        print(json.dumps({**summary, "skipped_lines": skipped}, ensure_ascii=False, indent=2))
    else:
        print(render_text(summary, skipped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
