"""Fusionne qa/results/lot-*.csv dans qa/checklist.csv (QA-STUDIO-001).

Précédence : les lots sont appliqués dans l'ordre ; un statut définitif (PASS, FAIL, ABSENT,
NOT_APPLICABLE) n'est jamais remplacé par BLOCKED ou NOT_RUN d'un lot ultérieur.
"""
import csv, sys, collections
ORDER = ['A', 'B', 'C', 'D', 'X1', 'X2', 'N1a', 'N2a', 'N2b']
FINAL = {'PASS', 'FAIL', 'ABSENT', 'NOT_APPLICABLE'}
rd = lambda p: list(csv.DictReader(open(p, encoding='utf-8-sig')))
reg = rd('qa/checklist.csv')
by = {r['id']: r for r in reg}
redo = set(open('qa/results/retest.txt').read().split()) if len(sys.argv) > 1 and sys.argv[1] == '--redo' else set()
for lot in ORDER:
    try:
        rows = rd(f'qa/results/lot-{lot}.csv')
    except FileNotFoundError:
        continue
    for x in rows:
        r = by.get(x['id'])
        if not r:
            continue
        st = x['statut'].strip().upper()
        if r['statut'] in FINAL and st not in FINAL:
            continue
        r.update(statut=st, severite=x.get('severite', ''), preuve=x.get('preuve', ''),
                 anomalie=x.get('anomalie_repro', ''), sha=f'lot-{lot}')
w = csv.DictWriter(open('qa/checklist.csv', 'w', encoding='utf-8', newline=''), fieldnames=list(reg[0].keys()))
w.writeheader(); w.writerows(reg)
c = collections.Counter(r['statut'] for r in reg)
print(dict(c), sum(c.values()))
