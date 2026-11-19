"""IA-DATA-01 L3 : apprendre l'angle des points par région et le comparer à la règle actuelle.

Entrée : le CSV de `extract.py`. Deux tâches, évaluées en validation croisée groupée
par famille de motifs (aucune famille à la fois en entraînement et en test) :

1. Angle (régions à angle fixe, cohérence >= --fixed) : le modèle prédit l'écart à
   l'axe principal dans l'espace des angles doublés (cos 2d, sin 2d), donc invariant
   par rotation. Références :
   - `regle_openstitch` : axe principal si anisotropie >= 1,3, sinon 45° (repère y
     vers le haut de `libs/autodigitize`, soit 135° dans le repère image) ;
   - `constant_45` : 135° partout (repère image) ;
   - `perpendiculaire` : axe principal + 90°.
2. « Suit la forme » (cohérence < --fixed) contre angle fixe : classifieur, comparé à
   la classe majoritaire.

Les chiffres obtenus sur jiang-s5 restent locaux (licence Research-Only) : ils ne vont
ni dans une PR ni dans la doc publique sans l'accord écrit de l'auteur du jeu.

    python scripts/ia_angle/train.py --regions regions.csv --report report.json
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path

import numpy as np
from sklearn.ensemble import HistGradientBoostingClassifier, HistGradientBoostingRegressor
from sklearn.metrics import balanced_accuracy_score, roc_auc_score
from sklearn.model_selection import GroupKFold

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract import FEATURES  # noqa: E402

SEED = 42
ISOTROPY = 1.3
ISO_ANGLE_IMAGE = 3 * math.pi / 4  # 45° en y vers le haut
ABS_BINS = 12  # classes d'angle absolu de 15°


def axial_error_deg(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    d = np.abs((a - b) % math.pi)
    return np.degrees(np.minimum(d, math.pi - d))


def summarize(err: np.ndarray, weight: np.ndarray) -> dict:
    return {
        "moyenne_deg": round(float(err.mean()), 2),
        "mediane_deg": round(float(np.median(err)), 2),
        "moyenne_ponderee_aire_deg": round(float((err * weight).sum() / weight.sum()), 2),
        "part_sous_15deg": round(float((err < 15).mean()), 4),
        "part_sous_30deg": round(float((err < 30).mean()), 4),
    }


def load(path: Path) -> dict[str, np.ndarray]:
    with open(path, encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    cols = {k: [r[k] for r in rows] for k in rows[0]}
    out = {k: np.array(v, dtype=float) for k, v in cols.items() if k not in ("pair_id", "family")}
    out["family"] = np.array(cols["family"])
    out["pair_id"] = np.array(cols["pair_id"])
    return out


def model_params() -> dict:
    return dict(max_iter=300, learning_rate=0.05, max_leaf_nodes=31, l2_regularization=1.0, random_state=SEED)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--regions", type=Path, required=True)
    ap.add_argument("--report", type=Path, required=True)
    ap.add_argument("--fixed", type=float, default=0.45, help="cohérence minimale d'un angle fixe")
    ap.add_argument("--folds", type=int, default=5)
    args = ap.parse_args()

    d = load(args.regions)
    X = np.stack([d[k] for k in FEATURES], axis=1)
    groups = d["family"]
    fixed = d["coherence"] >= args.fixed
    folds = GroupKFold(n_splits=args.folds)

    # --- Tâche 1 : angle des régions à angle fixe ---
    idx = np.nonzero(fixed)[0]
    Xf, gf = X[idx], groups[idx]
    tp, tt, area = d["theta_principal"][idx], d["theta_true"][idx], d["area_mm2"][idx]
    delta = tt - tp
    yc, ys = np.cos(2 * delta), np.sin(2 * delta)
    pred = np.zeros(len(idx))
    for tr, te in folds.split(Xf, groups=gf):
        rc = HistGradientBoostingRegressor(**model_params()).fit(Xf[tr], yc[tr])
        rs = HistGradientBoostingRegressor(**model_params()).fit(Xf[tr], ys[tr])
        pred[te] = tp[te] + 0.5 * np.arctan2(rs.predict(Xf[te]), rc.predict(Xf[te]))

    # Variante absolue : les numériseurs privilégient des angles standards (0°, 90°,
    # 45°…). Classes de 15° ; features + orientation absolue de la forme ; décision
    # = classe qui minimise l'erreur axiale attendue sous les probabilités prédites.
    Xa = np.column_stack([Xf, np.cos(2 * tp), np.sin(2 * tp)])
    centers = np.arange(ABS_BINS) * math.pi / ABS_BINS
    ya = np.rint(tt / (math.pi / ABS_BINS)).astype(int) % ABS_BINS
    cost = axial_error_deg(centers[:, None], centers[None, :])  # [vraie, choisie]
    pred_abs = np.zeros(len(idx))
    for tr, te in folds.split(Xa, groups=gf):
        clf = HistGradientBoostingClassifier(**model_params()).fit(Xa[tr], ya[tr])
        proba_full = np.zeros((len(te), ABS_BINS))
        proba_full[:, clf.classes_] = clf.predict_proba(Xa[te])
        pred_abs[te] = centers[np.argmin(proba_full @ cost, axis=1)]
    snaps = np.array([0.0, math.pi / 4, math.pi / 2, 3 * math.pi / 4])
    snap = snaps[np.argmin(axial_error_deg(tp[:, None], snaps[None, :]), axis=1)]

    aniso = d["anisotropy"][idx]
    rule = np.where(aniso >= ISOTROPY, tp, ISO_ANGLE_IMAGE)
    angle_report = {
        "regions": int(len(idx)),
        "motifs": int(len(set(d["pair_id"][idx]))),
        "familles": int(len(set(gf))),
        "modele_relatif": summarize(axial_error_deg(pred, tt), area),
        "modele_absolu": summarize(axial_error_deg(pred_abs, tt), area),
        "axe_arrondi_45": summarize(axial_error_deg(snap, tt), area),
        "regle_openstitch": summarize(axial_error_deg(rule, tt), area),
        "constant_45": summarize(axial_error_deg(np.full_like(tt, ISO_ANGLE_IMAGE), tt), area),
        "perpendiculaire": summarize(axial_error_deg(tp + math.pi / 2, tt), area),
    }
    # Détail selon l'anisotropie : là où la règle hésite (formes rondes) ou non.
    by_shape = {}
    for name, sel in (("isotrope_<1.3", aniso < ISOTROPY), ("allongee_>=1.3", aniso >= ISOTROPY)):
        if sel.any():
            by_shape[name] = {
                "regions": int(sel.sum()),
                "modele_absolu": summarize(axial_error_deg(pred_abs[sel], tt[sel]), area[sel]),
                "regle_openstitch": summarize(axial_error_deg(rule[sel], tt[sel]), area[sel]),
            }
    angle_report["par_forme"] = by_shape

    # --- Tâche 2 : la direction suit-elle la forme ? ---
    y = (~fixed).astype(int)
    proba = np.zeros(len(y))
    for tr, te in folds.split(X, groups=groups):
        clf = HistGradientBoostingClassifier(**model_params()).fit(X[tr], y[tr])
        proba[te] = clf.predict_proba(X[te])[:, 1]
    majority = int(y.mean() >= 0.5)
    follow_report = {
        "regions": int(len(y)),
        "part_suit_la_forme": round(float(y.mean()), 4),
        "modele": {
            "precision_equilibree": round(float(balanced_accuracy_score(y, proba >= 0.5)), 4),
            "auc": round(float(roc_auc_score(y, proba)), 4),
        },
        "classe_majoritaire": {
            "precision_equilibree": round(float(balanced_accuracy_score(y, np.full_like(y, majority))), 4),
        },
    }

    # Importance grossière : modèle complet d'angle, gain moyen par permutation sur un pli.
    report = {
        "schema": "openstitch.ia-angle.report/1",
        "seuil_angle_fixe": args.fixed,
        "plis": args.folds,
        "graine": SEED,
        "features": FEATURES,
        "angle": angle_report,
        "suit_la_forme": follow_report,
    }
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"angle": {k: v for k, v in angle_report.items() if k != "par_forme"}, "suit_la_forme": follow_report}, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
