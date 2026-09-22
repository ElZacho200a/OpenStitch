---
name: openstitch-stitch-param
description: Checklist bout en bout pour ajouter ou modifier un paramètre de couture (champ de TatamiParams, SatinParams, DirectionalFillParams…), un nouveau type de point (alternative de StitchParams), ou tout champ persistant d'un objet du document. À utiliser dès qu'une fonctionnalité demande « un nouveau réglage », « une option dans l'inspecteur », « un nouveau type de remplissage », ou touche à embroidery_object.hpp / project.hpp.
---

# Ajouter un paramètre ou un type de point

Un réglage de broderie traverse **toutes les couches** : modèle → format
`.osp` → génération → commande (undo) → inspecteur → tests → doc. En oublier
une donne des bugs silencieux (réglage perdu à la sauvegarde, non annulable,
ignoré à l'export). Suivre la checklist dans l'ordre ; cocher chaque étape
dans le rapport final.

Avant de commencer : `openstitch-docs` sur le sujet (le paramètre existe
peut-être déjà sous un autre nom, ou a été rejeté pour une raison documentée).

## 1. Modèle — `libs/document`

Fichier : `libs/document/include/openstitch/document/embroidery_object.hpp`
(ou `project.hpp` / `finishing.hpp` pour un réglage global).

- Type fort : `Micrometers` pour toute longueur, `Angle` pour un angle,
  jamais un `double` brut pour une coordonnée. `double` seulement pour un
  ratio sans unité (documenter la plage, ex. `[0 ; 1]`).
- **Valeur par défaut = comportement actuel.** Les `.osp` existants et les
  objets déjà créés ne doivent pas changer de rendu. Si le nouveau
  comportement doit s'appliquer aux nouveaux objets seulement, le régler
  à la création (auto-numérisation, `AutoOptions`, UI), pas via le défaut.
- Commentaire en français : rôle, unité, plage, valeur en mm lisible
  (`Micrometers row_spacing{400}; // 0,4 mm`).
- Le struct doit garder `bool operator==(const X&) const = default;`
  (utilisé par les commandes et les tests de round-trip).
- **Nouveau type de point** : ajouter l'alternative **en fin** de
  `std::variant StitchParams` (les index existants ne bougent pas), plus un
  prédicat `is_<type>()` sur `EmbroideryObject`.

## 2. Format `.osp` — `libs/project_io/src/json_serialize.cpp`

- Écriture : clé en **camelCase** (`rowSpacing`), valeur en µm entiers
  (`.value`).
- Lecture : **toujours `j.value("cle", <défaut du modèle>)`**, jamais
  `j.at()` pour un nouveau champ — un `.osp` antérieur doit se relire à
  l'identique. Le défaut en lecture doit être le même que dans le struct.
- Nouveau type : nouvelle valeur de `"type"` dans le switch lecture/écriture,
  toutes les clés sauf `type` optionnelles.
- Ne monter `kSchemaVersion` (`project_io.cpp`) que si l'ancien lecteur ne
  peut **pas** ignorer la nouveauté sans erreur grave — sinon non.
- Doc : `docs/source/project-format.md` (clé, unité, défaut).

## 3. Génération — `libs/stitch_generation`

- Brancher le paramètre dans le générateur (`generate.cpp`,
  `tatami.cpp`, `satin*.cpp`, `directional_fill.cpp`…). Nouveau type :
  nouveau `std::visit`/branche dans `generate.cpp`, générateur dans son
  propre `.cpp` + en-tête public, ajouté au `CMakeLists.txt` de la lib.
- Fonction pure sur un snapshot immuable, **déterministe** (graine stockée
  dans l'objet si pseudo-aléatoire, jamais `std::random_device`/heure).
- Borner les valeurs aberrantes à la génération (longueur de point
  dans [1 ; 7] mm, etc.) plutôt que de faire confiance au fichier.
- Les consommateurs passent par `effective_sequence` : rien à faire côté
  aperçu/export si le générateur est bien branché.
- Retouches manuelles (Lot 8) : changer un paramètre change la séquence
  brute → l'empreinte des overrides ne correspond plus et ils sont
  invalidés (`overrides.cpp`). C'est voulu ; vérifier que l'UI le signale
  si le paramètre est modifiable après retouche.

## 4. Commande — `libs/commands/include/openstitch/commands/project_commands.hpp`

- Un réglage d'inspecteur passe en général par **`SetStitchParamsCommand`**
  (remplace le `StitchParams` complet) : rien à créer.
- Nouveau geste spécifique (édition sur le canevas, conversion…) : nouvelle
  classe `final : public ICommand` qui mémorise l'état précédent dans
  `apply` et le restaure **exactement** dans `revert` ; no-op si l'objet est
  introuvable ou n'a plus le bon type ; `name()` en français lisible
  (« Orientation du remplissage »).
- Nouveau type : vérifier `SetStitchTypeCommand` / `ConvertFillGroupCommand`
  (conversion depuis/vers le nouveau type, sections sœurs d'un même
  `source_vector`).
- Si le paramètre contient des coordonnées (guides, points) : les faire
  suivre dans `TranslateVectorObjectCommand` / `ScaleVectorObjectCommand`.

## 5. Desktop — `apps/desktop` (aucune logique métier)

- `properties_panel.cpp/.hpp` : widget (spinbox en **mm** pour l'affichage,
  conversion µm↔mm à la frontière), émis vers `MainWindow` qui pousse la
  commande sur l'`UndoStack`. Jamais de mutation directe du `Project`.
- Nouveau type : `document_panel.cpp` (icône/libellé), menus et barre
  d'outils de `main_window.cpp`, `tools.hpp` si un outil de canevas est
  nécessaire. Gros ajout → fichier dédié (`main_window_<sujet>.cpp`,
  à ajouter à `apps/desktop/CMakeLists.txt`).
- Pas de membre nommé `slots` ; déclarations anticipées Qt à portée globale.

## 6. Tests (un par couche touchée)

| Couche | Fichier | Quoi |
|---|---|---|
| Génération | `tests/unit/stitch/test_*.cpp` | effet du paramètre, bornes, **déterminisme** |
| `.osp` | `tests/unit/project_io/test_roundtrip.cpp` | round-trip égal (`==`) **et** lecture d'un JSON sans la clé → défaut |
| Commande | `tests/unit/commands/test_undo_stack.cpp` | apply → revert → état identique |
| Inspecteur | `tests/unit/desktop/test_properties_panel.cpp` | le widget émet la bonne valeur |
| Chemin UI | `tests/unit/desktop/test_main_window.cpp` | geste réel → undo |
| Export | `tests/unit/formats/test_dst.cpp` | si le réglage change ce qui sort en DST |

Puis compiler/tester selon `openstitch-build-test`.

## 7. Documentation

Via `openstitch-docs` : chapitre du sous-système (`tatami.md`,
`directional-fill.md`, `satin.md`…), `embroidery-objects.md` (tableau des
paramètres), `project-format.md`, `module-reference.md` si nouveau fichier ;
`user-guide.md` si visible dans l'IHM. Régénérer le PDF.
