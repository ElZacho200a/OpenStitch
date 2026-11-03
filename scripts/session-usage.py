#!/usr/bin/env python3
"""Mesure la consommation de sessions Claude Code depuis les transcripts locaux (lecture seule).

Usage : python scripts/session-usage.py [--top N] [--since AAAA-MM-JJ (date de dernière modification)] <dossier-ou-fichiers.jsonl>...
Dossier typique : ~/.claude/projects/<projet-encodé>/ (un .jsonl par session).

Rapporte, par session puis au total : tokens d'entrée / cache écrit / cache lu / sortie
(dernier `usage` de chaque message, doublons de streaming écartés), appels d'outils,
sous-agents (avec leur modèle), lectures de fichiers sans `limit`, et volume de résultats d'outils
par outil (caractères, proxy de la place prise dans le contexte).
"""
import argparse, collections, glob, json, os, sys
from datetime import datetime


def iter_files(paths):
    for p in paths:
        if os.path.isdir(p):
            yield from sorted(glob.glob(os.path.join(p, "**", "*.jsonl"), recursive=True))
        else:
            yield p


def analyse(path):
    usage = {}
    calls, agents, bigreads = collections.Counter(), [], []
    result_chars = collections.Counter()
    tool_names = {}
    first_ts = None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            try:
                d = json.loads(line)
            except ValueError:
                continue
            first_ts = first_ts or d.get("timestamp")
            msg = d.get("message") or {}
            content = msg.get("content")
            if d.get("type") == "assistant" and isinstance(msg, dict):
                if msg.get("usage") and msg.get("id"):
                    usage[msg["id"]] = msg["usage"]
                for b in content if isinstance(content, list) else []:
                    if b.get("type") != "tool_use":
                        continue
                    name, inp = b.get("name", "?"), b.get("input") or {}
                    calls[name] += 1
                    tool_names[b.get("id")] = name
                    if name in ("Agent", "Task"):
                        agents.append(inp.get("model") or inp.get("subagent_type") or "défaut")
                    if name == "Read" and "limit" not in inp and "pages" not in inp:
                        bigreads.append(inp.get("file_path", "?"))
            elif d.get("type") == "user" and isinstance(content, list):
                for b in content:
                    if b.get("type") == "tool_result":
                        c = b.get("content")
                        n = len(c) if isinstance(c, str) else sum(len(x.get("text", "")) for x in c or [] if isinstance(x, dict))
                        result_chars[tool_names.get(b.get("tool_use_id"), "?")] += n
    tot = collections.Counter()
    for u in usage.values():
        tot["input"] += u.get("input_tokens", 0)
        tot["cache_write"] += u.get("cache_creation_input_tokens", 0)
        tot["cache_read"] += u.get("cache_read_input_tokens", 0)
        tot["output"] += u.get("output_tokens", 0)
    tot["requests"] = len(usage)
    return dict(path=path, ts=first_ts, tot=tot, calls=calls, agents=agents, bigreads=bigreads, results=result_chars)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--top", type=int, default=10)
    ap.add_argument("--since")
    a = ap.parse_args()
    rows = []
    for f in iter_files(a.paths):
        r = analyse(f)
        if a.since and datetime.fromtimestamp(os.path.getmtime(f)).strftime("%Y-%m-%d") < a.since:
            continue
        if r["tot"]["requests"]:
            rows.append(r)
    rows.sort(key=lambda r: r["tot"]["cache_read"] + r["tot"]["cache_write"] + r["tot"]["output"], reverse=True)
    print(f"{len(rows)} sessions analysées")
    print(f"{'session':14} {'req':>5} {'input':>9} {'c.write':>10} {'c.read':>12} {'output':>9} {'agents':>6} {'outils':>6}")
    for r in rows[: a.top]:
        t = r["tot"]
        print(f"{os.path.basename(r['path'])[:12]:14} {t['requests']:>5} {t['input']:>9} {t['cache_write']:>10} {t['cache_read']:>12} {t['output']:>9} {len(r['agents']):>6} {sum(r['calls'].values()):>6}")
    T, C, A, R, B = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    for r in rows:
        T.update(r["tot"]); C.update(r["calls"]); A.update(r["agents"]); R.update(r["results"]); B.update(r["bigreads"])
    print("\nTOTAL", dict(T))
    print("Appels d'outils :", dict(C.most_common(8)))
    print("Sous-agents par modèle/type :", dict(A.most_common()))
    print("Résultats d'outils (Mcar) :", {k: round(v / 1e6, 2) for k, v in R.most_common(6)})
    print("Lectures sans limit les plus fréquentes :")
    for p, n in B.most_common(8):
        print(f"  {n:>4}  {p}")


if __name__ == "__main__":
    sys.exit(main())
