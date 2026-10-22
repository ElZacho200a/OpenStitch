#!/usr/bin/env python3
"""Validateur du manifeste semantique preparatoire OpenStitch (stdlib seule).

Ne genere pas de points et ne modifie jamais les fichiers OSP. Format stable
openstitch.semantic/1 pour de futurs imports de masques et objets techniques.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

SCHEMA = 'openstitch.semantic/1'
RESULT_SCHEMA = 'openstitch.semantic-validation/1'
IDENT = re.compile(r'^[a-zA-Z0-9][a-zA-Z0-9_.:-]*$')
COLOR = re.compile(r'^#[0-9a-fA-F]{6}$')
PRIORITIES = {'protected', 'important', 'secondary', 'optional'}
INTENTS = {None, 'unstitched', 'tatami', 'auto_satin', 'running', 'directional', 'satin'}
DECISIONS = {'keep', 'merge', 'remove', 'review'}
LEVELS = {'family', 'instance', 'component', 'technical'}


def png_size(path: Path) -> tuple[int, int]:
    with path.open('rb') as f:
        header = f.read(24)
    if (len(header) != 24 or header[:8] != b'\x89PNG\r\n\x1a\n'
            or header[12:16] != b'IHDR'):
        raise ValueError('masque non PNG ou IHDR absent')
    width, height = struct.unpack('>II', header[16:24])
    if width == 0 or height == 0 or width > 100000 or height > 100000:
        raise ValueError('dimensions PNG invalides')
    return width, height


def cycles(edges: dict[str, set[str]]) -> list[list[str]]:
    """Retourne une preuve de cycle, triee et deterministe, si presente."""
    state: dict[str, int] = {}
    stack: list[str] = []
    found: list[list[str]] = []

    def visit(node: str) -> None:
        if found:
            return
        if state.get(node) == 1:
            found.append(stack[stack.index(node):] + [node])
            return
        if state.get(node) == 2:
            return
        state[node] = 1
        stack.append(node)
        for child in sorted(edges.get(node, set())):
            visit(child)
        stack.pop()
        state[node] = 2

    for node in sorted(edges):
        visit(node)
        if found:
            break
    return found


def validate(manifest: dict, base: Path, *, skip_files: bool = False) -> dict:
    problems: list[dict] = []
    def fail(code: str, where: str, message: str) -> None:
        problems.append({'code': code, 'path': where, 'message': message})

    if not isinstance(manifest, dict):
        fail('E_SCHEMA', '$', 'racine JSON doit etre un objet')
        return {'schema': RESULT_SCHEMA, 'ok': False, 'errors': problems}
    if manifest.get('schema') != SCHEMA:
        fail('E_SCHEMA', 'schema', f'attendu : {SCHEMA}')
    source = manifest.get('source_image')
    expected: tuple[int, int] | None = None
    if not isinstance(source, dict):
        fail('E_SOURCE', 'source_image', 'objet source_image requis')
    else:
        w, h = source.get('width_px'), source.get('height_px')
        if type(w) is not int or type(h) is not int or w <= 0 or h <= 0:
            fail('E_SOURCE', 'source_image', 'dimensions width_px/height_px > 0 requises')
        else:
            expected = (w, h)
    design = manifest.get('design')
    if not isinstance(design, dict):
        fail('E_SIZE', 'design', 'objet design requis')
    else:
        for key in ('width_mm', 'height_mm'):
            value = design.get(key)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 < value <= 1000:
                fail('E_SIZE', f'design.{key}', 'dimension > 0 et <= 1000 mm requise')
    regions = manifest.get('regions')
    if not isinstance(regions, list):
        fail('E_REGIONS', 'regions', 'liste de regions requise')
        regions = []
    indexed: dict[str, dict] = {}
    for n, item in enumerate(regions):
        where = f'regions[{n}]'
        if not isinstance(item, dict):
            fail('E_REGION', where, 'region doit etre un objet')
            continue
        sid = item.get('semantic_id')
        if not isinstance(sid, str) or not IDENT.fullmatch(sid):
            fail('E_ID', f'{where}.semantic_id', 'identifiant technique ASCII stable requis')
            continue
        if sid in indexed:
            fail('E_DUPLICATE', f'{where}.semantic_id', f'{sid} apparait plusieurs fois')
        else:
            indexed[sid] = item
        if not isinstance(item.get('family'), str) or not item['family']:
            fail('E_FAMILY', f'{where}.family', 'famille non vide requise')
        if not isinstance(item.get('role'), str) or not item['role']:
            fail('E_ROLE', f'{where}.role', 'role non vide requis')
        if item.get('level', 'technical') not in LEVELS:
            fail('E_LEVEL', f'{where}.level', 'niveau famille/instance/composant/technique inconnu')
        if item.get('priority') not in PRIORITIES:
            fail('E_PRIORITY', f'{where}.priority', 'priorite inconnue')
        if item.get('stitch_intent') not in INTENTS:
            fail('E_STITCH', f'{where}.stitch_intent', 'type de point inconnu')
        if item.get('decision', 'keep') not in DECISIONS:
            fail('E_DECISION', f'{where}.decision', 'decision inconnue')
        color = item.get('thread_rgb')
        if color is not None and (not isinstance(color, str) or not COLOR.fullmatch(color)):
            fail('E_THREAD', f'{where}.thread_rgb', 'couleur attendue : #RRGGBB')
        if not isinstance(item.get('protected_detail', False), bool):
            fail('E_PROTECTED', f'{where}.protected_detail', 'booleen requis')
        if not isinstance(item.get('visible_on_fabric', False), bool):
            fail('E_VISIBLE', f'{where}.visible_on_fabric', 'booleen requis')
        if item.get('stitch_intent') not in (None, 'unstitched') and not item.get('mask_path'):
            fail('E_MASK', f'{where}.mask_path', 'masque requis pour un objet cousu')
        mask = item.get('mask_path')
        if mask is not None:
            if not isinstance(mask, str) or not mask:
                fail('E_MASK', f'{where}.mask_path', 'chemin relatif non vide requis')
            elif not skip_files:
                target = (base / mask).resolve()
                if not target.is_relative_to(base.resolve()):
                    fail('E_PATH', f'{where}.mask_path', 'masque en dehors du dossier du manifeste')
                elif not target.is_file():
                    fail('E_MISSING_MASK', f'{where}.mask_path', 'fichier introuvable')
                else:
                    try:
                        dims = png_size(target)
                        if expected and dims != expected:
                            fail('E_MASK_SIZE', f'{where}.mask_path', f'{dims} != dimensions source {expected}')
                    except (OSError, ValueError) as exc:
                        fail('E_MASK_PNG', f'{where}.mask_path', str(exc))
    hierarchy: dict[str, set[str]] = {sid: set() for sid in indexed}
    occlusions: dict[str, set[str]] = {sid: set() for sid in indexed}
    for sid, item in indexed.items():
        parent = item.get('parent_id')
        if parent is not None:
            if parent not in indexed:
                fail('E_PARENT', sid, f'parent inconnu : {parent}')
            else:
                hierarchy[sid].add(parent)
        for relation in ('under', 'over'):
            links = item.get(relation, [])
            if not isinstance(links, list) or any(not isinstance(x, str) for x in links):
                fail('E_RELATION', f'{sid}.{relation}', 'liste d identifiants requise')
                continue
            for target in links:
                if target not in indexed:
                    fail('E_REFERENCE', f'{sid}.{relation}', f'objet inconnu : {target}')
                elif relation == 'under':
                    occlusions[sid].add(target)
                else:
                    occlusions[target].add(sid)
    for graph, code in ((hierarchy, 'E_PARENT_CYCLE'), (occlusions, 'E_OCCLUSION_CYCLE')):
        for trace in cycles(graph):
            fail(code, 'regions', ' -> '.join(trace))
    counts = Counter(item.get('family') for item in indexed.values() if isinstance(item.get('family'), str))
    return {'schema': RESULT_SCHEMA, 'ok': not problems, 'regions': len(regions),
            'unique_regions': len(indexed), 'families': dict(sorted(counts.items())),
            'errors': problems}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description='Valider une segmentation semantique pour OpenStitch')
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--skip-files', action='store_true', help='Valider le schema sans lire les masques')
    args = parser.parse_args(argv)
    try:
        with args.manifest.open('r', encoding='utf-8') as stream:
            manifest = json.load(stream)
        result = validate(manifest, args.manifest.parent, skip_files=args.skip_files)
    except (OSError, json.JSONDecodeError) as exc:
        result = {'schema': RESULT_SCHEMA, 'ok': False,
                  'errors': [{'code': 'E_IO', 'path': str(args.manifest), 'message': str(exc)}]}
    print(json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(',', ':')))
    return 0 if result['ok'] else 2


if __name__ == '__main__':
    sys.exit(main())
