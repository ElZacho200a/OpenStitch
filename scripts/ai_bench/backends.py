# SPDX-License-Identifier: Apache-2.0
"""Backends compares par le benchmark.

- `cielab`  : baseline OpenStitch actuelle (`openstitch-cli project digitize`), skip si CLI absent.
- `sam2`    : worker SAM 2.1 existant (JSON Lines v1), skip si poids/environnement absents.
- `oracle`, `lighthouse_only` : temoins de controle de la metrique (jamais compares a un vrai modele).

Un backend retourne `BackendResult` ; ni score ni masque n'est jamais invente : un backend
indisponible rend status="skipped" avec la raison.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import queue
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngio  # noqa: E402
from metrics import Grid, Region  # noqa: E402

BACKGROUND_RGB = (240, 240, 240)  # fond neutre de regions.png (cf. debug_dump.cpp)


@dataclass
class BackendResult:
    backend: str
    status: str  # "ok" | "skipped" | "error"
    reason: str = ""
    regions: list[Region] = field(default_factory=list)
    meta: dict = field(default_factory=dict)
    label_map: list[int] | None = None  # etiquettes par pixel, pour les images de comparaison
    project_check: dict | None = None
    project_check_reason: str = ""


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def color_components(width: int, height: int, rgb: bytes, ignore=BACKGROUND_RGB) -> list[int]:
    """Etiquettes 4-connexes de pixels de meme couleur exacte (0 = fond ignore)."""
    labels = [0] * (width * height)
    nxt = 0
    for start in range(width * height):
        if labels[start]:
            continue
        color = rgb[start * 3 : start * 3 + 3]
        if tuple(color) == ignore:
            continue
        nxt += 1
        labels[start] = nxt
        stack = [start]
        while stack:
            p = stack.pop()
            py, px = divmod(p, width)
            for ny, nx in ((py - 1, px), (py + 1, px), (py, px - 1), (py, px + 1)):
                if 0 <= ny < height and 0 <= nx < width:
                    q = ny * width + nx
                    if not labels[q] and rgb[q * 3 : q * 3 + 3] == color:
                        labels[q] = nxt
                        stack.append(q)
    return labels


def regions_from_labels(grid: Grid, labels: list[int], prefix: str) -> list[Region]:
    buckets: dict[int, bytearray] = {}
    for i, v in enumerate(labels):
        if v:
            buckets.setdefault(v, bytearray(len(labels)))[i] = 1
    return [Region(f"{prefix}{k}", grid.pack(buckets[k])) for k in sorted(buckets)]


def _gold_flat(case_root: Path, obj: dict) -> bytes:
    w, h, ch, px = pngio.decode((case_root / obj["gold_mask"]["path"]).read_bytes())
    return bytes(1 if v else 0 for v in pngio.to_gray(w, h, ch, px))


class Oracle:
    name = "oracle"

    def run(self, case: dict, case_root: Path, grid: Grid, workdir: Path) -> BackendResult:
        regions, labels = [], [0] * (grid.width * grid.height)
        for obj in case["objects"]:
            flat = _gold_flat(case_root, obj)
            regions.append(Region(obj["semantic_id"], grid.pack(flat)))
            for i, v in enumerate(flat):
                if v:
                    labels[i] = obj["label"]
        return BackendResult(self.name, "ok", regions=regions, label_map=labels, meta={"control": True})


class LighthouseOnly:
    """Temoin negatif : segmente parfaitement le phare, regroupe tout le reste en une seule region."""

    name = "lighthouse_only"

    def run(self, case: dict, case_root: Path, grid: Grid, workdir: Path) -> BackendResult:
        size = grid.width * grid.height
        rest = bytearray(1 for _ in range(size))
        regions, labels = [], [0] * size
        for obj in case["objects"]:
            if obj["family"] != "lighthouse":
                continue
            flat = _gold_flat(case_root, obj)
            regions.append(Region(obj["semantic_id"], grid.pack(flat)))
            for i, v in enumerate(flat):
                if v:
                    rest[i] = 0
                    labels[i] = obj["label"]
        regions.append(Region("rest", grid.pack(rest)))
        for i, v in enumerate(rest):
            if v:
                labels[i] = 99
        return BackendResult(self.name, "ok", regions=regions, label_map=labels, meta={"control": True})


class Cielab:
    name = "cielab"

    def __init__(self, cli: str | None, max_colors: int = 12, skip_background: str = "no") -> None:
        self.cli = None
        if cli:
            found = shutil.which(cli) or (str(Path(cli).resolve()) if Path(cli).is_file() else None)
            self.cli = found
        self.max_colors = max_colors
        self.skip_background = skip_background

    def _run(self, args: list[str]) -> subprocess.CompletedProcess:
        return subprocess.run([self.cli, *args], capture_output=True, text=True, timeout=300, check=False)

    def run(self, case: dict, case_root: Path, grid: Grid, workdir: Path) -> BackendResult:
        if not self.cli:
            return BackendResult(self.name, "skipped", "openstitch-cli introuvable (--cli ou OPENSTITCH_CLI)")
        source = case_root / case["source"]["path"]
        out = workdir / "cielab"
        out.mkdir(parents=True, exist_ok=True)
        dpi = case["px_per_mm"] * 25.4
        proc = self._run(
            ["project", "digitize", str(source), "-o", str(out / "baseline.osp"), "--dpi", f"{dpi:g}",
             "--max-colors", str(self.max_colors), "--skip-background", self.skip_background,
             "--debug-dir", str(out / "debug"), "--report", str(out / "digitize.json")]
        )
        if proc.returncode != 0 or not (out / "debug" / "regions.png").is_file():
            return BackendResult(self.name, "error", f"digitize a echoue (code {proc.returncode}) : {proc.stderr.strip()[:300]}")
        w, h, ch, px = pngio.decode((out / "debug" / "regions.png").read_bytes())
        if (w, h) != (grid.width, grid.height) or ch < 3:
            return BackendResult(self.name, "error", "regions.png de dimensions ou format inattendus")
        rgb = px if ch == 3 else b"".join(px[i : i + 3] for i in range(0, len(px), 4))
        labels = color_components(w, h, rgb)
        result = BackendResult(
            self.name, "ok", regions=regions_from_labels(grid, labels, "r"), label_map=labels,
            meta={"max_colors": self.max_colors, "skip_background": self.skip_background, "dpi": dpi,
                  "other_options": "valeurs par defaut du CLI (voir openstitch_cli dans runner)",
                  "region_source": "debug-dir regions.png, composantes 4-connexes de meme couleur"},
        )
        check = self._run(["project", "check", str(out / "baseline.osp"), "--json"])
        try:
            result.project_check = json.loads(check.stdout)
        except ValueError:
            result.project_check_reason = f"project check illisible (code {check.returncode})"
        return result


class Sam2:
    """Pilote le worker SAM 2.1 existant (protocole JSON Lines v1) sans le modifier."""

    name = "sam2"

    def __init__(self, models_dir: str | None, model: str = "small", profile: str = "balanced",
                 python: str | None = None, worker: Path | None = None, timeout: float = 900.0) -> None:
        self.models_dir, self.model, self.profile = models_dir, model, profile
        self.python = python or sys.executable
        self.worker = worker or Path(__file__).resolve().parents[2] / "sam-worker" / "openstitch_sam_worker.py"
        self.timeout = timeout

    CHECKPOINTS = {"tiny": "sam2.1_hiera_tiny.pt", "small": "sam2.1_hiera_small.pt",
                   "base_plus": "sam2.1_hiera_base_plus.pt", "large": "sam2.1_hiera_large.pt"}

    def _wait(self, q: "queue.Queue[dict | None]", wanted: set[str], req_id: str | None):
        import time
        deadline = time.monotonic() + self.timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError("delai depasse en attendant " + "/".join(sorted(wanted)))
            try:
                msg = q.get(timeout=left)
            except queue.Empty:
                raise TimeoutError("delai depasse")
            if msg is None:
                raise EOFError("le worker s'est arrete")
            if msg.get("type") == "error" and (req_id is None or msg.get("id") in (req_id, None)):
                return msg
            if msg.get("type") in wanted and (req_id is None or msg.get("id") in (req_id, None)):
                return msg

    def run(self, case: dict, case_root: Path, grid: Grid, workdir: Path) -> BackendResult:
        if not self.models_dir:
            return BackendResult(self.name, "skipped", "poids absents : definir --sam-models ou OPENSTITCH_SAM_MODELS")
        ckpt = Path(self.models_dir) / self.CHECKPOINTS.get(self.model, "")
        if not ckpt.is_file():
            return BackendResult(self.name, "skipped", f"checkpoint absent : {ckpt.name}")
        if not self.worker.is_file():
            return BackendResult(self.name, "skipped", "sam-worker/openstitch_sam_worker.py introuvable")
        job = workdir / "sam2"
        job.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(case_root / case["source"]["path"], job / "input.png")
        proc = subprocess.Popen(
            [self.python, str(self.worker), "--models-dir", str(self.models_dir), "--device", "auto"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding="utf-8", bufsize=1,
        )
        q: "queue.Queue[dict | None]" = queue.Queue()

        def pump():
            for line in proc.stdout:
                line = line.strip()
                if line:
                    try:
                        q.put(json.loads(line))
                    except ValueError:
                        q.put({"type": "error", "code": "STDOUT_NOT_PROTOCOL", "message": line[:200]})
            q.put(None)

        threading.Thread(target=pump, daemon=True).start()

        def send(payload):
            proc.stdin.write(json.dumps(payload) + "\n")
            proc.stdin.flush()

        try:
            ready = self._wait(q, {"worker_ready"}, None)
            if ready.get("type") == "error":
                return BackendResult(self.name, "skipped", f"worker non demarre : {ready.get('code')}")
            version = ready.get("protocol_version")
            send({"id": "load", "type": "load_model", "model": self.model})
            loaded = self._wait(q, {"model_ready"}, "load")
            if loaded.get("type") == "error":
                return BackendResult(self.name, "skipped", f"{loaded.get('code')} : {loaded.get('message')}")
            send({"id": "seg", "type": "segment_image", "job_dir": str(job), "image_file": "input.png",
                  "profile": self.profile})
            done = self._wait(q, {"segment_result", "cancelled"}, "seg")
            if done.get("type") != "segment_result":
                return BackendResult(self.name, "error", f"{done.get('code', done.get('type'))} : {done.get('message', '')}")
            doc = json.loads((job / done.get("masks_file", "masks.json")).read_text(encoding="utf-8"))
            regions, labels = [], [0] * (grid.width * grid.height)
            for entry in sorted(doc["masks"], key=lambda e: e["id"]):
                w, h, ch, px = pngio.decode((job / entry["file"]).read_bytes())
                if (w, h) != (grid.width, grid.height):
                    return BackendResult(self.name, "error", "masque de dimensions differentes de la source")
                flat = bytes(1 if v else 0 for v in pngio.to_gray(w, h, ch, px))
                regions.append(Region(f"m{entry['id']}", grid.pack(flat), entry.get("predicted_iou")))
            # carte d'etiquettes : plus petit masque en dernier (il recouvre les grands)
            for r in sorted(regions, key=lambda r: -r.mask.bit_count()):
                flat = grid.unpack(r.mask)
                idx = int(r.region_id[1:]) + 1
                for i, v in enumerate(flat):
                    if v:
                        labels[i] = idx
            return BackendResult(
                self.name, "ok", regions=regions, label_map=labels,
                meta={"model": self.model, "profile": self.profile, "model_sha256": sha256_file(ckpt),
                      "worker_protocol_version": version, "mask_count": len(regions)},
            )
        except (TimeoutError, EOFError, OSError, ValueError, KeyError) as exc:
            return BackendResult(self.name, "error", f"{type(exc).__name__} : {exc}")
        finally:
            try:
                send({"id": "bye", "type": "shutdown"})
            except OSError:
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            for stream in (proc.stdin, proc.stdout):
                try:
                    stream.close()
                except OSError:
                    pass


def available(cli: str | None, sam_models: str | None, sam_model: str, sam_python: str | None,
              pipelines: dict | None = None) -> dict:
    pools = {name: PlanThenSegment(name, p.get("planner"), p.get("segmenter"), name)
             for name, p in (pipelines or {}).items()}
    return {
        **pools,
        "cielab": Cielab(cli),
        "sam2": Sam2(sam_models, model=sam_model, python=sam_python),
        "oracle": Oracle(),
        "lighthouse_only": LighthouseOnly(),
    }


class PlanThenSegment:
    """Pipeline en deux etages via le protocole generique : un planner (Florence-2) localise les
    phrases du plan de scene, un segmenteur guide par boites (SAM 2.1, HQ-SAM 2, MobileSAM) rend
    un masque par detection. Le plan de scene vient du manifeste (phrases seulement, aucun masque or).
    Chaque masque est une instance de FAMILLE (voilier, phare...) : pas de sous-composants."""

    def __init__(self, name: str, planner: list[str] | None, segmenter: list[str] | None, label: str) -> None:
        self.name, self.planner, self.segmenter, self.label = name, planner, segmenter, label

    def run(self, case: dict, case_root: Path, grid: Grid, workdir: Path) -> BackendResult:
        from workerclient import WorkerError, WorkerSession

        if not self.planner or not self.segmenter:
            return BackendResult(self.name, "skipped", "commande du planner ou du segmenteur non configuree (--planner / --segmenter)")
        plan = case.get("scene_plan")
        if not plan:
            return BackendResult(self.name, "skipped", "le cas n'a pas de scene_plan")
        job = workdir / self.name
        job.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(case_root / case["source"]["path"], job / "input.png")
        phrases = ". ".join(p["phrase"] for p in plan)
        planner = segmenter = None
        try:
            planner = WorkerSession(self.planner)
            caps_p = planner.request("capabilities")["backend"]
            res = planner.request("predict", job_dir=str(job), image_file="input.png",
                                  prompts=[{"kind": "text", "text": phrases}], options={})
            detections = res.get("detections", [])
            planner.close()
            planner = None
            # etiquette Florence -> famille du plan (correspondance exacte de phrase, sinon ignore)
            by_phrase = {p["phrase"].lower(): p["family"] for p in plan}
            kept = [(d, by_phrase[d["label"].lower()]) for d in detections if d["label"].lower() in by_phrase]
            if not kept:
                return BackendResult(self.name, "ok", regions=[], label_map=[0] * (grid.width * grid.height),
                                     meta={"planner": caps_p, "detections": detections,
                                           "note": "aucune detection associee au plan"})
            segmenter = WorkerSession(self.segmenter)
            caps_s = segmenter.request("capabilities")["backend"]
            res2 = segmenter.request("predict", job_dir=str(job), image_file="input.png",
                                     prompts=[{"kind": "box", "box_xywh": d["bbox_xywh"]} for d, _ in kept], options={})
            regions, labels = [], [0] * (grid.width * grid.height)
            for m in res2["masks"]:
                w, h, ch, px = pngio.decode((job / m["file"]).read_bytes())
                flat = bytes(1 if v else 0 for v in pngio.to_gray(w, h, ch, px))
                fam = kept[m["prompt_index"]][1]
                regions.append(Region(f"{fam}#{m['id']}", grid.pack(flat), m.get("confidence")))
                for i, v in enumerate(flat):
                    if v:
                        labels[i] = 1 + m["prompt_index"]
            return BackendResult(
                self.name, "ok", regions=regions, label_map=labels,
                meta={"planner": {k: caps_p.get(k) for k in ("backend_id", "backend_revision", "model_hash")},
                      "segmenter": {k: caps_s.get(k) for k in ("backend_id", "backend_revision", "model_hash")},
                      "detections": [{"label": d["label"], "bbox_xywh": d["bbox_xywh"]} for d in detections],
                      "protocol": PLAN_PROTOCOL, "level": "family"})
        except (WorkerError, OSError, KeyError, ValueError) as exc:
            code = getattr(exc, "code", type(exc).__name__)
            status = "skipped" if code in ("SAM2_NOT_INSTALLED", "CHECKPOINT_NOT_FOUND", "MODEL_NOT_INSTALLED") else "error"
            return BackendResult(self.name, status, f"{code} : {exc}")
        finally:
            for s in (planner, segmenter):
                if s is not None:
                    s.close()


PLAN_PROTOCOL = "openstitch.ai.backend/1"
