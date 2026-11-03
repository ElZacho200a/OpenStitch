#!/usr/bin/env python3
"""Edits OSP par lots controles en s'appuyant uniquement sur le CLI officiel."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

SCHEMA = 'openstitch.agent-bulk/1'
OPERATIONS = {'set_params', 'set_color', 'auto_satin', 'set_stitch_type'}


def run(cli: str, *args: str, codes: tuple[int, ...] = (0,)) -> dict:
    try:
        process = subprocess.run([cli, *args], capture_output=True, text=True,
                                 encoding='utf-8', errors='replace', timeout=240)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f'Execution CLI impossible : {exc}') from exc
    try:
        result = json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f'JSON CLI non valide : {process.stderr[:250]}') from exc
    if process.returncode not in codes:
        raise RuntimeError(f'Code {process.returncode} pour {" ".join(args[:4])}: {str(result.get("error"))[:300]}')
    return result


def select(objects: list[dict], *, kind: str | None, color: str | None,
           min_area: float | None, max_area: float | None,
           ids: set[int] | None) -> list[dict]:
    chosen = []
    for o in objects:
        if kind and o.get('type') != kind:
            continue
        if color and str(o.get('color', '')).lower() != color.lower():
            continue
        area = float(o.get('region_area_mm2') or 0)
        if min_area is not None and area < min_area:
            continue
        if max_area is not None and area > max_area:
            continue
        if ids is not None and o.get('id') not in ids:
            continue
        if o.get('locked'):
            continue
        chosen.append(o)
    return sorted(chosen, key=lambda o: o['id'])


def make_ops(chosen: list[dict], operation: str, params: dict) -> list[dict]:
    ops = []
    for o in chosen:
        if operation == 'set_params':
            ops.append({'op': 'set_params', 'object': o['id'], 'params': params})
        elif operation == 'set_color':
            ops.append({'op': 'set_color', 'object': o['id'], **params})
        elif operation == 'set_stitch_type':
            ops.append({'op': 'set_stitch_type', 'object': o['id'], **params})
        elif operation == 'auto_satin':
            if o.get('type') == 'auto_satin':
                value = dict(params)
                ops.append({'op': 'set_params', 'object': o['id'], 'params': value})
            else:
                ops.append({'op': 'auto_satin', 'object': o['id'], **params})
    return ops


def edit(cli: str, project: Path, dest: Path, *, kind: str | None,
         color: str | None, min_area: float | None, max_area: float | None,
         ids: set[int] | None, max_objects: int, operation: str,
         params: dict, commit: bool) -> dict:
    if dest.exists() or dest.with_suffix('.batch.json').exists():
        raise RuntimeError('Sortie existante : refus d ecrasement')
    if dest.resolve() == project.resolve():
        raise RuntimeError('Le projet source doit rester inchange')
    inspected = run(cli, 'project', 'inspect', str(project), '--json')
    if inspected.get('schema') != 'openstitch.project-inspect/1':
        raise RuntimeError('Schema project inspect incorrect')
    chosen = select(inspected.get('embroidery_objects', []), kind=kind, color=color,
                    min_area=min_area, max_area=max_area, ids=ids)
    if not chosen:
        raise RuntimeError('Aucun objet modifiable ne correspond aux filtres')
    if len(chosen) > max_objects:
        raise RuntimeError(f'{len(chosen)} objets selectionnes ; --max-objects {max_objects} trop bas')
    ops = make_ops(chosen, operation, params)
    dest.parent.mkdir(parents=True, exist_ok=True)
    batch = dest.with_suffix('.batch.json')
    batch.write_text(json.dumps({'schema': 'openstitch.batch/1', 'operations': ops},
                                ensure_ascii=False, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    dry = run(cli, 'project', 'apply', str(project), str(batch), '--dry-run', '--json', codes=(0, 3))
    if not dry.get('ok'):
        return {'schema': SCHEMA, 'ok': False, 'applied': False, 'selected_ids': [o['id'] for o in chosen],
                'batch': str(batch), 'dry_run': dry, 'error': 'Lot refuse ; source preservee'}
    data = {'schema': SCHEMA, 'ok': True, 'applied': False,
            'selected_ids': [o['id'] for o in chosen], 'batch': str(batch), 'dry_run': dry}
    if commit:
        applied = run(cli, 'project', 'apply', str(project), str(batch), '-o', str(dest), '--json')
        if not applied.get('ok'):
            raise RuntimeError('Echec application alors que dry-run valide')
        data.update({'applied': True, 'output': str(dest),
                     'diff': run(cli, 'project', 'diff', str(project), str(dest), '--json')})
    return data


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description='Retouches collectives OSP transactionnelles')
    parser.add_argument('project', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cli', default=os.environ.get('OPENSTITCH_CLI') or shutil.which('openstitch-cli'))
    parser.add_argument('--type', help='Filtrer par type de point')
    parser.add_argument('--color', help='Filtrer par couleur RGB')
    parser.add_argument('--min-area-mm2', type=float)
    parser.add_argument('--max-area-mm2', type=float)
    parser.add_argument('--ids', help='Liste d IDs separes par des virgules')
    parser.add_argument('--max-objects', type=int, default=20)
    parser.add_argument('--operation', choices=sorted(OPERATIONS), required=True)
    parser.add_argument('--params', required=True, help='Objet JSON des arguments de l operation')
    parser.add_argument('--apply', action='store_true', help='Ecrire une copie OSP seulement apres dry-run')
    args = parser.parse_args(argv)
    if not any([args.type, args.color, args.min_area_mm2 is not None,
                args.max_area_mm2 is not None, args.ids]):
        parser.error('Au moins un filtre explicite est obligatoire')
    if args.max_objects < 1 or args.max_objects > 10000:
        parser.error('--max-objects entre 1 et 10000')
    try:
        params = json.loads(args.params)
        if not isinstance(params, dict) or not params:
            raise ValueError('Un objet JSON non vide est requis')
        ids = set(int(x) for x in args.ids.split(',')) if args.ids else None
        if not args.cli:
            raise RuntimeError('openstitch-cli introuvable (passer --cli)')
        result = edit(args.cli, args.project, args.out, kind=args.type, color=args.color,
                      min_area=args.min_area_mm2, max_area=args.max_area_mm2, ids=ids,
                      max_objects=args.max_objects, operation=args.operation, params=params,
                      commit=args.apply)
    except (OSError, ValueError, RuntimeError) as exc:
        result = {'schema': SCHEMA, 'ok': False, 'error': str(exc)}
    print(json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(',', ':')))
    return 0 if result.get('ok') else 2


if __name__ == '__main__':
    sys.exit(main())
