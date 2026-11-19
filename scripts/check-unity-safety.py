#!/usr/bin/env python3
"""BUILD-OPT-01.6 : heuristique de securite Unity Build pour une lib (docs/source/build-pch-unity.md).

Usage : python scripts/check-unity-safety.py libs/geometry [libs/...]
Signale, entre fichiers .cpp d'une meme lib : symboles `static` / d'espace de noms anonyme homonymes
(collision ODR en Unity), #define / #undef locaux, `using namespace` au niveau fichier.
Code de sortie 1 si au moins une collision. Heuristique : un faux negatif reste possible, la
preuve finale est la compilation + les tests identiques octet pour octet.
"""
import re, sys, pathlib, collections

DECL = re.compile(r'^(?:static\s+|inline\s+|constexpr\s+|const\s+)*[\w:<>,\*&\s]+?[\s\*&](\w+)\s*(?:\(|=|\{|;)')
def scan(cpp):
    names, defines, usings = set(), [], []
    anon = 0
    depth_anon = None
    depth = 0
    for line in cpp.read_text(encoding='utf-8', errors='replace').splitlines():
        s = line.strip()
        if re.match(r'#\s*(define|undef)\s+(\w+)', s):
            defines.append(s)
        if re.match(r'using\s+namespace\s', s) and not line.startswith((' ', '\t')):
            usings.append(s)
        if re.match(r'namespace\s*\{', s):
            depth_anon = depth
        # Seuls comptent les membres DIRECTS de l'espace anonyme (pas les locales) et les `static` de premier niveau.
        direct = depth_anon is not None and depth == depth_anon + 1
        static_top = s.startswith('static ') and not line.startswith((' ', '	'))
        if direct or static_top:
            m = DECL.match(s)
            if m and not s.startswith(('//', '#', 'return', 'namespace', 'struct', 'class', 'enum', 'using', '}', 'if', 'for', 'while')):
                names.add(m.group(1))
        depth += line.count('{') - line.count('}')
        if depth_anon is not None and depth <= depth_anon:
            depth_anon = None
    return names, defines, usings

bad = 0
for lib in sys.argv[1:]:
    seen = collections.defaultdict(list)
    for cpp in sorted(pathlib.Path(lib).glob('src/**/*.cpp')):
        names, defines, usings = scan(cpp)
        for n in names: seen[n].append(cpp.name)
        for d in defines: print(f'[define] {cpp}: {d}')
        for u in usings: print(f'[using ] {cpp}: {u}')
    for n, files in sorted(seen.items()):
        if len(files) > 1:
            bad += 1
            print(f'[ODR   ] {lib}: {n} dans {", ".join(files)}')
print(f'{bad} collision(s) potentielle(s)')
sys.exit(1 if bad else 0)
