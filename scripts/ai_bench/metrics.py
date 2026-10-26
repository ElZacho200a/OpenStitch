# SPDX-License-Identifier: Apache-2.0
"""Metriques de segmentation par objet protege, en bibliotheque standard.

Un masque est un entier Python a un octet (0/1) par pixel, avec une colonne de
garde (stride = largeur + 1) : les decalages de bits donnent dilatation et
erosion a la vitesse du C sans numpy.

Aucune valeur n'est inventee : une metrique non calculable vaut `None` et la
raison est portee par `reasons`.
"""
from __future__ import annotations

from dataclasses import dataclass, field

PURITY = 0.5  # une region predite "appartient" a un objet si >= 50 % de sa surface y est
PRESENT_IOU = 0.5  # seuil de presence d'une instance


class Grid:
    def __init__(self, width: int, height: int) -> None:
        self.width, self.height = width, height
        self.stride = width + 1
        self.size = self.stride * height
        valid = bytearray(self.size)
        for y in range(height):
            valid[y * self.stride : y * self.stride + width] = b"\x01" * width
        self.valid = int.from_bytes(valid, "little")

    def pack(self, flat: bytes | bytearray) -> int:
        """Octets 0/1 (largeur*hauteur) -> masque entier a colonne de garde."""
        buf = bytearray(self.size)
        w, s = self.width, self.stride
        for y in range(self.height):
            buf[y * s : y * s + w] = flat[y * w : (y + 1) * w]
        return int.from_bytes(buf, "little")

    def unpack(self, mask: int) -> bytearray:
        raw = mask.to_bytes(self.size, "little")
        w, s = self.width, self.stride
        out = bytearray(w * self.height)
        for y in range(self.height):
            out[y * w : (y + 1) * w] = raw[y * s : y * s + w]
        return out

    def dilate(self, mask: int, radius: int = 1) -> int:
        for _ in range(radius):
            m = mask
            s8, one = self.stride * 8, 8
            rows = m | (m << s8) | (m >> s8)
            mask = (rows | (rows << one) | (rows >> one)) & self.valid
        return mask

    def erode(self, mask: int) -> int:
        return mask & (self.valid ^ self.dilate(self.valid ^ mask))

    def boundary(self, mask: int) -> int:
        return mask & ~self.erode(mask) & self.valid


def count(mask: int) -> int:
    return mask.bit_count()


def iou(a: int, b: int) -> float | None:
    union = count(a | b)
    return None if union == 0 else count(a & b) / union


def dice(a: int, b: int) -> float | None:
    total = count(a) + count(b)
    return None if total == 0 else 2 * count(a & b) / total


def tolerant_f(grid: Grid, pred: int, gold: int, tol_px: int) -> float | None:
    """F-mesure de deux ensembles de pixels avec tolerance spatiale tol_px."""
    if count(pred) == 0 or count(gold) == 0:
        return None if count(gold) == 0 and count(pred) == 0 else 0.0
    precision = count(pred & grid.dilate(gold, tol_px)) / count(pred)
    recall = count(gold & grid.dilate(pred, tol_px)) / count(gold)
    return 0.0 if precision + recall == 0 else 2 * precision * recall / (precision + recall)


def components_and_holes(flat: bytes | bytearray, width: int, height: int) -> tuple[int, int]:
    """(composantes 8-connexes, fonds fermes 4-connexes) d'un masque 0/1."""
    seen = bytearray(len(flat))
    comps = 0
    for start in range(len(flat)):
        if flat[start] and not seen[start]:
            comps += 1
            stack, seen[start] = [start], 1
            while stack:
                p = stack.pop()
                py, px = divmod(p, width)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = py + dy, px + dx
                        if 0 <= ny < height and 0 <= nx < width:
                            q = ny * width + nx
                            if flat[q] and not seen[q]:
                                seen[q] = 1
                                stack.append(q)
    outside = bytearray(len(flat))
    holes = 0
    for start in range(len(flat)):
        if not flat[start] and not outside[start]:
            touches, stack, outside[start] = False, [start], 1
            while stack:
                p = stack.pop()
                py, px = divmod(p, width)
                touches |= py in (0, height - 1) or px in (0, width - 1)
                for ny, nx in ((py - 1, px), (py + 1, px), (py, px - 1), (py, px + 1)):
                    if 0 <= ny < height and 0 <= nx < width:
                        q = ny * width + nx
                        if not flat[q] and not outside[q]:
                            outside[q] = 1
                            stack.append(q)
            holes += 0 if touches else 1
    return comps, holes


@dataclass
class Region:
    region_id: str
    mask: int  # masque entier (voir Grid.pack)
    score: float | None = None  # confiance rapportee par le backend, jamais synthetisee


@dataclass
class ObjectScore:
    semantic_id: str
    family: str
    kind: str
    required: bool
    protected: bool
    present: bool
    best_single_iou: float | None
    union_iou: float | None
    union_dice: float | None
    edge_f: float | None  # F-mesure tolerante : contour pour une aire, pixels pour un trait
    edge_metric: str
    regions_used: int  # fragmentation : nombre de regions "pures" regroupees
    components_gold: int
    components_pred: int
    holes_gold: int | None
    holes_pred: int | None
    holes_preserved: bool | None
    reasons: list[str] = field(default_factory=list)


def score_object(grid: Grid, obj: dict, gold: int, regions: list[Region], tol_px: int) -> ObjectScore:
    reasons: list[str] = []
    gold_area = count(gold)
    best_single, pure = 0.0, []
    for r in regions:
        inter = count(r.mask & gold)
        if inter == 0:
            continue
        area = count(r.mask)
        j = inter / (area + gold_area - inter)
        best_single = max(best_single, j)
        if inter / area >= PURITY:
            pure.append(r)
    union = 0
    for r in pure:
        union |= r.mask
    u_iou, u_dice = iou(union, gold), dice(union, gold)
    present = bool(u_iou is not None and u_iou >= PRESENT_IOU)
    if not pure:
        reasons.append("aucune region predite n'appartient majoritairement a cet objet")
    kind = obj["kind"]
    edge_metric = "pixel_f" if kind == "line" else "boundary_f"
    if kind == "line":
        edge = tolerant_f(grid, union, gold, tol_px)
    else:
        edge = tolerant_f(grid, grid.boundary(union), grid.boundary(gold), tol_px) if union else 0.0
    w, h = grid.width, grid.height
    cg, hg = components_and_holes(grid.unpack(gold), w, h)
    cp, hp = components_and_holes(grid.unpack(union), w, h) if union else (0, 0)
    holes_ok = None
    if obj.get("protect_holes"):
        holes_ok = hp >= hg
        if not holes_ok:
            reasons.append(f"trou(s) perdu(s) : {hp} predit(s) pour {hg} attendu(s)")
    return ObjectScore(
        semantic_id=obj["semantic_id"],
        family=obj["family"],
        kind=kind,
        required=bool(obj.get("required")),
        protected=bool(obj.get("protected")),
        present=present,
        best_single_iou=round(best_single, 4),
        union_iou=None if u_iou is None else round(u_iou, 4),
        union_dice=None if u_dice is None else round(u_dice, 4),
        edge_f=None if edge is None else round(edge, 4),
        edge_metric=edge_metric,
        regions_used=len(pure),
        components_gold=cg,
        components_pred=cp,
        holes_gold=hg if obj.get("protect_holes") else None,
        holes_pred=hp if obj.get("protect_holes") else None,
        holes_preserved=holes_ok,
        reasons=reasons,
    )


def semantic_gate(scores: list[ObjectScore], main_families: list[str]) -> dict:
    """Porte bloquante : tout objet `required` doit etre present. Un sujet principal
    absent est un echec, quel que soit le score geometrique des autres objets."""
    missing = [s.semantic_id for s in scores if s.required and not s.present]
    families = {}
    for fam in main_families:
        mine = [s for s in scores if s.family == fam]
        families[fam] = {
            "objects": len(mine),
            "present": sum(1 for s in mine if s.present),
            "required_missing": [s.semantic_id for s in mine if s.required and not s.present],
        }
    return {"passed": not missing, "required_missing": missing, "families": families}
