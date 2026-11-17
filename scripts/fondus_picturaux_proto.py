"""Prototype : zones de fondu pour l'autodigitize (tableaux Turner / Monet).

Questions :
 Q1  peut-on découper un tableau en peu de ZONES (dizaines) séparées par des bords nets ?
 Q2  dans une zone, un mélange de k fils (k <= 4) reconstruit-il la couleur mieux qu'un aplat ?
 Q3  à quoi ressemble la couture simulée (rangées partagées, seuils de Weyl) vue de loin ?

Déterministe : ligne de partage des eaux sans aléa, k-means initialisé par quantiles, aucun RNG.
Usage : python scripts/fondus_picturaux_proto.py <image> <sortie_dossier> [largeur_mm] [seuil_bord]
Variables : PALETTE (fils au total, 12), KMAX (fils par zone, 4), LAM (localité, 0.05).
Étude et mesures : docs/fondus-picturaux-autodigitize.md
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage as ndi
from scipy.optimize import nnls
from skimage import color, filters, graph, segmentation

MM_PER_PX = 0.5  # image de travail : 1 px = 0,5 mm
ROW_MM = 0.4  # écart nominal des rangées
SEG_MM = 3.0  # longueur d'un segment de points (résolution le long de la rangée)
GOLD = 0.6180339887498949


def load(path, width_mm):
    im = Image.open(path).convert("RGB")
    w = int(round(width_mm / MM_PER_PX))
    h = int(round(im.height * w / im.width))
    im = im.resize((w, h), Image.LANCZOS)
    return np.asarray(im).astype(np.float64) / 255.0


def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def kmeans_det(X, k, iters=30):
    """k-means déterministe : centres initiaux aux quantiles de la 1re composante."""
    mu = X.mean(0)
    _, _, vt = np.linalg.svd(X - mu, full_matrices=False)
    proj = (X - mu) @ vt[0]
    order = np.argsort(proj, kind="stable")
    C = np.array([X[order[int((i + 0.5) * len(X) / k)]] for i in range(k)])
    for _ in range(iters):
        d = ((X[:, None, :] - C[None]) ** 2).sum(-1)
        lab = d.argmin(1)
        newC = np.array([X[lab == i].mean(0) if (lab == i).any() else C[i] for i in range(k)])
        if np.allclose(newC, C):
            break
        C = newC
    return C, lab


def zones(lab_s, n_segments):
    # Bord net = gradient Lab fort (ΔE par px) ; un fondu a un gradient faible mais continu.
    grad = np.zeros(lab_s.shape[:2])
    for ch, wgt in ((0, 1.0), (1, 1.0), (2, 1.0)):
        grad += (filters.sobel(lab_s[..., ch]) * wgt) ** 2
    grad = np.sqrt(grad)
    sp = segmentation.watershed(grad, markers=n_segments, compactness=0.001) - 1
    rag = graph.rag_boundary(sp, grad)
    # Fusion hiérarchique sur la FORCE DU BORD (pas la différence de moyenne) :
    # un dégradé lent se fond en une zone, un bord net l'arrête.
    def weight_boundary(g, src, dst, n):
        default = {"weight": 0.0, "count": 0}
        c1 = g[src].get(n, default)["count"]
        c2 = g[dst].get(n, default)["count"]
        w1 = g[src].get(n, default)["weight"]
        w2 = g[dst].get(n, default)["weight"]
        c = c1 + c2
        return {"count": c, "weight": (c1 * w1 + c2 * w2) / max(c, 1)}

    def merge_boundary(g, src, dst):
        pass

    return sp, grad, rag, weight_boundary, merge_boundary


def segment_zones(lab_s, n_segments, thresh):
    sp, grad, rag, wb, mb = zones(lab_s, n_segments)
    z = graph.merge_hierarchical(sp, rag, thresh=thresh, rag_copy=False, in_place_merge=True,
                                 merge_func=mb, weight_func=wb)
    z = segmentation.relabel_sequential(z)[0]
    # Petites zones (< 25 mm²) : absorbées par la voisine au bord le plus faible -> simple : par
    # la zone majoritaire du voisinage.
    min_px = int(25 / (MM_PER_PX ** 2))
    for _ in range(3):
        ids, counts = np.unique(z, return_counts=True)
        small = ids[counts < min_px]
        if len(small) == 0:
            break
        for s in small:
            m = z == s
            ring = ndi.binary_dilation(m, iterations=1) & ~m
            if ring.any():
                vals, cnt = np.unique(z[ring], return_counts=True)
                z[m] = vals[cnt.argmax()]
        z = segmentation.relabel_sequential(z)[0]
    return z, grad


def fit_zone(lin_px, k):
    """Palette de k fils (k-means en RVB linéaire) et poids NNLS par pixel (somme 1)."""
    C, _ = kmeans_det(lin_px, k)
    # NNLS avec contrainte de somme 1 par pénalité : ligne de 1 pondérée.
    A = np.vstack([C.T, 10.0 * np.ones((1, k))])
    W = np.zeros((len(lin_px), k))
    # Pixels uniques arrondis pour accélérer.
    q = np.round(lin_px * 64).astype(np.int32)
    uq, inv = np.unique(q, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    Wu = np.zeros((len(uq), k))
    for i, v in enumerate(uq / 64.0):
        b = np.concatenate([v, [10.0]])
        Wu[i], _ = nnls(A, b)
    W = Wu[inv]
    W /= np.maximum(W.sum(1, keepdims=True), 1e-9)
    return C, W


def de76(a, b):
    return np.sqrt(((a - b) ** 2).sum(-1))


def simulate(Wmap, palette_lin, shape, zone_of, zone_palettes):
    """Couture simulée : rangées horizontales tous les ROW_MM, segments de SEG_MM.
    Chaque segment appartient au fil dont la tranche [lower ; lower+w) contient le seuil
    de Weyl de sa rangée, évaluée au milieu du segment (comme `SpacingField::owns`)."""
    H, W = shape
    sub = 4  # px de simulation par px de travail (0,125 mm)
    out = np.zeros((H * sub, W * sub, 3))
    row_px = ROW_MM / (MM_PER_PX / sub)
    seg_px = SEG_MM / (MM_PER_PX / sub)
    nrows = int(np.ceil(H * sub / row_px))
    for r in range(nrows):
        u = (r * GOLD) % 1.0
        y0 = int(round(r * row_px))
        y1 = int(round((r + 1) * row_px))
        # décalage de brique entre rangées (comme un tatami)
        off = (r % 3) * seg_px / 3.0
        x = -off
        while x < W * sub:
            xa = max(0, int(round(x)))
            xb = min(W * sub, int(round(x + seg_px)))
            if xb > xa:
                xm = min(W - 1, ((xa + xb) // 2) // sub)
                ym = min(H - 1, ((y0 + y1) // 2) // sub)
                zid = zone_of[ym, xm]
                w = Wmap[ym, xm]
                cum = np.cumsum(w)
                idx = int(np.searchsorted(cum, u, side="right"))
                idx = min(idx, len(w) - 1)
                out[y0:y1, xa:xb] = zone_palettes[zid][idx]
            x += seg_px
    return out


def main():
    src, outdir = Path(sys.argv[1]), Path(sys.argv[2])
    width_mm = float(sys.argv[3]) if len(sys.argv) > 3 else 150.0
    outdir.mkdir(parents=True, exist_ok=True)
    name = src.stem
    rgb = load(src, width_mm)
    lin = srgb_to_lin(rgb)
    lab = color.rgb2lab(rgb)
    # Lissage : on veut les couleurs « vues de loin » (le fil ne rendra pas le grain de la toile).
    rgb_s = np.stack([ndi.gaussian_filter(rgb[..., c], 1.5) for c in range(3)], -1)
    lab_s = color.rgb2lab(rgb_s)
    lin_s = srgb_to_lin(rgb_s)
    H, W = rgb.shape[:2]

    n_seg = max(200, (H * W) // 400)
    z, grad = segment_zones(lab_s, n_seg, thresh=float(sys.argv[4]) if len(sys.argv) > 4 else 6.0)
    nz = z.max() + 1

    # --- Q2 : pour chaque zone, aplat (1 fil) vs mélange de k fils.
    report = {"image": name, "size_mm": [W * MM_PER_PX, H * MM_PER_PX], "zones": int(nz)}
    import os
    N = int(os.environ.get("PALETTE", "12"))
    K = int(os.environ.get("KMAX", "4"))
    allpx = lin_s.reshape(-1, 3)
    LAM = float(os.environ.get("LAM", "0.05"))
    samp_all = allpx[:: max(1, len(allpx) // 30000)]
    cand, _ = kmeans_det(samp_all, 48)
    # extrêmes : 1er et 99e centiles le long des 3 axes principaux (le mélange ne sort pas de l'enveloppe)
    mu = samp_all.mean(0); _, _, vt = np.linalg.svd(samp_all - mu, full_matrices=False)
    for ax in vt:
        pr = (samp_all - mu) @ ax
        for qq in (1, 99):
            cand = np.vstack([cand, samp_all[np.argmin(np.abs(pr - np.percentile(pr, qq)))]])
    evalpx = np.unique(np.round(samp_all * 32).astype(int), axis=0) / 32.0
    evalpx = evalpx[:: max(1, len(evalpx) // 600)]
    def mix_resid(Pal, X, kmax=4):
        tot = 0.0
        for v in X:
            d = ((Pal - v) ** 2).sum(1)
            near = np.argsort(d, kind="stable")[:kmax]
            C = Pal[near]
            A = np.vstack([C.T, 10.0 * np.ones((1, len(near))), np.sqrt(LAM) * np.diag(np.sqrt(d[near]))])
            b = np.concatenate([v, [10.0], np.zeros(len(near))])
            tot += nnls(A, b)[1]
        return tot
    selP = [int(((cand - mu) ** 2).sum(1).argmin())]
    while len(selP) < N:
        best, bj = None, None
        for j in range(len(cand)):
            if j in selP: continue
            r = mix_resid(cand[selP + [j]], evalpx)
            if best is None or r < best: best, bj = r, j
        selP.append(bj)
    P = cand[selP]
    Wmap = np.zeros((H, W, K))
    zone_pal = {}
    flat = np.zeros_like(lin_s)
    mix = np.zeros_like(lin_s)
    classes = {"aplat": 0, "fondu": 0}
    area = {"aplat": 0.0, "fondu": 0.0}
    threads_total = 0
    used = set()
    for zi in range(nz):
        m = z == zi
        if not m.any():
            zone_pal[zi] = P[:1]
            continue
        px = lin_s[m]
        # aplat = fil de la palette le plus proche de la moyenne
        mean = px.mean(0)
        flat[m] = P[((P - mean) ** 2).sum(1).argmin()]
        q = np.round(px * 48).astype(np.int32)
        uq, inv = np.unique(q, axis=0, return_inverse=True)
        uq = uq / 48.0
        inv = inv.reshape(-1)
        samp = uq[:: max(1, len(uq) // 400)]
        def solve(sel, v):
            C = P[sel]; d = ((C - v) ** 2).sum(1)
            A = np.vstack([C.T, 10.0 * np.ones((1, len(sel))), np.sqrt(LAM) * np.diag(np.sqrt(d))])
            return nnls(A, np.concatenate([v, [10.0], np.zeros(len(sel))]))
        def resid(sel, X):
            return sum(solve(sel, v)[1] for v in X)
        sel = [int(((P - mean) ** 2).sum(1).argmin())]
        base = resid(sel, samp)
        while len(sel) < K:
            best, bi = base, None
            for j in range(N):
                if j in sel:
                    continue
                r = resid(sel + [j], samp)
                if r < best:
                    best, bi = r, j
            # un fil de plus seulement s'il réduit nettement l'erreur
            if bi is None or best > 0.8 * base:
                break
            sel.append(bi); base = best
        k = len(sel)
        C = P[sel]
        Wu = np.array([solve(sel, v)[0] for v in uq])
        Wu /= np.maximum(Wu.sum(1, keepdims=True), 1e-9)
        Wz = Wu[inv]
        key = "aplat" if k == 1 else "fondu"
        classes[key] += 1
        area[key] += m.sum()
        used.update(sel)
        threads_total += k
        zone_pal[zi] = C
        Wfull = np.zeros((len(px), K))
        Wfull[:, :k] = Wz
        Wmap[m] = Wfull
        mix[m] = Wz @ C
    report["fils_utilises"] = len(used)
    nnz = (Wmap > 0.05).sum(-1)
    report["px_melange_1_2_3_4"] = [round(float((nnz == i).mean()), 3) for i in (1, 2, 3, 4)]

    lab_flat = color.rgb2lab(lin_to_srgb(flat))
    lab_mix = color.rgb2lab(lin_to_srgb(mix))
    e_flat = de76(lab_flat, lab_s)
    e_mix = de76(lab_mix, lab_s)
    report.update(
        zones_aplat=classes["aplat"], zones_fondu=classes["fondu"],
        area_fondu=round(area["fondu"] / (H * W), 3),
        couches_total=int(threads_total),
        dE_aplat_par_zone={"mean": round(float(e_flat.mean()), 2), "p90": round(float(np.percentile(e_flat, 90)), 2)},
        dE_melange={"mean": round(float(e_mix.mean()), 2), "p90": round(float(np.percentile(e_mix, 90)), 2)},
    )

    # Base de comparaison : quantification k-means globale au même nombre de fils.
    flatq = lin_s.reshape(-1, 3)
    C8, _ = kmeans_det(flatq[:: max(1, len(flatq) // 30000)], N)
    d = ((flatq[:, None, :] - C8[None]) ** 2).sum(-1).argmin(1)
    q8 = C8[d].reshape(lin_s.shape)
    e_q8 = de76(color.rgb2lab(lin_to_srgb(q8)), lab_s)
    report["dE_quantif_palette"] = {"mean": round(float(e_q8.mean()), 2), "p90": round(float(np.percentile(e_q8, 90)), 2)}

    # --- Q3 : couture simulée, puis vue « à distance » (flou 1 mm).
    sim = simulate(Wmap, None, (H, W), z, zone_pal)
    sim_srgb = lin_to_srgb(sim)
    sub = 4
    sim_far_lin = np.stack([ndi.gaussian_filter(sim[..., c], 1.0 / (MM_PER_PX / sub)) for c in range(3)], -1)
    sim_far = lin_to_srgb(sim_far_lin[sub // 2 :: sub, sub // 2 :: sub][:H, :W])
    e_sim = de76(color.rgb2lab(sim_far), lab_s)
    report["dE_couture_vue_1mm"] = {"mean": round(float(e_sim.mean()), 2), "p90": round(float(np.percentile(e_sim, 90)), 2)}

    # Images
    def save(a, n, scale=1):
        im = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))
        if scale != 1:
            im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        im.save(outdir / f"{name}_{n}.png")

    bounds = segmentation.find_boundaries(z, mode="inner")
    zv = rgb.copy()
    zv[bounds] = [1, 0, 1]
    save(zv, "zones")
    save(lin_to_srgb(q8), "quantif")
    save(lin_to_srgb(mix), "melange")
    save(sim_srgb, "couture")
    save(sim_far, "couture_loin")
    (outdir / f"{name}.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report))


if __name__ == "__main__":
    main()
