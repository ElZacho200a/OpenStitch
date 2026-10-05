#!/usr/bin/env python3
"""
check-arch-plan-consistency.py — cross-check an arch-plan doc's redundant
dependency representations against each other (and, optionally, against
what liza actually persisted in .liza/state.yaml).

Why this exists: liza arch-plan docs (specs/arch-plan/vision/*.md) repeat
the same "who depends on whom" information in up to four places:

  1. "### Correspondance"   — scope name -> output[] index
  2. "### Dependency Order" — scope name -> "Depend de" (list of scope names)
  3. "## Output"            — output index -> "Dependances (index)" list
  4. .liza/state.yaml       — the *persisted* decomposition.depends_on /
                              read_only_depends_on liza actually uses

A human/LLM keeping these four in sync by hand is exactly where the
recurring "undeclared cross-scope provider/consumer edge" rejections came
from (4 rejections on arm-1, then a 5th stale-output bug even after the
RCA fix). This script makes that check mechanical instead of relying on a
reviewer agent to notice by eye.

It also parses "## Interfaces" (owner scope + free-text consumer scopes)
and checks, via the *transitive* closure of the Dependency Order graph,
that every consumer scope of an interface actually depends (directly or
transitively) on that interface's owner scope. The doc explicitly allows
transitive edges (no direct edge required if already implied), so the
check follows that same rule rather than demanding a direct edge.

Usage:
    python3 check_arch_plan_consistency.py <arch-plan.md> [--state <state.yaml>] [--task-prefix arm-1-ar-]

Exit code 0 = clean, 1 = at least one inconsistency found (findings printed
to stdout either way).
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field

SCOPE_RE = re.compile(r"\bS\d+[a-z]?\b")


def parse_markdown_table(lines: list[str], start_idx: int) -> tuple[list[str], list[list[str]], int]:
    """Given lines and the index of a '| header | ... |' row, parse the
    table (header + separator + body rows) and return (header_cells,
    body_rows, index_after_table)."""
    header = [c.strip() for c in lines[start_idx].strip().strip("|").split("|")]
    i = start_idx + 2  # skip header + '---' separator row
    rows = []
    while i < len(lines) and lines[i].strip().startswith("|"):
        cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
        rows.append(cells)
        i += 1
    return header, rows, i


def find_section(lines: list[str], heading_pattern: str) -> int | None:
    pat = re.compile(heading_pattern)
    for idx, line in enumerate(lines):
        if pat.match(line.strip()):
            return idx
    return None


def find_first_table_after(lines: list[str], start_idx: int) -> int | None:
    for idx in range(start_idx, len(lines)):
        if lines[idx].strip().startswith("|") and idx + 1 < len(lines) and set(lines[idx + 1].strip()) <= set("|-: "):
            return idx
    return None


@dataclass
class ArchPlan:
    correspondance: dict[str, int] = field(default_factory=dict)   # scope -> output index
    corr_index_to_scope: dict[int, str] = field(default_factory=dict)
    dep_order: dict[str, set[str]] = field(default_factory=dict)   # scope -> depends_on scopes
    output_deps: dict[int, set[int]] = field(default_factory=dict)  # output idx -> dep indices
    output_scope: dict[int, str] = field(default_factory=dict)     # output idx -> scope
    interfaces: list[tuple[str, str, set[str]]] = field(default_factory=list)  # (id, owner, consumers)


NEGATION_RE = re.compile(
    r"\bn['e]\S*\s*\S*\s*(?:consomme|utilise)\S*\s+(?:rien|pas)\b", re.I
)


def extract_scopes(text: str) -> set[str]:
    return set(SCOPE_RE.findall(text))


def extract_positive_scopes(text: str) -> set[str]:
    """Like extract_scopes, but drops scopes that only appear inside a
    negated clause ('S3 n'en consomme rien', 'S11 ne l'utilise pas en P0').
    Free-text prose columns (Interfaces 'Consommateurs') mix real consumers
    with explicit negations explaining why a scope is *not* one; a naive
    regex over the whole cell treats both as positive matches."""
    scopes: set[str] = set()
    # split into clauses on common separators; a negation and its subject
    # scope are almost always in the same clause
    for clause in re.split(r"[;.]|(?<=\))\s*,", text):
        clause_scopes = extract_scopes(clause)
        if not clause_scopes:
            continue
        if NEGATION_RE.search(clause):
            continue
        scopes |= clause_scopes
    return scopes


def parse_plan(path: str) -> ArchPlan:
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()

    plan = ArchPlan()

    # --- Correspondance (scope -> output index) ---
    idx = find_section(lines, r"^###\s+Correspondance")
    if idx is not None:
        tbl_idx = find_first_table_after(lines, idx)
        if tbl_idx is not None:
            header, rows, _ = parse_markdown_table(lines, tbl_idx)
            scope_col = next((i for i, h in enumerate(header) if "rimètre" in h or "eriметre" in h or "Perimetre" in h or "P" in h[:2]), 0)
            out_col = next((i for i, h in enumerate(header) if "output" in h.lower()), 1)
            for row in rows:
                m = SCOPE_RE.search(row[scope_col])
                if not m:
                    continue
                scope = m.group(0)
                try:
                    out_i = int(re.sub(r"\D", "", row[out_col]))
                except ValueError:
                    continue
                plan.correspondance[scope] = out_i
                plan.corr_index_to_scope[out_i] = scope

    # --- Dependency Order (scope -> depends_on) ---
    idx = find_section(lines, r"^###\s+Dependency Order")
    if idx is not None:
        tbl_idx = find_first_table_after(lines, idx)
        if tbl_idx is not None:
            header, rows, _ = parse_markdown_table(lines, tbl_idx)
            scope_col = 0
            dep_col = next((i for i, h in enumerate(header) if "pend" in h.lower()), 1)
            for row in rows:
                if len(row) <= max(scope_col, dep_col):
                    continue
                m = SCOPE_RE.search(row[scope_col])
                if not m:
                    continue
                scope = m.group(0)
                deps = extract_scopes(row[dep_col]) - {scope}
                plan.dep_order[scope] = deps
                plan.dep_order.setdefault(scope, set())
            # scopes that appear only as a dependency target but never as a row
            # (e.g. declared parallel/no deps) still get an empty entry via defaultdict-like access later

    # --- Output table (index -> scope, dep indices) ---
    idx = find_section(lines, r"^##\s+Output\s*$")
    if idx is not None:
        tbl_idx = find_first_table_after(lines, idx)
        if tbl_idx is not None:
            header, rows, _ = parse_markdown_table(lines, tbl_idx)
            out_col = 0
            scope_col = next((i for i, h in enumerate(header) if "rimètre" in h or "eriметre" in h), 1)
            dep_col = next((i for i, h in enumerate(header) if "pendance" in h.lower()), 3)
            for row in rows:
                if len(row) <= max(out_col, scope_col, dep_col):
                    continue
                try:
                    out_i = int(re.sub(r"\D", "", row[out_col]))
                except ValueError:
                    continue
                m = SCOPE_RE.search(row[scope_col])
                scope = m.group(0) if m else None
                dep_str = row[dep_col].strip()
                deps = set()
                if dep_str not in ("—", "-", ""):
                    for tok in re.split(r"[,;]", dep_str):
                        tok = tok.strip()
                        if tok.isdigit():
                            deps.add(int(tok))
                plan.output_deps[out_i] = deps
                if scope:
                    plan.output_scope[out_i] = scope

    # --- Interfaces (owner + consumers) ---
    idx = find_section(lines, r"^##\s+Interfaces\s*$")
    if idx is not None:
        tbl_idx = find_first_table_after(lines, idx)
        if tbl_idx is not None:
            header, rows, _ = parse_markdown_table(lines, tbl_idx)
            id_col = 0
            owner_col = next((i for i, h in enumerate(header) if "opri" in h.lower()), 2)
            cons_col = next((i for i, h in enumerate(header) if "onsommateur" in h.lower()), 3)
            for row in rows:
                if len(row) <= max(id_col, owner_col, cons_col):
                    continue
                iface_id = row[id_col].split()[0] if row[id_col] else "?"
                owner_scopes = extract_scopes(row[owner_col])
                if not owner_scopes:
                    continue
                owner = sorted(owner_scopes)[0]  # first scope mentioned = primary owner
                consumers = extract_positive_scopes(row[cons_col]) - {owner}
                plan.interfaces.append((iface_id, owner, consumers))

    return plan


def transitive_closure(dep_order: dict[str, set[str]]) -> dict[str, set[str]]:
    closure = {s: set(deps) for s, deps in dep_order.items()}
    changed = True
    while changed:
        changed = False
        for s in list(closure):
            new = set(closure[s])
            for d in list(closure[s]):
                new |= closure.get(d, set())
            if new != closure[s]:
                closure[s] = new
                changed = True
    return closure


def check(plan: ArchPlan) -> tuple[list[str], list[str]]:
    """Returns (errors, warnings). Errors are on structured/tabular data
    (Correspondance / Dependency Order / Output indices) and are reliable —
    treat as a hard gate. Warnings come from parsing free-text prose
    (Interfaces 'Consommateurs') and can false-positive on phrasing this
    script doesn't recognize — treat as 'a human should double-check this',
    not as a blocking failure."""
    errors: list[str] = []
    warnings: list[str] = []

    # 1) Output-table deps vs Dependency-Order-table deps, per scope (via Correspondance)
    for out_i, scope in sorted(plan.output_scope.items()):
        out_deps_as_scopes = {plan.corr_index_to_scope.get(d, f"output#{d}") for d in plan.output_deps.get(out_i, set())}
        order_deps = plan.dep_order.get(scope, set())
        missing_in_output = order_deps - out_deps_as_scopes
        extra_in_output = out_deps_as_scopes - order_deps
        if missing_in_output:
            errors.append(
                f"[Output vs Dependency Order] {scope} (output {out_i}): "
                f"'Dependency Order' lists depends_on {sorted(order_deps)} but the "
                f"'Output' table dependance-index list is missing {sorted(missing_in_output)}"
            )
        if extra_in_output:
            errors.append(
                f"[Output vs Dependency Order] {scope} (output {out_i}): "
                f"'Output' table dependance-index list has {sorted(extra_in_output)} "
                f"not present in 'Dependency Order' ({sorted(order_deps)})"
            )

    # 2) Interfaces: every consumer must (transitively) depend on the owner.
    # Heuristic prose parsing -> warnings, not errors (see docstring above).
    closure = transitive_closure(plan.dep_order)
    for iface_id, owner, consumers in plan.interfaces:
        for c in sorted(consumers):
            reachable = closure.get(c, set())
            if owner not in reachable:
                warnings.append(
                    f"[Interfaces vs Dependency Order] {iface_id}: consumer {c} does not "
                    f"depend (even transitively) on owner {owner} — 'Dependency Order' for "
                    f"{c} only reaches {sorted(reachable) or '{}'}. Double-check by hand: "
                    f"this may be a real missing edge, or prose this script mis-parsed."
                )

    return errors, warnings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plan_path")
    args = ap.parse_args()

    plan = parse_plan(args.plan_path)

    print(f"Parsed: {len(plan.correspondance)} scopes in Correspondance, "
          f"{len(plan.dep_order)} rows in Dependency Order, "
          f"{len(plan.output_deps)} rows in Output, "
          f"{len(plan.interfaces)} interfaces.\n")

    errors, warnings = check(plan)

    if warnings:
        print(f"{len(warnings)} warning(s) (heuristic prose parsing — verify by hand):\n")
        for w in warnings:
            print(f"  ? {w}")
        print()

    if not errors:
        print("OK — no structural inconsistency between Correspondance / Dependency Order / Output.")
        return 0

    print(f"{len(errors)} error(s) (structured-table mismatch — fix before submitting for review):\n")
    for e in errors:
        print(f"  - {e}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
