# Cadrage architectural — Lot 8 : édition manuelle des points

Public : mainteneur. **Document de cadrage, non implémenté.** Aucun code n'a
été modifié pour produire cette étude ; elle propose une architecture, pas une
décision arrêtée. Les sections *Décisions ouvertes* doivent être tranchées par
l'utilisateur avant tout début d'implémentation.

État vérifié au moment de l'étude : Lots 1–7 terminés (code `4b29e99`, docs
`9f9ae85`), 217/217 tests Debug et Release. Fichiers inspectés :
`libs/document/include/openstitch/document/{project,embroidery_object}.hpp`,
`libs/stitch/include/openstitch/stitch/sequence.hpp`,
`libs/stitch_generation/include/.../generate.hpp` +
`libs/stitch_generation/src/generate.cpp`,
`libs/commands/include/openstitch/commands/{command,undo_stack,project_commands}.hpp`,
`libs/project_io` (via `docs/source/project-format.md`),
`libs/formats/include/openstitch/formats/dst.hpp`, `apps/desktop/main_window.cpp`
(sections régénération, sauvegarde, import/export DST), et les chapitres
`stitch-editing.md`, `data-model.md`, `project-format.md`, `limitations.md`,
`roadmap.md`, `testing.md`.

## Constats et contradictions documentaires relevés

1. **« ADR-014 » n'existe pas comme document.** Il est cité dans
   `docs/source/tatami.md:181` et `docs/stitch-engine-audit.md:80` comme s'il
   référençait une décision d'architecture numérotée, mais aucun fichier ADR
   n'est présent dans le dépôt (`docs/phase0/08-roadmap-adr.md` ne le liste
   pas). C'est en réalité un **principe**, pas un ADR écrit : « les points
   sont une fonction pure de l'intention (objets + paramètres), jamais stockés
   ». Ce cadrage le nomme *principe de régénération* et recommande soit de
   rédiger l'ADR-014 manquant, soit de cesser d'y référer comme à un document.
2. **Le point le plus contraignant pour ce lot n'est pas documenté** :
   `generate_satin_group` (`generate.cpp:172-232`) peut **réordonner et
   inverser** les points d'une colonne satin routée en fonction de ses
   **voisines** (couleur, `source_vector`, adjacence dans
   `embroidery_objects`) — pas seulement en fonction des paramètres de l'objet
   lui-même. Un objet satin peut donc changer de séquence de points sans que
   **rien chez lui** n'ait changé (réordonnancement de la couture, changement
   de couleur d'un voisin). Toute stratégie d'identité fondée sur « les
   entrées de cet objet » (géométrie + paramètres propres) est donc **fausse**
   pour le satin routé — voir §1 pour la solution retenue.
3. **Une séquence importée d'un DST n'est aujourd'hui stockée nulle part dans
   le document.** `stitch-editing.md` dit qu'elle « est traitée comme la
   vérité », mais `MainWindow::sequence_`/`sequenceImported_`
   (`apps/desktop/main_window.hpp:192-193`) sont des champs **applicatifs**,
   absents de `document::Project` et absents de `project.json`. Conséquence
   vérifiée dans le code (`main_window.cpp:2508-2513`) : `saveProject()`
   refuse d'enregistrer si `!hasImage() && vector_objects.empty()` — un
   projet **uniquement** issu d'un import DST (aucune image, aucun objet
   vectoriel) **ne peut pas être sauvegardé en `.osp`**. Seul un ré-export DST
   est possible. Éditer manuellement une telle séquence n'a donc de sens
   aujourd'hui que pour la durée d'une session, jamais persistée. Ce point
   doit être tranché explicitement (§4 et *Décisions ouvertes*), il déborde
   du périmètre strict du Lot 8.
4. **`docs/source/roadmap.md:20-21`** annonce l'édition manuelle comme un
   bloc unique « moyen terme » sans sous-découpage — ce cadrage le décompose
   (§10).

---

## 1. Modèle de données

### Principe retenu : identité par contenu (empreinte de sortie), pas par entrée

Le constat n°2 ci-dessus interdit de fonder l'empreinte de stabilité sur « les
paramètres et la géométrie de l'objet » : trop de causes externes (routage
satin, changement de couleur d'un voisin, réordre de couture) peuvent changer
la sortie d'un objet sans toucher à ses propres champs, et l'inverse serait
fragile à maintenir (il faudrait énumérer manuellement, dans chaque commande,
tout ce qui peut invalider tout objet — source d'oubli, donc de perte
silencieuse).

Solution : chaque `StitchCommand` porte déjà `source` (`ObjectId`,
`sequence.hpp:40`). On définit donc, à partir de la séquence **brute**
(fonction pure actuelle, inchangée) :

```
raw_slice(project, object_id) = [c ∈ generate_sequence(project).commands
                                  | c.source == object_id]
```

C'est la **vue brute** de l'objet, dans l'ordre où il apparaît dans la
séquence finale — qu'il soit contigu (cas général) ou entrelacé avec des
trajets cachés (satin routé). Une empreinte entière et déterministe (FNV-1a ou
équivalent) est calculée sur cette vue (positions en µm entiers + type de
commande) : `fingerprint(raw_slice)`. Comme les coordonnées sont des `int32`
(µm), l'empreinte est reproductible bit à bit — pas d'instabilité liée aux
flottants (seul `TatamiParams.angle` est un `double`, mais il n'est jamais
recalculé avant hachage : c'est la valeur exacte posée par l'utilisateur qui
entre dans le calcul de la géométrie, donc dans `raw_slice`, de façon stable).

Cette empreinte capture **automatiquement** tout changement pertinent — y
compris les effets de voisinage du satin routé — sans qu'aucune commande
n'ait besoin de « savoir » qu'elle doit invalider tel ou tel objet ailleurs
dans le document. C'est la propriété centrale qui rend le modèle robuste aux
oublis.

### Structure minimale ajoutée

```cpp
// libs/document/include/openstitch/document/embroidery_object.hpp

enum class StitchPointType { Stitch, Jump };  // seules transitions permises (MVP)

struct StitchOverride {
    std::size_t base_index{};                  // index dans raw_slice(object)
    std::optional<Vec2um> moved_to;             // nullopt = position générée
    std::optional<StitchPointType> forced_type; // nullopt = type généré
    bool trim_after{false};                     // insère un Trim juste après ce point
};

struct EmbroideryObject {
    // ... champs existants inchangés ...
    std::vector<StitchOverride> overrides;      // vide = comportement actuel (rétrocompatible)
    std::uint64_t edited_fingerprint{0};        // fingerprint(raw_slice) au moment de la dernière édition réussie
};
```

Pas de duplication de la séquence : les overrides sont des **deltas épars**
(quelques entrées, jamais O(nombre de points)). `edited_fingerprint` n'est
significatif que si `overrides` n'est pas vide.

### États Clean / ManuallyEdited / Dirty — dérivés, pas stockés

L'état n'est **pas** un champ à synchroniser (source d'incohérence), il se
**calcule** à la demande :

| État | Condition | Comportement de génération |
|---|---|---|
| `Clean` | `overrides.empty()` | Séquence brute, inchangé (comportement actuel) |
| `ManuallyEdited` | `!overrides.empty() && fingerprint(raw_slice) == edited_fingerprint` | Séquence brute **patchée** par les overrides (positions, type, trims insérés) |
| `Dirty` | `!overrides.empty() && fingerprint(raw_slice) != edited_fingerprint` | Séquence brute **non patchée**, overrides conservés tels quels, avertissement persistant |

### Transitions exhaustives

1. `Clean → ManuallyEdited` : première édition d'un point de cet objet. La
   commande capture `edited_fingerprint = fingerprint(raw_slice_actuel)`
   avant d'ajouter l'entrée à `overrides`.
2. `ManuallyEdited → ManuallyEdited` : nouvelle édition sur le même objet,
   tant que `fingerprint` n'a pas changé entre-temps (ajout/mise à jour d'une
   entrée de `overrides`, `edited_fingerprint` inchangé).
3. `ManuallyEdited → Clean` : action explicite « Abandonner les retouches »
   (annulable). `overrides.clear()`, `edited_fingerprint = 0`.
4. `ManuallyEdited → Dirty` : **détecté**, pas déclenché par une commande
   dédiée — se produit dès qu'une régénération (après n'importe quelle
   commande, sur cet objet ou un autre) constate `fingerprint(raw_slice) !=
   edited_fingerprint`. Aucune donnée n'est perdue : `overrides` reste intact,
   seulement non appliqué tant que non résolu.
5. `Dirty → Clean` : action explicite « Abandonner les retouches » (même
   commande qu'en 3 ; toujours proposée depuis Dirty).
6. `Dirty → ManuallyEdited` : action explicite « Considérer comme la nouvelle
   référence » — `edited_fingerprint` est recalculé sur le `raw_slice` actuel
   ; les `base_index` des overrides restent tels quels (ils ne sont
   **réinterprétés** contre la nouvelle vue que si l'utilisateur les
   ré-applique un par un — voir *Décisions ouvertes*, aucune tentative de
   « recalage automatique par proximité » n'est proposée en MVP : un recalage
   géométrique heuristique pourrait replacer silencieusement une retouche au
   mauvais endroit, ce qui est justement le risque à éviter).
7. `Dirty` sans action : reste `Dirty` indéfiniment ; c'est un état stable et
   sûr, pas une erreur bloquante.
8. `Clean` face à un changement de géométrie/paramètres : **inchangé**,
   comportement actuel (régénération pure, aucune notion d'override en jeu).
9. Suppression de l'objet : ses overrides disparaissent avec lui, aucun état
   orphelin possible (pas de table d'overrides séparée du document).
10. Undo/redo de n'importe quelle commande d'édition manuelle : restaure
    exactement `overrides` et `edited_fingerprint` (même discipline que
    `SetStitchTypeCommand`/`SetStitchParamsCommand` existants, qui mémorisent
    déjà l'état précédent complet pour un retour exact).

### Pourquoi aucune perte silencieuse

Le seul moment où `overrides` est vidé est une action **explicite** (transition
3/5), toujours annulable via l'`UndoStack`. Un changement de géométrie ou de
paramètres, ailleurs ou sur l'objet lui-même, ne fait jamais que **désactiver
temporairement** l'application des retouches (`Dirty`) — jamais les effacer.

---

## 2. Opérations MVP

Restreintes à la passe `StitchPass::TopStitch` (la couche « dessinée » par
l'utilisateur) : les passes `Underlay`/`Travel`/`Lock` restent entièrement
régénérées, non éditables en Lot 8. Justification : ce sont des points
intermédiaires synthétiques (sous-couches, trajets cachés, fixations) dont la
sémantique dépend étroitement de l'algorithme qui les a produits ; les
exposer à l'édition démultiplierait les cas particuliers sans bénéfice net
pour un MVP. À reconsidérer plus tard si le besoin est confirmé.

1. **Déplacer un point** (`moved_to`) : uniquement des commandes `Stitch`. Un
   avertissement (réutilise `stitch_generation::WarningCode::StitchTooLong` /
   `StitchTooShort`, déjà défini dans `running_stitch.hpp`) s'affiche si le
   déplacement crée un segment anormalement long/court vis-à-vis des voisins
   — **non bloquant** (l'utilisateur peut vouloir un point isolé volontaire).
2. **Convertir Stitch ↔ Jump** (`forced_type`) : seule transition permise en
   MVP (pas de conversion vers `Trim`/`ColorChange`/`Stop`, qui ont une
   sémantique machine distincte).
3. **Ajouter/supprimer un Trim** (`trim_after`) : insère/retire un `Trim`
   après un point donné. N'ajoute ni ne supprime de point de couture — c'est
   une commande machine supplémentaire, pas une retouche de géométrie.

### Sélection et édition multi-points

**Non justifiée en MVP.** Seule l'opération « déplacer » se prêterait
naturellement à un déplacement groupé (delta commun sur une sélection), mais
elle ajoute une combinatoire de tests (overrides multiples dans une seule
commande, undo atomique du lot) sans qu'aucun des trois usages MVP ne
l'exige structurellement. Reportée à un sous-lot ultérieur (§10, 8.4) si le
besoin est confirmé à l'usage.

---

## 3. Commandes undo/redo

Trois commandes, même discipline que `SetStitchParamsCommand` (mémorisation
de l'état précédent complet pour un retour exact) :

```cpp
class MoveStitchPointCommand final : public ICommand {
    // ctor(ObjectId, std::size_t base_index, Vec2um new_pos)
    // apply()  : si overrides vide -> capture edited_fingerprint (transition 1) ;
    //            mémorise l'ancienne valeur de l'entrée (ou son absence) ; pose moved_to.
    // revert() : restaure l'entrée précédente ; si c'était la dernière entrée
    //            créée par cette commande (transition 1 inverse), remet
    //            overrides vide et edited_fingerprint à 0.
};

class SetStitchPointTypeCommand final : public ICommand { /* même schéma, forced_type */ };
class SetStitchTrimCommand final : public ICommand { /* même schéma, trim_after (bool) */ };

// Résolution explicite d'un état Dirty — pas de commande "automatique" :
class DiscardOverridesCommand final : public ICommand {
    // apply()  : sauvegarde overrides + edited_fingerprint, les vide.
    // revert() : les restaure tels quels.
};
class RebaselineOverridesCommand final : public ICommand {
    // apply()  : sauvegarde l'ancien edited_fingerprint, le recalcule sur raw_slice actuel.
    // revert() : restaure l'ancien edited_fingerprint.
};
```

**Invariant commun** (comme pour toute commande existante, ADR-010) : `apply`
puis `revert` ramènent le document **exactement** à l'état antérieur —
vérifié par des tests dédiés (§8). Aucune de ces commandes ne touche à
`generate_sequence` : elles ne mutent que `EmbroideryObject::overrides` /
`edited_fingerprint`.

---

## 4. Persistance `.osp`

### Rétrocompatibilité

Suivant la convention déjà en usage pour les champs des Lots 3–7 (finitions
satin, sous-couches tatami, etc.) : `overrides` et `edited_fingerprint` sont
des champs **optionnels** de `embroideryObjects[i]` dans `project.json`. Un
fichier sans ces clés se charge avec `overrides = []` (état `Clean`), donc
sans bascule de `schemaVersion` requise pour rester compatible avec la
convention existante.

```json
"overrides": [
  { "index": 42, "pos": { "x": 12000, "y": -4300 } },
  { "index": 57, "type": "jump" },
  { "index": 57, "trimAfter": true }
],
"editedFingerprint": 9814772034551998211
```

### Ouvert : faut-il quand même bumper `schemaVersion` (2 → 3) ?

Les Lots 3–7 n'ont bumpé la version qu'une fois (v1→v2, cadre + barreaux
satin), tous les champs suivants sont restés « optionnels sous v2 ». Ce lot
introduit cependant une **sémantique** nouvelle (des points ne sont plus
*purement* dérivés), ce qui est un changement plus structurant qu'un champ de
finition. Recommandation : bumper à **v3** malgré la compatibilité technique,
pour que la version documente honnêtement « ce fichier peut contenir des
retouches manuelles » — à trancher avec l'utilisateur (voir *Décisions
ouvertes*), car cela reste un choix éditorial, pas une nécessité technique.

### DST importé comme vérité

Non couvert par le modèle ci-dessus : une séquence DST importée n'a **aucun**
`EmbroideryObject` auquel accrocher des overrides (constat n°3). Deux options,
à trancher séparément du reste du Lot 8 (périmètre distinct) :

- **A — Hors périmètre du Lot 8.** L'édition manuelle ne s'applique qu'aux
  objets générés depuis une image/vectorisation. Le DST importé reste, comme
  aujourd'hui, un état de session non persistable en `.osp`. Le bug latent
  (constat n°3 : perte silencieuse si l'utilisateur tente d'enregistrer)
  devrait alors être corrigé séparément — a minima en désactivant/expliquant
  l'action « Enregistrer » pour ce cas, plutôt que de laisser croire que rien
  n'est à sauvegarder.
- **B — Généraliser.** Ajouter `document::Project::imported_sequence :
  std::optional<stitch::StitchSequence>` (persisté dans `project.json`,
  effacé au chargement d'une image — même règle que la remise à zéro actuelle
  de `sequenceImported_`). Les mêmes primitives d'override s'appliquent alors
  directement dessus (`base_index` = position dans la séquence importée elle-
  même, `fingerprint` de la séquence importée — qui ne change jamais sauf
  nouvel import, donc l'état `Dirty` y est quasiment impossible en pratique).
  Corrige au passage le bug latent du constat n°3.

Recommandation : **B**, parce qu'il corrige un bug réel et unifie le modèle,
mais son coût dépasse le MVP strict (§2) — proposé comme sous-lot séparé
(§10, 8.5), pas comme prérequis bloquant des sous-lots 8.0–8.3.

---

## 5. Séparation cœur/Qt

Tout le mécanisme décrit en §1 est **entièrement dans le cœur**, sans état
caché ni cache de session :

- `libs/stitch_generation/include/.../overrides.hpp` (nouveau, ou ajouté à
  `generate.hpp`) :
  ```cpp
  [[nodiscard]] std::uint64_t fingerprint(const stitch::StitchSequence& raw_slice);
  [[nodiscard]] std::vector<stitch::StitchCommand>
      raw_slice(const stitch::StitchSequence& full, ObjectId object);
  // Applique les overrides valides (fingerprint à jour) ; les objets Dirty
  // restent tels quels dans `sequence`. Retourne les ObjectId Dirty détectés.
  [[nodiscard]] std::vector<ObjectId>
      apply_manual_overrides(stitch::StitchSequence& sequence, const document::Project& project);
  ```
  `generate_sequence` (inchangé) produit toujours la séquence brute pure ;
  `apply_manual_overrides` est un **second passage** séparé, appliqué par
  l'appelant. Aucun état caché : rejouable à l'identique par le desktop, la
  CLI, et les tests — **c'est la séquence qui fait foi pour analyse, export
  et simulation** (point 7), unique chemin, pas de divergence possible entre
  « ce qu'on voit » et « ce qu'on exporte ».
- `libs/commands/.../project_commands.hpp` : les cinq commandes du §3, aucune
  dépendance Qt.
- `libs/project_io` : sérialisation/désérialisation des deux nouveaux champs
  JSON — pur, déjà le cas pour tous les autres champs.
- **Qt (`apps/desktop`)** ne fait que : (a) construire les commandes à partir
  d'un geste utilisateur (glisser une poignée de point → `MoveStitchPointCommand`),
  (b) afficher l'état dérivé (`Clean`/`ManuallyEdited`/`Dirty`) calculé par le
  cœur, (c) proposer les actions de résolution (`Discard`/`Rebaseline`)
  quand Dirty. **Aucune règle métier dans les widgets** — la fonction
  `fingerprint`/`apply_manual_overrides` est la seule source de vérité sur
  l'état, jamais recalculée à la main côté UI.

### Architecture A vs B — cache de session

Deux architectures examinées pour « que montrer/exporter pour un objet
Dirty » :

- **A — cache de session à l'échelle de l'application.** Un objet
  `EditSessionCache` (hors document, à côté de l'`UndoStack`) mémorise la
  dernière séquence patchée avec succès par objet, et la sert tant que
  l'utilisateur n'a pas résolu l'état Dirty (l'objet garde visuellement
  « l'ancien dessin retouché » au lieu de basculer brutalement en séquence
  brute). Plus riche visuellement, mais : état supplémentaire à maintenir
  hors du document, invisible à la CLI/aux tests headless (un premier appel
  sans historique n'a rien à servir), et il faut décider où il vit et
  comment il se réinitialise (nouveau chargement, undo…).
- **B — sans état, séquence brute en repli (retenue).** Un objet Dirty
  affiche/exporte simplement sa séquence **brute actuelle** (non patchée) ;
  les overrides restent stockés, inertes, avec un avertissement visible tant
  que non résolus. Entièrement rejouable en fonction pure à tout instant
  (cohérent avec le principe de régénération existant), identique en
  desktop/CLI/tests, aucun état supplémentaire à synchroniser.

**Recommandation : B.** Il est strictement plus simple, ne casse pas
l'invariant « tout est recalculable depuis le document » qui structure déjà
tout le reste du logiciel, et évite d'introduire une deuxième source de
vérité temporaire. Le confort visuel de A (ne pas voir le dessin « sauter »
pendant qu'on répare) reste une amélioration UX possible **plus tard**,
ajoutée uniquement côté application, sans remettre en cause le modèle cœur.

---

## 6. UX — inspirée de Hatch, sans en copier l'interface

- **Mode d'édition explicite.** Un bouton/outil dédié « Éditer les points »
  (barre d'outils ou barre contextuelle, cf. `docs/ui-redesign-specification.md`)
  bascule le canevas en affichage des poignées de points **générés** pour
  l'objet sélectionné — distinct du mode « nœuds vectoriels » existant (qui
  édite la géométrie source, pas les points cousus). Éviter de mélanger les
  deux : déplacer un nœud vectoriel régénère tout ; déplacer un point de
  couture crée une retouche locale. Les deux gestes doivent rester visuellement
  et conceptuellement distincts.
- **Avertissement de régénération.** À la première retouche d'un objet
  (transition `Clean → ManuallyEdited`), un message non bloquant explique :
  « Cet objet a des points retouchés à la main ; toute modification
  ultérieure de sa forme ou de ses paramètres devra être reconfirmée. » —
  une fois, pas à chaque édition suivante.
- **Indicateurs d'état.** Icône sur l'objet (liste Document + inspecteur) :
  aucune (Clean), crayon (ManuallyEdited), triangle d'avertissement (Dirty,
  avec info-bulle listant la cause si connue : « géométrie source modifiée »,
  « ordre de couture modifié », etc. — dérivable en comparant les catégories
  de cause possibles, pas la cause exacte qui n'est pas conservée).
- **Confirmation / non-destruction.** `DiscardOverridesCommand` passe par une
  boîte de confirmation explicite (« Abandonner définitivement les retouches
  de cet objet ? », annulable via Ctrl+Z même après confirmation — cohérent
  avec le reste du logiciel où toute mutation passe par l'`UndoStack`).
  Jamais de suppression automatique en arrière-plan.

---

## 7. Analyse / export / simulation — quelle séquence fait foi

Chaîne unique, identique pour tous les consommateurs (desktop, CLI, tests) :

```
generate_sequence(project)            // pur, existant, inchangé
    -> apply_manual_overrides(seq, project)   // pur, nouveau (§5)
    -> seq                                    // AUTHENTIQUE pour analyse/export/simulation
```

Aucune divergence possible entre l'aperçu affiché et ce qui est exporté :
c'est la même séquence, calculée par le même appel.

### Validation points longs/courts

Réutilise la taxonomie existante (`WarningCode::StitchTooLong/StitchTooShort`,
`running_stitch.hpp`) au moment de l'édition (non bloquant, §2). Aucune
duplication de logique de normalisation : `encode_dst` (`libs/formats/src/dst.cpp`)
scinde déjà tout déplacement dépassant le pas machine (±12,1 mm par
enregistrement DST) en plusieurs enregistrements, **indépendamment de
l'origine** du point (généré, retouché, importé) — ce mécanisme n'a besoin
d'aucune modification, la retouche produit une position comme une autre en
sortie de `apply_manual_overrides`.

---

## 8. Tests

- **Unitaires (`libs/stitch_generation`)** : `fingerprint` stable si
  `raw_slice` identique, change si un seul point diffère (position **ou**
  type **ou** ordre) ; `raw_slice` correcte sur séquence contiguë **et**
  entrelacée (satin routé — cas concret : deux colonnes de même couleur,
  vérifier que la vue par `source` reconstitue chaque colonne indépendamment
  du trajet caché intercalé) ; `apply_manual_overrides` patch correctement
  move/type/trim par index, laisse un objet Dirty inchangé, ne touche jamais
  aux autres objets.
- **Unitaires (`libs/commands`)** : chaque commande, `apply`/`revert` exact
  (position/overrides/`edited_fingerprint` avant/après identiques à l'état
  initial) ; transition 1 (Clean→ManuallyEdited) et son annulation exacte
  (retour à `overrides` vide) ; `DiscardOverridesCommand`/
  `RebaselineOverridesCommand` aller-retour.
- **Intégration** : scénario « éditer un point → modifier un paramètre d'un
  **autre** objet → l'objet retouché reste `ManuallyEdited` (fingerprint
  inchangé) » ; scénario « éditer un point d'une colonne satin routée →
  changer la couleur d'une colonne voisine du même groupe → l'objet retouché
  passe `Dirty` » (vérifie que le mécanisme capture bien les effets de
  voisinage, cf. constat n°2) ; résolution Dirty dans les deux sens.
- **Round-trip `.osp`** (`tests/unit/project_io/test_roundtrip.cpp`) :
  `overrides` + `edited_fingerprint` survivent à save/load, y compris état
  Dirty (recalculé au chargement, pas stocké) ; fichier sans ces champs
  charge en `Clean`.
- **Non-perte** : suite de scénarios qui appliquent des séquences de
  commandes arbitraires (y compris réordre de couture, changement de type,
  suppression d'objets voisins) sur un objet retouché et vérifient à chaque
  étape : soit `overrides` est intact et appliqué (ManuallyEdited), soit
  intact et non appliqué (Dirty) — **jamais** vidé sans commande explicite de
  résolution.

### Critères d'acceptation mesurables

- 100 % des commandes du §3 : `apply` puis `revert` restaurent un état
  strictement égal (comparaison structurelle) à l'état antérieur.
- Aucune suite de commandes automatisée (fuzz sur permutations de commandes
  existantes + une des trois commandes d'édition) ne vide `overrides` en
  dehors d'un appel explicite à `DiscardOverridesCommand`.
- Round-trip `.osp` byte-exact sur `overrides`/`edited_fingerprint` (comme
  pour les autres champs, cf. `testing.md`).
- Régression zéro sur les 217 tests existants (aucune modification des
  générateurs `running`/`tatami`/`satin`/`routing`).

---

## 9. Complexité et mémoire sur motifs volumineux

- `overrides` : O(nombre d'éditions), pas O(nombre de points) — un motif de
  50 000 points avec 30 retouches ne stocke que 30 entrées.
- `fingerprint` : un hachage linéaire sur `raw_slice`, recalculé à **chaque**
  régénération globale, pour **chaque** objet ayant des overrides. Coût
  O(Σ tailles des slices retouchées) — négligeable devant le coût de
  génération lui-même (tatami/satin sont déjà O(points) ou pire). Pas de
  dégradation perceptible tant que le nombre d'objets retouchés reste
  raisonnable (dizaines) ; pas de garde-fou nécessaire en MVP.
- Aucun cache supplémentaire à maintenir (Architecture B, §5) : pas de risque
  de fuite mémoire de session liée à des objets Dirty jamais résolus — au
  pire, un triangle d'avertissement affiché indéfiniment, sans coût mémoire
  au-delà des `overrides` eux-mêmes.

---

## 10. Découpage en sous-lots

| Sous-lot | Contenu | Risque | Bénéfice | Décision utilisateur requise |
|---|---|---|---|---|
| **8.0** | `StitchOverride`, `fingerprint`, `raw_slice`, `apply_manual_overrides` — cœur pur, **aucune UI**, testé unitairement uniquement | Faible : aucun changement de comportement observable (overrides toujours vides en pratique) | Valide le mécanisme d'identité (le point le plus incertain du cadrage) avant tout investissement UI | Valider le choix « empreinte de sortie » du §1 avant de coder |
| **8.1** | Les 5 commandes (§3) + persistance `.osp` (§4, hors option DST-importé) + tests d'intégration/round-trip | Moyen : touche le format de fichier (même sans bump de version) | Le modèle est complet et testable en CLI/tests, sans dépendance Qt | Trancher schemaVersion 2 vs 3 (§4) |
| **8.2** | UI desktop minimale : mode édition, déplacement d'**un** point, indicateurs Clean/ManuallyEdited/Dirty, résolution Dirty (Discard/Rebaseline) | Élevé : premier contact utilisateur réel avec le concept, ergonomie à valider | Rend le Lot 8 réellement utilisable | Point de décision UX majeur : valider le mode d'édition dédié et le wording des avertissements avant généralisation |
| **8.3** | Stitch/Jump + Trim dans l'UI (réutilise 8.1/8.2) | Faible, une fois 8.2 acquis | Complète les 3 opérations MVP | — |
| **8.4** (optionnel) | Sélection/édition multi-points | Faible techniquement, mais scope creep possible | Confort si l'usage réel le demande | À ouvrir seulement si demandé après usage de 8.1–8.3 |
| **8.5** (scope séparé) | Généraliser aux séquences DST importées (option B, §4) + correction du bug de sauvegarde latent (constat n°3) | Moyen : touche `document::Project`, indépendant du reste | Corrige un bug réel, unifie le modèle | Décider si ce correctif est rattaché au Lot 8 ou traité comme correctif indépendant, avant ou après 8.0–8.3 |

---

## Décisions ouvertes à soumettre à l'utilisateur

1. Le mécanisme d'identité par **empreinte de la vue brute par objet**
   (`raw_slice` + `fingerprint`, §1) est-il validé comme fondation, avant tout
   codage ? C'est le choix le plus structurant de ce cadrage.
2. Le refus explicite de tout **recalage automatique** d'un objet Dirty (pas
   de remise en correspondance par proximité géométrique) est-il accepté,
   sachant qu'il impose à l'utilisateur de ré-éditer manuellement après un
   changement de forme important ?
3. `schemaVersion` : rester à 2 (convention actuelle, champs optionnels) ou
   bumper à 3 pour documenter le changement de nature du fichier (§4) ?
4. Le périmètre DST importé (option A « hors périmètre » vs B « généraliser
   et corriger le bug de sauvegarde », §4 et sous-lot 8.5) — et si B, quand :
   avant, après, ou en parallèle de 8.0–8.3 ?
5. Restriction MVP à la seule passe `TopStitch` (§2) : acceptée, ou faut-il
   dès le MVP couvrir aussi les points de sous-couche/lock générés par le
   Lot 4/5/7 ?
6. Édition multi-points (8.4) : à exclure du Lot 8 entièrement, ou à garder
   en sous-lot ouvert soumis à retour d'usage ?
7. Faut-il rédiger l'ADR-014 manquant (constat n°1) à l'occasion de ce lot,
   pour que les références existantes cessent de pointer vers un document
   inexistant ?

## Implémentation associée (une fois les décisions ci-dessus tranchées)

- `libs/document/include/openstitch/document/embroidery_object.hpp` —
  `StitchOverride`, champs `overrides`/`edited_fingerprint`.
- `libs/stitch_generation/include/.../generate.hpp` (+ nouveau fichier
  `overrides.hpp`) — `fingerprint`, `raw_slice`, `apply_manual_overrides`.
- `libs/commands/include/openstitch/commands/project_commands.hpp` — 5
  nouvelles commandes (§3).
- `libs/project_io` — sérialisation des deux champs (§4).
- `apps/desktop/main_window.cpp`, `canvas_view.cpp`, `properties_panel.cpp` —
  mode d'édition, poignées, indicateurs (§6), aucune logique métier au-delà
  de la construction des commandes.
