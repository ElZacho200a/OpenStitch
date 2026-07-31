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

1. **Correction (constat initial erroné) : ADR-014 existe bel et bien**, mais
   pas encore comme fichier séparé. `docs/phase0/08-roadmap-adr.md:43` le
   liste explicitement : « 014 \| États de génération
   Clean/Dirty/ManuallyEdited ; jamais d'écrasement silencieux de retouches \|
   Proposé ». C'est un statut **« Proposé »**, pas encore **« Accepté »**
   (cf. ADR-002 qui l'est), et aucun fichier `docs/adr/014-*.md` au format
   MADR n'a encore été rédigé — seule la ligne de synthèse existe. Les
   citations dans `docs/source/tatami.md:181` et
   `docs/stitch-engine-audit.md:80` (« les points sont une fonction pure de
   l'intention, jamais stockés ») en sont un corollaire, pas une
   contradiction : la régénération pure est le cas par défaut (`Clean`) du
   même modèle d'états que celui que ce cadrage détaille. **Autrement dit,
   Lot 8 est l'implémentation concrète d'ADR-014** — voir *Décisions
   ouvertes* pour la formalisation du fichier ADR et le passage de statut.
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
trajets cachés (satin routé). **Domaine indexé précis : `raw_slice` couvre
toutes les passes de l'objet (`Underlay`/`TopStitch`/`Travel`/`Lock`), pas
seulement `TopStitch`.** Bien que les retouches MVP ne portent que sur des
entrées `pass == StitchPass::TopStitch` (§2), `base_index` numérote sa
position dans cette vue **complète**, et l'empreinte est calculée sur la vue
**complète** également. Conséquence recherchée : toute évolution des
sous-couches ou des trajets cachés de l'objet (nombre de points d'underlay,
insertion d'un travel supplémentaire, etc.) modifie le contenu ou la longueur
de `raw_slice` **avant** les entrées `TopStitch` qui suivent, donc change
`fingerprint(raw_slice)` — la retouche passe `Dirty` au lieu de rester
silencieusement alignée sur un `base_index` qui désignerait désormais un
point différent. C'est une détection délibérément prudente (elle déclenche
aussi `Dirty` pour des changements d'underlay qui, par coïncidence, ne
décaleraient aucun point `TopStitch`), préférée à une indexation
`TopStitch`-only qui serait plus fine mais reposerait sur l'hypothèse
non vérifiée que rien en dehors du `TopStitch` ne peut influencer sa position
relative dans la séquence finale — hypothèse qu'aucune commande n'est en
mesure de garantir aujourd'hui (voir constat n°2, satin routé).

Une empreinte entière et déterministe (FNV-1a ou équivalent) est calculée sur
cette vue (positions en µm entiers + type de commande) : `fingerprint(raw_slice)`.
Comme les coordonnées sont des `int32`
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
    std::size_t base_index{};                  // index dans raw_slice(object), toutes passes
                                                 // confondues ; doit désigner une entrée
                                                 // pass == StitchPass::TopStitch en MVP (§2)
    std::optional<Vec2um> moved_to;             // nullopt = position générée
    std::optional<StitchPointType> forced_type; // nullopt = type généré
    bool trim_after{false};                     // insère un Trim juste après ce point
};

struct EmbroideryObject {
    // ... champs existants inchangés ...
    std::vector<StitchOverride> overrides;      // vide = comportement actuel (rétrocompatible)
    std::uint64_t edited_fingerprint{0};        // fingerprint(raw_slice) au moment de la dernière édition réussie
    std::uint32_t edited_point_count{0};        // raw_slice.size() au même moment — vérif rapide
                                                 // indépendante du hachage, voir §1 "collisions"
};
```

Pas de duplication de la séquence : les overrides sont des **deltas épars**
(quelques entrées, jamais O(nombre de points)). `edited_fingerprint` et
`edited_point_count` ne sont significatifs que si `overrides` n'est pas vide.

### États Clean / ManuallyEdited / Dirty — dérivés, pas stockés

L'état n'est **pas** un champ à synchroniser (source d'incohérence), il se
**calcule** à la demande :

| État | Condition | Comportement de génération |
|---|---|---|
| `Clean` | `overrides.empty()` | Séquence brute, inchangé (comportement actuel) |
| `ManuallyEdited` | `!overrides.empty() && raw_slice.size() == edited_point_count && fingerprint(raw_slice) == edited_fingerprint` | Séquence brute **patchée** par les overrides (positions, type, trims insérés) |
| `Dirty` | `!overrides.empty() && (raw_slice.size() != edited_point_count \|\| fingerprint(raw_slice) != edited_fingerprint)` | Séquence brute **non patchée**, overrides conservés tels quels, avertissement persistant |

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
   commande qu'en 3 ; toujours proposée depuis Dirty). **C'est en MVP la
   seule sortie de l'état `Dirty`** — voir ci-dessous pourquoi il n'existe
   volontairement **aucune** transition `Dirty → ManuallyEdited`.
6. `Dirty` sans action : reste `Dirty` indéfiniment ; c'est un état stable et
   sûr, pas une erreur bloquante.
7. `Clean` face à un changement de géométrie/paramètres : **inchangé**,
   comportement actuel (régénération pure, aucune notion d'override en jeu).
8. Suppression de l'objet : ses overrides disparaissent avec lui, aucun état
   orphelin possible (pas de table d'overrides séparée du document).
9. Undo/redo de n'importe quelle commande d'édition manuelle : restaure
   exactement `overrides` et `edited_fingerprint`/`edited_point_count` (même
   discipline que `SetStitchTypeCommand`/`SetStitchParamsCommand` existants,
   qui mémorisent déjà l'état précédent complet pour un retour exact).

### Pas de transition `Dirty → ManuallyEdited` : pourquoi

Une version antérieure de ce cadrage proposait une action « Considérer comme
la nouvelle référence » qui se contentait de recalculer `edited_fingerprint`
(et maintenant `edited_point_count`) sur le `raw_slice` actuel, en laissant
les `base_index` des overrides inchangés. **C'est incohérent avec le reste du
modèle et a été retiré** : dès la régénération suivante, l'état recalculé
serait `ManuallyEdited` (les compteurs correspondent de nouveau), donc
`apply_manual_overrides` réappliquerait automatiquement chaque override à
l'index `base_index` de la **nouvelle** `raw_slice` — qui, après le
changement ayant causé le passage à `Dirty`, ne désigne en général plus le
même point (positions décalées, points insérés/supprimés, satin réordonné).
Concrètement : un déplacement, une conversion de type ou un trim posés sur un
point précis pourraient se retrouver appliqués **silencieusement** à un autre
point, sans aucun avertissement — exactement la perte/altération silencieuse
que ce cadrage doit exclure (cf. ADR-014, constat n°1).

Deux façons saines de sortir de `Dirty` ont été considérées :

- **Abandon explicite, annulable** (transition 5, retenue pour le MVP) :
  `overrides.clear()`, l'utilisateur ré-édite au besoin sur la géométrie
  actuelle. Aucun remapping, aucune heuristique — le comportement est
  entièrement prévisible et déjà couvert par `DiscardOverridesCommand` (§3).
- **Réconciliation explicite point par point, avec aperçu avant validation**
  (non retenue pour le MVP, cf. *Décisions ouvertes*) : afficher à
  l'utilisateur, pour chaque override existant, sa position/valeur d'origine
  superposée à la `raw_slice` actuelle, et lui faire confirmer ou réassigner
  **individuellement** chaque retouche avant qu'elle redevienne active — donc
  sans jamais réactiver un `base_index` par un simple remplacement
  d'empreinte. Plus coûteux à concevoir et à tester (UI de comparaison,
  atomicité du lot de confirmations) qu'un gain net incertain avant retour
  d'usage réel.

**Recommandation : abandon explicite uniquement pour le MVP** (option la plus
simple et la plus sûre) ; la réconciliation point par point reste une piste
pour un sous-lot ultérieur si l'usage la justifie, mais n'est pas
prérequise et ne doit pas être confondue avec un « rebaseline » automatique.

### Pourquoi aucune perte silencieuse

Le seul moment où `overrides` est vidé est une action **explicite** (transition
3/5), toujours annulable via l'`UndoStack`. Un changement de géométrie ou de
paramètres, ailleurs ou sur l'objet lui-même, ne fait jamais que **désactiver
temporairement** l'application des retouches (`Dirty`) — jamais les effacer,
et jamais les réappliquer à un point différent sans confirmation explicite
(voir ci-dessus).

### Limite honnête : collision de l'empreinte 64 bits

Le titre ci-dessus ne doit pas être lu comme une garantie mathématique. La
détection `Clean`/`ManuallyEdited` vs `Dirty` repose sur une comparaison
d'empreinte 64 bits : deux `raw_slice` **différentes** qui produiraient la
même valeur de `fingerprint` (et, désormais, la même longueur —
`edited_point_count`) ne seraient pas distinguées, et une retouche resterait
appliquée à des points qui ont pourtant changé. C'est une collision de
hachage, dont la probabilité n'est jamais nulle pour un espace de sortie
fini. Ce risque n'est donc pas mathématiquement exclu — seulement rendu
négligeable en pratique :

- La probabilité de collision d'un bon hachage 64 bits, même sur les
  quelques dizaines/centaines de comparaisons que subit un objet retouché
  au cours d'une session, reste de l'ordre de 10⁻¹⁵–10⁻¹⁷ (borne
  anniversaire) — très inférieure à d'autres risques déjà acceptés ailleurs
  dans la chaîne (arrondi machine DST ±50 µm, corruption disque non
  détectée, etc.).
- **Protection proportionnée retenue** : `edited_point_count` (longueur de
  `raw_slice`) est stockée et comparée **avant** le hachage, en plus du
  `fingerprint`. C'est un contrôle indépendant du hachage — toute
  insertion/suppression de point (le cas le plus fréquent en pratique :
  ajout/retrait de points d'underlay, changement de densité tatami/satin,
  etc.) est détectée à coup sûr par ce seul compteur, sans dépendre de la
  qualité du hachage. Seule une modification qui **préserve exactement** le
  nombre de points repose sur le hachage seul.
- Coût de cette protection : 4 octets par objet retouché (`std::uint32_t`),
  négligeable devant les overrides eux-mêmes (§9) — pas de compromis mémoire
  réel à faire.
- Alternative examinée et écartée pour le MVP : stocker une signature plus
  forte (128/256 bits) ou la `raw_slice` complète pour comparaison exacte.
  Rejetée : coût mémoire proportionnel au nombre de points (contraire à
  l'objectif « deltas épars », §1) pour un gain de fiabilité qui, empreinte
  64 bits + longueur combinées, est déjà hors de proportion avec les autres
  sources d'erreur du logiciel. À reconsidérer seulement si un cas réel de
  collision est un jour observé.

En résumé : la détection est **fiable en pratique, pas garantie en théorie**
— ce cadrage préfère le dire explicitement plutôt que de promettre une
absence de perte silencieuse qu'aucune empreinte de taille finie ne peut
réellement garantir.

---

## 2. Opérations MVP

Restreintes à la passe `StitchPass::TopStitch` (la couche « dessinée » par
l'utilisateur) : les passes `Underlay`/`Travel`/`Lock` restent entièrement
régénérées, non éditables en Lot 8. Justification : ce sont des points
intermédiaires synthétiques (sous-couches, trajets cachés, fixations) dont la
sémantique dépend étroitement de l'algorithme qui les a produits ; les
exposer à l'édition démultiplierait les cas particuliers sans bénéfice net
pour un MVP. À reconsidérer plus tard si le besoin est confirmé. Cette
restriction porte sur les entrées **éligibles** à un override (`pass ==
TopStitch`), pas sur le domaine indexé par `base_index`/`fingerprint`, qui
couvre toujours l'objet entier toutes passes confondues (§1) — c'est ce qui
garantit qu'une évolution de l'`Underlay`/`Travel` de l'objet fait aussi
passer `Dirty` les retouches `TopStitch` qui suivent, au lieu de les laisser
pointer, sans le savoir, sur un index décalé.

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

// Résolution explicite d'un état Dirty — seule sortie de Dirty en MVP (§1) :
// pas de commande "rebaseline"/"considérer comme référence" qui réactiverait
// silencieusement d'anciens base_index contre une raw_slice différente.
class DiscardOverridesCommand final : public ICommand {
    // apply()  : sauvegarde overrides + edited_fingerprint + edited_point_count, les vide.
    // revert() : les restaure tels quels.
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
satin, sous-couches tatami, etc.) : `overrides`, `edited_fingerprint` et
`edited_point_count` sont des champs **optionnels** de `embroideryObjects[i]`
dans `project.json`. Un fichier sans ces clés se charge avec `overrides = []`
(état `Clean`), donc sans bascule de `schemaVersion` requise pour rester
compatible avec la convention existante.

```json
"overrides": [
  { "index": 42, "pos": { "x": 12000, "y": -4300 } },
  { "index": 57, "type": "jump" },
  { "index": 57, "trimAfter": true }
],
"editedFingerprint": 9814772034551998211,
"editedPointCount": 214
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
  // Applique les overrides valides (fingerprint + point_count à jour) ; les
  // objets Dirty restent tels quels dans `sequence`. Retourne les ObjectId
  // Dirty détectés. Bloc de construction interne — non appelé directement
  // par desktop/CLI/export (voir effective_sequence ci-dessous).
  [[nodiscard]] std::vector<ObjectId>
      apply_manual_overrides(stitch::StitchSequence& sequence, const document::Project& project);

  // Point d'entrée UNIQUE pour tout consommateur (desktop, CLI, export,
  // simulation, tests) : enchaîne generate_sequence + apply_manual_overrides.
  // Aucun appelant ne doit composer les deux passes lui-même — imposer cette
  // discipline à chaque site d'appel serait fragile (un seul oubli suffit à
  // faire fuiter la séquence brute non patchée vers un export). Signature
  // volontairement identique à l'actuel generate_sequence(project) pour que
  // les appelants existants n'aient qu'à substituer l'appel.
  [[nodiscard]] stitch::StitchSequence effective_sequence(const document::Project& project);
  ```
  `generate_sequence` (inchangé) produit toujours la séquence brute pure.
  `apply_manual_overrides` reste un **second passage** séparé et testable
  indépendamment (§8), mais `effective_sequence` est la **seule** fonction
  que desktop/CLI/export/simulation sont autorisés à appeler pour obtenir la
  séquence à afficher/exporter/simuler — remplace tous les appels actuels à
  `generate_sequence(project)` dans ces couches. Aucun état caché : rejouable
  à l'identique par le desktop, la CLI, et les tests — **c'est la séquence
  qui fait foi pour analyse, export et simulation** (point 7), unique chemin,
  pas de divergence possible entre « ce qu'on voit » et « ce qu'on exporte ».
- `libs/commands/.../project_commands.hpp` : les quatre commandes du §3, aucune
  dépendance Qt.
- `libs/project_io` : sérialisation/désérialisation des trois nouveaux champs
  JSON — pur, déjà le cas pour tous les autres champs.
- **Qt (`apps/desktop`)** ne fait que : (a) construire les commandes à partir
  d'un geste utilisateur (glisser une poignée de point → `MoveStitchPointCommand`),
  (b) afficher l'état dérivé (`Clean`/`ManuallyEdited`/`Dirty`) calculé par le
  cœur, (c) proposer l'action de résolution (`Discard`) quand Dirty, (d)
  appeler `effective_sequence(project)` — jamais `generate_sequence` seul —
  pour tout affichage/export. **Aucune règle métier dans les widgets** — la
  fonction `fingerprint`/`apply_manual_overrides`/`effective_sequence` est la
  seule source de vérité sur l'état, jamais recalculée à la main côté UI.

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
effective_sequence(project)           // point d'entrée unique (§5) :
    = apply_manual_overrides(generate_sequence(project), project)
                                       // AUTHENTIQUE pour analyse/export/simulation
```

`generate_sequence` et `apply_manual_overrides` restent des blocs de
construction internes, appelables séparément en test ; desktop, CLI, export
et simulation n'appellent, eux, que `effective_sequence`. Aucune divergence
possible entre l'aperçu affiché et ce qui est exporté : c'est la même
séquence, calculée par le même appel — et aucun site d'appel ne peut
« oublier » le second passage puisqu'il n'existe qu'un seul appel à faire.

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
  (position/overrides/`edited_fingerprint`/`edited_point_count` avant/après
  identiques à l'état initial) ; transition 1 (Clean→ManuallyEdited) et son
  annulation exacte (retour à `overrides` vide) ; `DiscardOverridesCommand`
  aller-retour ; **absence** de toute commande capable de faire redevenir
  `ManuallyEdited` un objet `Dirty` sans passer par une nouvelle édition
  explicite (test négatif garantissant qu'aucun ancien `base_index` ne se
  réactive par simple remplacement d'empreinte, cf. §1).
- **Intégration** : scénario « éditer un point → modifier un paramètre d'un
  **autre** objet → l'objet retouché reste `ManuallyEdited` (fingerprint
  inchangé) » ; scénario « éditer un point d'une colonne satin routée →
  changer la couleur d'une colonne voisine du même groupe → l'objet retouché
  passe `Dirty` » (vérifie que le mécanisme capture bien les effets de
  voisinage, cf. constat n°2) ; scénario « objet Dirty → Abandonner les
  retouches → ré-éditer sur la géométrie actuelle » (seul chemin de
  résolution testé, §1) ; scénario « objet Dirty non résolu → `edited_point_count`
  change seul (insertion/suppression d'un point de sous-couche) sans que
  `fingerprint` change par ailleurs → toujours détecté `Dirty` » (couvre la
  protection §1 indépendante du hachage).
- **Round-trip `.osp`** (`tests/unit/project_io/test_roundtrip.cpp`) :
  `overrides` + `edited_fingerprint` + `edited_point_count` survivent à
  save/load, y compris état Dirty (recalculé au chargement, pas stocké) ;
  fichier sans ces champs charge en `Clean`.
- **Non-perte** : suite de scénarios qui appliquent des séquences de
  commandes arbitraires (y compris réordre de couture, changement de type,
  suppression d'objets voisins) sur un objet retouché et vérifient à chaque
  étape : soit `overrides` est intact et appliqué (ManuallyEdited), soit
  intact et non appliqué (Dirty) — **jamais** vidé, et **jamais** réappliqué
  à un `base_index` différent de celui posé par l'utilisateur, sans commande
  explicite de résolution.
- **`effective_sequence`** (§5/§7) : `effective_sequence(project) ==
  apply_manual_overrides(generate_sequence(project), project)` sur un jeu de
  projets de test ; vérifie que desktop/CLI/export appellent bien cette seule
  fonction (revue de code / grep CI sur les appels directs à
  `generate_sequence` hors `libs/stitch_generation` et tests).

### Critères d'acceptation mesurables

- 100 % des commandes du §3 : `apply` puis `revert` restaurent un état
  strictement égal (comparaison structurelle) à l'état antérieur.
- Aucune suite de commandes automatisée (fuzz sur permutations de commandes
  existantes + une des trois commandes d'édition) ne vide `overrides` en
  dehors d'un appel explicite à `DiscardOverridesCommand`.
- Round-trip `.osp` byte-exact sur `overrides`/`edited_fingerprint`/
  `edited_point_count` (comme pour les autres champs, cf. `testing.md`).
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
| **8.1** | Les 4 commandes (§3) + persistance `.osp` (§4, hors option DST-importé) + `effective_sequence` (§5) + tests d'intégration/round-trip | Moyen : touche le format de fichier (même sans bump de version) | Le modèle est complet et testable en CLI/tests, sans dépendance Qt | Trancher schemaVersion 2 vs 3 (§4) |
| **8.2** | UI desktop minimale : mode édition, déplacement d'**un** point, indicateurs Clean/ManuallyEdited/Dirty, résolution Dirty (Discard uniquement, §1) | Élevé : premier contact utilisateur réel avec le concept, ergonomie à valider | Rend le Lot 8 réellement utilisable | Point de décision UX majeur : valider le mode d'édition dédié et le wording des avertissements avant généralisation |
| **8.3** | Stitch/Jump + Trim dans l'UI (réutilise 8.1/8.2) | Faible, une fois 8.2 acquis | Complète les 3 opérations MVP | — |
| **8.4** (optionnel) | Sélection/édition multi-points | Faible techniquement, mais scope creep possible | Confort si l'usage réel le demande | À ouvrir seulement si demandé après usage de 8.1–8.3 |
| **8.5** (scope séparé) | Généraliser aux séquences DST importées (option B, §4) + correction du bug de sauvegarde latent (constat n°3) | Moyen : touche `document::Project`, indépendant du reste | Corrige un bug réel, unifie le modèle | Décider si ce correctif est rattaché au Lot 8 ou traité comme correctif indépendant, avant ou après 8.0–8.3 |
| **8.6** (optionnel, non MVP) | Réconciliation Dirty point par point avec aperçu (§1, alternative écartée pour le MVP) | Élevé : UI de comparaison + atomicité du lot de confirmations, risque de scope creep | Évite de perdre les retouches en cas de changement de forme mineur | À n'ouvrir que si le simple « Abandonner + ré-éditer » (8.2) s'avère trop coûteux à l'usage |

---

## Décisions ouvertes à soumettre à l'utilisateur

1. Le mécanisme d'identité par **empreinte de la vue brute par objet**
   (`raw_slice` + `fingerprint`, §1) est-il validé comme fondation, avant tout
   codage ? C'est le choix le plus structurant de ce cadrage.
2. Ce cadrage **retient déjà** l'abandon explicite comme seule sortie de
   `Dirty` en MVP (§1) — aucune transition `Dirty → ManuallyEdited`, aucun
   recalage automatique ou par proximité géométrique, qui réappliquerait
   silencieusement d'anciens `base_index` à une `raw_slice` différente. Ce
   choix est-il accepté tel quel, sachant qu'il impose à l'utilisateur de
   ré-éditer manuellement après un changement de forme important ? Sinon,
   faut-il ouvrir dès maintenant le sous-lot 8.6 (réconciliation point par
   point avec aperçu, §1) plutôt que de le différer ?
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
7. ADR-014 est déjà annoncé (statut « Proposé ») dans
   `docs/phase0/08-roadmap-adr.md:43` (constat n°1) mais n'a pas de fichier
   `docs/adr/` dédié. Faut-il le rédiger formellement (format MADR) à
   l'occasion de ce lot et passer son statut à « Accepté » une fois le
   modèle §1 validé, ou différer cette formalisation ?

## Implémentation associée (une fois les décisions ci-dessus tranchées)

- `libs/document/include/openstitch/document/embroidery_object.hpp` —
  `StitchOverride`, champs `overrides`/`edited_fingerprint`/`edited_point_count`.
- `libs/stitch_generation/include/.../generate.hpp` (+ nouveau fichier
  `overrides.hpp`) — `fingerprint`, `raw_slice`, `apply_manual_overrides`,
  `effective_sequence` (§5).
- `libs/commands/include/openstitch/commands/project_commands.hpp` — 4
  nouvelles commandes (§3).
- `libs/project_io` — sérialisation des trois champs (§4).
- `apps/desktop/main_window.cpp`, `canvas_view.cpp`, `properties_panel.cpp` —
  mode d'édition, poignées, indicateurs (§6), aucune logique métier au-delà
  de la construction des commandes.
