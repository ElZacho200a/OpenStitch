#!/usr/bin/env python3
"""Compare trois strategies OpenStitch pour UN objet, sans modifier l'OSP source."""
from __future__ import annotations
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

SCHEMA = 'openstitch.satin-compare/1'
STRATEGIES = ('skeleton', 'parallel_sweep', 'medial_axis')


def call(cli: str, *args: str, accepted: tuple[int, ...] = (0,), timeout: int = 240) -> dict:
    try:
        p = subprocess.run([cli, *args], capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f'CLI indisponible : {exc}') from exc
    try:
        result = json.loads(p.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f'JSON CLI invalide ({" ".join(args)}) : {p.stderr[:250]}') from exc
    if p.returncode not in accepted:
        message = result.get('error') or p.stderr.strip()[:300]
        raise RuntimeError(f'code {p.returncode} : {message}')
    return result


def pareto(trials: list[dict]) -> list[str]:
    """Front non domine : couverture↑, debordement↓, sauts↓ et points↓."""
    eligible = [x for x in trials if x.get('status') == 'ok' and
                x.get('coverage') is not None and x.get('overflow') is not None and
                x.get('stitches', 0) > 0]
    def values(x):
        return (-x['coverage'], x['overflow'], x['jumps'], x['stitches'])
    result = []
    for trial in eligible:
        a = values(trial)
        if not any(other is not trial and all(b <= v for b, v in zip(values(other), a))
                   and any(b < v for b, v in zip(values(other), a)) for other in eligible):
            result.append(trial['strategy'])
    return sorted(result)


def compare(cli: str, project: Path, object_id: int, output: Path,
            spacing: float, render: bool) -> dict:
    base = call(cli, 'project', 'inspect', str(project), '--object', str(object_id), '--json')
    if base.get('schema') != 'openstitch.object-inspect/1' or not base.get('object'):
        raise RuntimeError('Objet source introuvable ou schema inspect invalide')
    source = base['object']
    if source.get('type') not in {'tatami', 'auto_satin', 'running', 'directional'}:
        raise RuntimeError(f"Type non pris en charge pour l'essai auto_satin : {source.get('type')}")
    output.mkdir(parents=True, exist_ok=True)
    trials = []
    for strategy in STRATEGIES:
        osp = output / f'{strategy}.osp'
        batch = output / f'{strategy}.batch.json'
        if osp.exists() or batch.exists():
            raise RuntimeError(f'Fichier deja present (aucun ecrasement) : {osp} ou {batch}')
        if source['type'] == 'auto_satin':
            op = {'op': 'set_params', 'object': object_id,
                  'params': {'strategy': strategy, 'spacing_mm': spacing}}
        else:
            op = {'op': 'auto_satin', 'object': object_id, 'strategy': strategy,
                  'spacing_mm': spacing}
        batch.write_text(json.dumps({'schema': 'openstitch.batch/1', 'operations': [op]},
                                    sort_keys=True, indent=2) + '\n', encoding='utf-8')
        entry = {'strategy': strategy, 'status': 'rejected', 'project': None}
        try:
            dry = call(cli, 'project', 'apply', str(project), str(batch), '--dry-run', '--json',
                       accepted=(0, 3))
            if not dry.get('ok'):
                entry['reason'] = dry.get('error') or dry.get('outcomes', [])
                trials.append(entry)
                continue
            applied = call(cli, 'project', 'apply', str(project), str(batch), '-o', str(osp), '--json')
            if not applied.get('ok'):
                entry['reason'] = applied.get('error')
                trials.append(entry)
                continue
            detail = call(cli, 'project', 'inspect', str(osp), '--object', str(object_id), '--json')
            object_data = detail.get('object', {})
            stats = object_data.get('stitches') or {}
            quality = call(cli, 'project', 'check', str(osp), '--only', str(object_id), '--json', accepted=(0, 2))
            measured = next((x for x in quality.get('objects', []) if x.get('id') == object_id), {})
            entry.update({'status': 'ok', 'project': str(osp), 'stitches': stats.get('total', 0),
                          'jumps': stats.get('jumps', 0), 'coverage': measured.get('covered_ratio'),
                          'overflow': measured.get('overflow_ratio'),
                          'quality': quality.get('summary', {}),
                          'findings': [x for x in quality.get('findings', []) if x.get('object') == object_id]})
            if entry['stitches'] <= 0:
                entry['status'] = 'rejected'
                entry['reason'] = 'aucun point produit'
            elif render:
                png = output / f'{strategy}.png'
                call(cli, 'project', 'render', str(osp), '-o', str(png), '--px-per-mm', '4', '--json')
                entry['preview'] = str(png)
        except RuntimeError as exc:
            entry['status'] = 'error'
            entry['reason'] = str(exc)
        trials.append(entry)
    answer = {'schema': SCHEMA, 'source_project': str(project), 'object': object_id,
              'source_type': source.get('type'), 'spacing_mm': spacing,
              'trials': trials, 'pareto_strategies': pareto(trials),
              'warning': 'Front de Pareto technique, pas selection artistique. Comparer les rendus et verifier sur tissu.'}
    (output / 'comparison.json').write_text(json.dumps(answer, sort_keys=True, ensure_ascii=False, indent=2) + '\n',
                                            encoding='utf-8')
    return answer


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description='Comparer 3 auto-satins avec le moteur OpenStitch')
    parser.add_argument('project', type=Path)
    parser.add_argument('--object', type=int, required=True)
    parser.add_argument('--out-dir', type=Path, required=True)
    parser.add_argument('--cli', default=os.environ.get('OPENSTITCH_CLI') or shutil.which('openstitch-cli'))
    parser.add_argument('--spacing-mm', type=float, default=0.4)
    parser.add_argument('--no-render', action='store_true')
    args = parser.parse_args(argv)
    if not 0.1 <= args.spacing_mm <= 1.5:
        parser.error('--spacing-mm dans [0.1; 1.5]')
    try:
        if not args.cli or not Path(args.cli).is_file():
            raise RuntimeError('Client CLI absent : --cli ou OPENSTITCH_CLI requis')
        data = compare(args.cli, args.project, args.object, args.out_dir,
                       args.spacing_mm, not args.no_render)
    except (RuntimeError, OSError) as exc:
        print(json.dumps({'schema': SCHEMA, 'ok': False, 'error': str(exc)}, ensure_ascii=False))
        return 2
    print(json.dumps(data, ensure_ascii=False, separators=(',', ':'), sort_keys=True))
    return 0 if any(x.get('status') == 'ok' for x in data['trials']) else 2


if __name__ == '__main__':
    sys.exit(main())
