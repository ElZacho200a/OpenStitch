// SPDX-License-Identifier: Apache-2.0
// Prélude Python du script de sélection : enveloppe chaque objet de broderie (JSON
// d'inspection `openstitch.project-inspect/1`) dans `Obj`, exécute le script de l'utilisateur
// dans un espace de noms neuf, puis rend le lot d'opérations et l'aperçu des changements.
// Aucune dépendance hors bibliothèque standard ; le C++ ne voit que des chaînes JSON.
#include "prelude.hpp"

namespace openstitch::scripting::detail {

// Littéral découpé en morceaux < 16 Kio (limite MSVC C2026).
const char* const kPrelude = R"PY(
import sys as _sys
import io as _io
import json as _json
import math
import colorsys as _colorsys
import traceback as _tb

_SCRIPT_FILE = "<script>"
_OUTPUT_LIMIT = 65536


class ScriptTimeout(BaseException):
    """Levée quand le script dépasse son délai (non rattrapée par `except Exception`)."""


class Vec:
    __slots__ = ("x", "y")

    def __init__(self, x, y):
        self.x = float(x)
        self.y = float(y)

    def __iter__(self):
        yield self.x
        yield self.y

    def __repr__(self):
        return f"Vec({self.x:g}, {self.y:g})"

    def distance(self, other):
        return math.hypot(self.x - other.x, self.y - other.y)


class Color:
    """Couleur #RRGGBB : r, g, b (0-255), hue (0-360), saturation et lightness (0-100)."""

    __slots__ = ("hex", "r", "g", "b", "hue", "saturation", "lightness")

    def __init__(self, text):
        if isinstance(text, Color):
            text = text.hex
        t = str(text).strip().lstrip("#").upper()
        if len(t) != 6 or any(c not in "0123456789ABCDEF" for c in t):
            raise ValueError(f"couleur invalide : {text!r} (attendu « #RRGGBB »)")
        self.hex = "#" + t
        self.r, self.g, self.b = (int(t[i:i + 2], 16) for i in (0, 2, 4))
        h, l, s = _colorsys.rgb_to_hls(self.r / 255.0, self.g / 255.0, self.b / 255.0)
        self.hue = h * 360.0
        self.saturation = s * 100.0
        self.lightness = l * 100.0

    def __eq__(self, other):
        if isinstance(other, str):
            try:
                other = Color(other)
            except ValueError:
                return False
        return isinstance(other, Color) and other.hex == self.hex

    def __hash__(self):
        return hash(self.hex)

    def __str__(self):
        return self.hex

    def __repr__(self):
        return f"Color('{self.hex}')"

    def distance(self, other):
        """Distance euclidienne RVB (0 = identiques, ~441 = noir/blanc)."""
        o = other if isinstance(other, Color) else Color(other)
        return math.dist((self.r, self.g, self.b), (o.r, o.g, o.b))

    def is_close(self, other, tolerance=30.0):
        return self.distance(other) <= tolerance


_MISSING = object()


class Obj:
    """Objet de broderie vu par le script. Lecture : id, name, order, type, color, visible,
    locked, intent, params, area_mm2, bounds, width_mm, height_mm, center, x, y, stitches,
    et chaque paramètre par son nom (o.row_spacing_mm). Écriture : o.set(...), o.set_color(...).
    """

    def __init__(self, data, editable):
        self.raw = data
        self.id = int(data["id"])
        self.name = data.get("name") or ""
        self.order = int(data.get("order") or 0)
        self.type = data.get("type") or ""
        self.color = Color(data.get("color") or "#000000")
        self.visible = bool(data.get("visible", True))
        self.locked = bool(data.get("locked", False))
        self.intent = data.get("intent") or ""
        self.params = dict(data.get("params") or {})
        area = data.get("region_area_mm2")
        self.area_mm2 = float(area) if area is not None else 0.0
        b = data.get("geometry_bounds")
        if b:
            x0, y0 = b["min_mm"]
            x1, y1 = b["max_mm"]
            self.bounds = (float(x0), float(y0), float(x1), float(y1))
            self.width_mm = float(x1) - float(x0)
            self.height_mm = float(y1) - float(y0)
            self.center = Vec((x0 + x1) / 2.0, (y0 + y1) / 2.0)
        else:
            self.bounds = None
            self.width_mm = 0.0
            self.height_mm = 0.0
            self.center = Vec(0.0, 0.0)
        self.x = self.center.x
        self.y = self.center.y
        st = data.get("stitches")
        self.stitches = int(st["total"]) if isinstance(st, dict) and "total" in st else None
        self._editable = editable
        self._params_set = {}
        self._color_set = None

    def __getattr__(self, name):
        # Appelé seulement quand l'attribut n'existe pas : repli sur les paramètres (valeur
        # déjà réglée par le script d'abord).
        pending = self.__dict__.get("_params_set") or {}
        if name in pending:
            return pending[name]
        params = self.__dict__.get("params") or {}
        if name in params:
            return params[name]
        raise AttributeError(
            f"objet {self.__dict__.get('id')} ({self.__dict__.get('type')}) : ni attribut ni "
            f"paramètre « {name} » ; paramètres : {', '.join(sorted(params))}")

    def __setattr__(self, name, value):
        # Après construction, « o.row_spacing_mm = 0.4 » équivaut à o.set(row_spacing_mm=0.4)
        # (sinon l'affectation serait silencieusement perdue).
        d = self.__dict__
        if "_color_set" in d and name in d["params"]:
            self.set(**{name: value})
            return
        if "_color_set" in d and name != "_color_set" and not name.startswith("_"):
            raise AttributeError(
                f"« {name} » n'est pas un paramètre de l'objet {d['id']} ({d['type']}) ; "
                f"utiliser o.set(...) ou o.set_color(...)")
        object.__setattr__(self, name, value)

    def _check_editable(self):
        if not self._editable:
            raise PermissionError(f"objet {self.id} hors sélection : lecture seule")

    def set(self, **params):
        """Règle un ou plusieurs paramètres : o.set(row_spacing_mm=0.4, angle_deg=45)."""
        self._check_editable()
        if not params:
            raise ValueError("set() attend au moins un paramètre, ex. o.set(row_spacing_mm=0.4)")
        for key, value in params.items():
            if isinstance(value, float) and not math.isfinite(value):
                raise ValueError(f"{key} : valeur non finie ({value})")
            self._params_set[key] = value

    def set_color(self, color):
        """Change la couleur du fil : o.set_color("#FF0000")."""
        self._check_editable()
        self._color_set = Color(color).hex

    def __repr__(self):
        return f"<Obj {self.id} {self.type} {self.color.hex} « {self.name} »>"
)PY"
                             R"PY(

def _format_error(exc):
    if isinstance(exc, SyntaxError) and exc.filename == _SCRIPT_FILE:
        line = (exc.text or "").strip()
        where = f"ligne {exc.lineno}" if exc.lineno else "script"
        return f"{where} : erreur de syntaxe ({exc.msg})" + (f"\n    {line}" if line else "")
    frames = [f for f in _tb.extract_tb(exc.__traceback__) if f.filename == _SCRIPT_FILE]
    head = f"{type(exc).__name__}: {exc}"
    if not frames:
        return head
    last = frames[-1]
    text = f"ligne {last.lineno} : {head}"
    if last.line:
        text += f"\n    {last.line.strip()}"
    return text


def _dump(value):
    return _json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                       separators=(",", ":"))


def _openstitch_run(source, inspect_json, selected_ids):
    doc = _json.loads(inspect_json)
    wanted = [int(i) for i in selected_ids]
    wanted_set = set(wanted)
    everything = [Obj(d, int(d["id"]) in wanted_set) for d in doc.get("embroidery_objects", [])]
    by_id = {o.id: o for o in everything}
    missing = [i for i in wanted if i not in by_id]
    if missing:
        return _dump({"status": "missing", "missing": missing})
    selection = [by_id[i] for i in wanted]

    namespace = {
        "__name__": "__script__",
        "__builtins__": __builtins__,
        "selection": selection,
        "objects": everything,
        "canvas": doc.get("canvas"),
        "Color": Color,
        "Vec": Vec,
        "math": math,
        "ScriptTimeout": ScriptTimeout,
    }
    out = _io.StringIO()
    saved = (_sys.stdout, _sys.stderr)
    _sys.stdout = out
    _sys.stderr = out
    status = "ok"
    error = ""
    try:
        code = compile(source, _SCRIPT_FILE, "exec")
        exec(code, namespace)
    except ScriptTimeout:
        status = "timeout"
    except SystemExit as exc:
        if exc.code not in (None, 0):
            status = "error"
            error = f"exit({exc.code!r})"
    except BaseException as exc:
        status = "error"
        error = _format_error(exc)
    finally:
        _sys.stdout, _sys.stderr = saved

    output = out.getvalue()
    if len(output) > _OUTPUT_LIMIT:
        output = output[:_OUTPUT_LIMIT] + "\n[sortie tronquée]"

    operations = []
    changes = []
    if status == "ok":
        try:
            for o in selection:
                if o._color_set is not None and o._color_set != o.color.hex:
                    operations.append({"op": "set_color", "object": o.id, "rgb": o._color_set})
                    changes.append({"object": o.id, "name": o.name, "key": "color",
                                    "before": _dump(o.color.hex), "after": _dump(o._color_set)})
                params = {}
                for key, value in o._params_set.items():
                    if o.params.get(key, _MISSING) == value:
                        continue
                    params[key] = value
                    before = o.params.get(key, None)
                    changes.append({"object": o.id, "name": o.name, "key": key,
                                    "before": _dump(before), "after": _dump(value)})
                if params:
                    operations.append({"op": "set_params", "object": o.id, "params": params})
            batch = _dump({"schema": "openstitch.batch/1", "operations": operations})
        except (TypeError, ValueError) as exc:
            status = "error"
            error = f"valeur non convertible en JSON : {exc}"
    if status != "ok":
        operations = []
        changes = []
        batch = _dump({"schema": "openstitch.batch/1", "operations": []})

    return _dump({"status": status, "error": error, "output": output, "batch": batch,
                  "operation_count": len(operations), "changes": changes})
)PY";

} // namespace openstitch::scripting::detail
