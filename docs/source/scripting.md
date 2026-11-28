# Script Python sur la sélection

Un petit script Python règle les paramètres de plusieurs objets d'un coup, selon ce qui les
distingue : couleur, taille, position, type, ou n'importe quel paramètre existant. Exemples :
desserrer les grandes zones, tourner l'angle des rangées de gauche à droite, densifier les rouges.

Le script **ne modifie jamais le document lui-même**. Il décrit des changements (`o.set(...)`) ;
OpenStitch en fait un lot d'opérations `set_params` / `set_color` limité aux objets sélectionnés,
le simule (aperçu), puis l'applique en **une seule étape d'annulation** (Ctrl+Z).

## Dans le Studio

1. Espace **Points** : ouvrir le panneau **Script** (barre des panneaux, ou clic droit ›
   « Script sur la sélection… »).
2. Sélectionner les objets : boutons « Tout sélectionner » et « Même couleur » du panneau, liste
   Objets à droite, ou canevas (Ctrl+clic, rectangle).
3. Choisir un **exemple** et l'adapter dans l'éditeur (Tab insère quatre espaces ; Échap quitte
   l'éditeur). Choisir un autre exemple remplace le script : « Restaurer mon script » le rend.
   L'aide repliable du panneau liste ce que le script peut lire, avec unités et plages.
4. **Aperçu** : une ligne par changement, « Remplissage Région 20 · Écart entre rangées (mm) :
   0,4 → 0,45 », sans rien modifier. Ce que le script affiche avec `print` apparaît en dessous.
5. **Appliquer** : une étape d'annulation. Sans aperçu à jour (script, sélection ou motif
   modifiés), « Appliquer » relance d'abord le script.

Une erreur indique la ligne et la cause (`ligne 3 : AttributeError: …`) ; un paramètre inconnu
ou une valeur hors plage est refusé à l'aperçu, document intact.

## En ligne de commande

```powershell
openstitch-cli project script motif.osp regles.py --where color=#D62828 --dry-run
openstitch-cli project script motif.osp regles.py -o motif-regle.osp --json
```

`--where` reprend les critères de `project select` (défaut : tous les objets). La sortie du
script (`print`) va sur stderr, le rapport du lot (`--json`) sur stdout. Codes d'erreur :
`E_SCRIPT_NO_SELECTION`, `E_SCRIPT_ERROR`, `E_SCRIPT_TIMEOUT`, `E_SCRIPT_REFUSED`,
`E_SCRIPT_UNAVAILABLE`.

## Ce que le script peut lire

| Nom | Contenu |
|---|---|
| `selection` | objets choisis, dans l'ordre de la sélection (modifiables) |
| `objects` | tous les objets de broderie (lecture seule) |
| `canvas` | cadre : `width_mm`, `height_mm`, `margin_mm`, `shape`… |
| `Color`, `Vec`, `math` | utilitaires |

Chaque objet `o` :

| Attribut | Contenu |
|---|---|
| `o.id`, `o.name`, `o.order` | identifiant stable, nom, rang de couture |
| `o.type` | `tatami`, `satin`, `running`, `directional`, `auto_satin`… |
| `o.color` | `Color` : `hex`, `r`/`g`/`b` (0-255), `hue` (0-360), `saturation` et `lightness` (0-100), `distance(autre)`, `is_close(autre, tolérance)` ; comparable à `"#RRGGBB"` |
| `o.area_mm2` | aire de la forme source |
| `o.bounds`, `o.width_mm`, `o.height_mm` | boîte englobante (mm, Y vers le haut) |
| `o.center`, `o.x`, `o.y` | centre de la boîte |
| `o.params` | tous les paramètres actuels (mêmes noms que l'opération `set_params`) |
| `o.row_spacing_mm`… | un paramètre se lit aussi directement par son nom |
| `o.visible`, `o.locked`, `o.intent` | état et rôle |
| `o.stitches` | nombre de points (CLI `--with-stitches` ; sinon `None`) |

Écrire : `o.set(row_spacing_mm=0.4, angle_deg=45)`, `o.row_spacing_mm = 0.4` (équivalent),
`o.set_color("#FF0000")`. Les noms et plages sont ceux de `set_params`
(`openstitch-cli project operations`). Un type sans le paramètre (un satin n'a pas d'écart de
rangées) le refuse : tester `"row_spacing_mm" in o.params`.

```python
# Moitié gauche à 45°, moitié droite à 135°.
milieu = canvas["width_mm"] / 2
for o in selection:
    if "angle_deg" in o.params:
        o.set(angle_deg=45 if o.x < milieu else 135)
```

## Garanties et limites

- **Déterminisme** : interpréteur isolé (ni variables `PYTHON*`, ni `site-packages`), graine de
  hachage fixe : même projet, même sélection, même script → même lot octet pour octet.
- **Délai** : 5 s par défaut (`--timeout-ms`). Une boucle sans fin est interrompue
  (`ScriptTimeout`, non rattrapée par `except Exception`). Un appel C très long
  (`sum(range(10**12))`) n'est interrompu qu'à la ligne Python suivante.
- **Bibliothèque standard seulement** : pas de NumPy ni de modules installés à part. Le script
  peut lire et écrire des fichiers comme tout programme Python : n'exécuter que des scripts
  dont on connaît la provenance.
- **Disponibilité** : CPython 3.12 embarqué via pybind11 (vcpkg), Windows uniquement ; la
  bibliothèque standard est copiée à côté de l'exécutable (`python/Lib`, ou
  `OPENSTITCH_PYTHONHOME`). Sans elle, erreur `E_SCRIPT_UNAVAILABLE` ; la CI Linux compile sans
  cette fonction (commande et panneau absents).

## Architecture

`libs/scripting` est la seule bibliothèque qui inclut Python. Son interface
(`openstitch/scripting/selection_script.hpp`) ne transporte que des chaînes et des types
OpenStitch : le script reçoit le JSON d'inspection (`project_ops::inspect_project_json`), un
prélude Python enveloppe chaque objet, et le résultat revient sous forme de lot
`openstitch.batch/1`, appliqué par `project_ops::apply_batch` (transactionnel, `ICommand`).
L'interpréteur démarre au premier script, rend le GIL, et chaque exécution le reprend depuis son
fil (Studio : fil de travail du `TaskManager`), sous un mutex qui sérialise les scripts.
