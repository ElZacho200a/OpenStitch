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
import zlib
from collections import Counter
from pathlib import Path, PureWindowsPath

SCHEMA = 'openstitch.semantic/1'
RESULT_SCHEMA = 'openstitch.semantic-validation/1'
IDENT = re.compile(r'^[a-zA-Z0-9][a-zA-Z0-9_.:-]*$')
COLOR = re.compile(r'^#[0-9a-fA-F]{6}$')
PRIORITIES = {'protected', 'important', 'secondary', 'optional'}
INTENTS = {None, 'unstitched', 'tatami', 'auto_satin', 'running', 'directional', 'satin'}
DECISIONS = {'keep', 'merge', 'remove', 'review'}
LEVELS = {'family', 'instance', 'component', 'technical'}


def png_size(path: Path) -> tuple[int, int]:
    """Lire les dimensions d'un PNG sans decoder les pixels.

    Verifie l'IHDR et la structure/CRC des chunks, sans decoder IDAT.
    """
    with path.open('rb') as f:
        header = f.read(33)  # signature (8) + length (4) + IHDR (17) + CRC (4)
    if (len(header) != 33 or header[:8] != b'\x89PNG\r\n\x1a\n'
            or header[12:16] != b'IHDR'):
        raise ValueError('masque non PNG ou IHDR tronque/absent')
    if struct.unpack('>I', header[8:12])[0] != 13:
        raise ValueError('longueur IHDR invalide')
    actual_crc = zlib.crc32(header[12:29]) & 0xffffffff
    if actual_crc != struct.unpack('>I', header[29:33])[0]:
        raise ValueError('CRC IHDR invalide')
    width, height = struct.unpack('>II', header[16:24])
    if width == 0 or height == 0 or width > 100000 or height > 100000:
        raise ValueError('dimensions PNG invalides')
    bit_depth, color_type, compression, filtering, interlace = header[24:29]
    valid_depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8),
                    4: (8, 16), 6: (8, 16)}
    if (bit_depth not in valid_depths.get(color_type, ()) or compression != 0
            or filtering != 0 or interlace not in (0, 1)):
        raise ValueError('parametres IHDR invalides')
    # Inspecter les chunks sans charger ni decomprimer les pixels en memoire.
    # Un IHDR correct seul n'est pas une preuve que le masque est un PNG entier.
    with path.open('rb') as f:
        f.seek(33)  # IHDR deja valide, y compris son CRC.
        seen_idat = False
        seen_plte = False
        after_idat = False
        while True:
            chunk_header = f.read(8)
            if len(chunk_header) != 8:
                raise ValueError('chunk PNG tronque ou IEND absent')
            length, kind = struct.unpack('>I4s', chunk_header)
            if not all(65 <= b <= 90 or 97 <= b <= 122 for b in kind):
                raise ValueError('type de chunk PNG invalide')
            if kind == b'IHDR':
                raise ValueError('IHDR duplique')
            # Unknown critical chunks cannot be ignored: their semantics may
            # change how the image is interpreted. Ancillary chunks are safe
            # to skip after validating their CRC.
            if kind[0] & 0x20 == 0 and kind not in (b'PLTE', b'IDAT', b'IEND'):
                raise ValueError('chunk PNG critique inconnu')
            if kind[2] & 0x20:
                raise ValueError('bit reserve du type de chunk PNG invalide')
            if kind == b'PLTE':
                # PNG requires the palette before IDAT and at most once.
                # Grayscale images forbid PLTE; indexed images require it.
                if (color_type in (0, 4) or seen_plte or seen_idat
                        or length == 0 or length > 768 or length % 3
                        or (color_type == 3 and length // 3 > (1 << bit_depth))):
                    raise ValueError('palette PLTE invalide')
                seen_plte = True
            if kind == b'IDAT':
                if color_type == 3 and not seen_plte:
                    raise ValueError('palette PLTE requise avant IDAT')
                if after_idat:
                    raise ValueError('chunks IDAT non contigus')
                seen_idat = True
            elif seen_idat:
                after_idat = True
            if kind == b'IEND' and (length != 0 or not seen_idat):
                raise ValueError('IEND invalide ou IDAT absent')
            crc = zlib.crc32(kind)
            remaining = length
            while remaining:
                block = f.read(min(65536, remaining))
                if not block:
                    raise ValueError('donnees de chunk PNG tronquees')
                crc = zlib.crc32(block, crc)
                remaining -= len(block)
            stored_crc = f.read(4)
            if len(stored_crc) != 4 or crc & 0xffffffff != struct.unpack('>I', stored_crc)[0]:
                raise ValueError('CRC de chunk PNG invalide ou tronque')
            if kind == b'IEND':
                if f.read(1):
                    raise ValueError('octets apres IEND')
                break
    return width, height

def cycles(edges: dict[str, set[str]]) -> list[list[str]]:
    """Retourne une preuve de cycle deterministe sans recursion Python.

    Un manifeste peut contenir plus de regions que la limite de recursion.
    Chaque frame conserve l'iterateur des voisins tries pour reproduire
    l'ordre de parcours DFS et le diagnostic de la version recursive.
    """
    state: dict[str, int] = {}
    path: list[str] = []

    for root in sorted(edges):
        if state.get(root):
            continue
        state[root] = 1
        path.append(root)
        frames = [iter(sorted(edges.get(root, set())))]
        while frames:
            try:
                child = next(frames[-1])
            except StopIteration:
                state[path.pop()] = 2
                frames.pop()
                continue
            if state.get(child) == 1:
                return [path[path.index(child):] + [child]]
            if state.get(child) == 2:
                continue
            state[child] = 1
            path.append(child)
            frames.append(iter(sorted(edges.get(child, set()))))
    return []


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
        if not isinstance(item.get('level', 'technical'), str) or item.get('level', 'technical') not in LEVELS:
            fail('E_LEVEL', f'{where}.level', 'niveau famille/instance/composant/technique inconnu')
        if not isinstance(item.get('priority'), str) or item.get('priority') not in PRIORITIES:
            fail('E_PRIORITY', f'{where}.priority', 'priorite inconnue')
        if item.get('stitch_intent') is not None and (not isinstance(item.get('stitch_intent'), str) or item.get('stitch_intent') not in INTENTS):
            fail('E_STITCH', f'{where}.stitch_intent', 'type de point inconnu')
        if not isinstance(item.get('decision', 'keep'), str) or item.get('decision', 'keep') not in DECISIONS:
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
            else:
                # --skip-files skips file reads, never path containment checks.
                # Resolve symlinks before checking containment to avoid escapes.
                try:
                    # Paths must be relative even when an absolute path is inside base.
                    # Portable manifests use forward slashes only. Reject Windows
                    # drives, UNC paths and backslash traversal on every host OS.
                    if (Path(mask).is_absolute() or PureWindowsPath(mask).drive
                            or '\\' in mask):
                        raise ValueError('non-portable or absolute mask path')
                    target = (base / mask).resolve()
                    contained = target.is_relative_to(base.resolve())
                except (OSError, ValueError):
                    contained = False
                    target = None
                if not contained:
                    fail('E_PATH', f'{where}.mask_path', 'masque en dehors du dossier du manifeste')
                elif not skip_files:
                    if not target.is_file():
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
            if not isinstance(parent, str) or parent not in indexed:
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
