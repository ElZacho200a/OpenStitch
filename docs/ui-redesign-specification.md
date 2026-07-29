# Refonte de l'interface — Phase 2 : spécification UX

Public : mainteneur, contributeur. Décrit l'interface **cible** d'OpenStitch
Studio. Fondée sur l'audit (`docs/ui-redesign-audit.md`) et sur le code métier
existant. Rien ici ne suppose une fonctionnalité métier absente ; les fonctions
partielles/expérimentales sont signalées comme telles.

Principe directeur : **le canevas domine**. Tous les panneaux sont
redimensionnables, repliables, masquables (`Tab`), et un **mode canevas** les
masque tous.

---

## 1. Structure générale de la fenêtre

```
┌───────────────────────────────────────────────────────────────────────────┐
│ Fichier  Édition  Image  Segmentation  Broderie  Affichage  Analyse  Aide  │  Menus
├───────────────────────────────────────────────────────────────────────────┤
│ [ouvrir img][ouvrir prj][enreg] │ [annuler][rétablir] │ [zoom-][fit][zoom+] │  Barre principale
│                                 │ [analyser][aperçu pts][exporter DST]       │
├───────────────────────────────────────────────────────────────────────────┤
│ Barre contextuelle : dépend de la sélection / de l'outil actif              │
├───┬───────────────────────────────────────────────────────────┬───────────┤
│ O │                                                           │ INSPECTEUR │
│ u │                                                           │ ─────────  │
│ t │                     CANEVAS                               │ Transform. │
│ i │        (image · régions · vecteurs · broderie ·           │ Apparence  │
│ l │         points · grille · règles · cadre ·                │ Type point │
│ s │         sélection · poignées · orientation · simu)        │ Paramètres │
│   │                                                           │ Généré     │
│ ▲ │                                                           │            │
├───┴───────────────────────────────────────────────────────────┴───────────┤
│ DOCUMENT (Objets · Régions · Ordre)         │  WORKFLOW (repliable)         │
├───────────────────────────────────────────────────────────────────────────┤
│ Simulation (zone réservée, activée si séquence)                             │
├───────────────────────────────────────────────────────────────────────────┤
│ x:12,4mm y:-3,1mm │ 220% │ Outil: Sélection │ Sel: Tatami «feuille» │ ● modifié │  Barre d'état
└───────────────────────────────────────────────────────────────────────────┘
```

Les panneaux sont des `QDockWidget` : Inspecteur (droite), Document (gauche),
Workflow (gauche, sous Document, repliable), Analyse (droite, onglet avec
Inspecteur **ou** dock séparé selon place). La barre de simulation occupe une
**zone réservée** en bas (elle se grise au lieu de disparaître → pas de saut de
mise en page, cf. audit P7).

**Adaptatif** : sous ~1280 px de large, l'Inspecteur et le Document deviennent
**onglets tabifiés** (`tabifyDockWidget`) plutôt que côte à côte. `Tab` masque
tous les docks (mode canevas). État des docks persisté (`QSettings`).

---

## 2. Barre de menus (réorganisation, sans casser les actions)

7 menus existants + **Aide**. Toutes les actions restent accessibles au menu,
même si dupliquées ailleurs. Les actions indisponibles sont **désactivées** avec
une infobulle explicative (jamais cachées).

- **Fichier** : Ouvrir une image… · Ouvrir un projet… · *(récents, si
  `QSettings`)* · — · Enregistrer le projet · Enregistrer sous… · — ·
  Importer un DST… · Exporter en DST… · — · Quitter.
- **Édition** : Annuler · Rétablir · — · *(à terme : préférences UI)*.
- **Image** : (inchangé, regroupé) Géométrie ▸ (symétries, rotations, recadrer) ·
  Correction ▸ (niveaux de gris, luminosité/contraste) · Nettoyage ▸ (débruitage) ·
  Couleurs ▸ (quantifier).
- **Segmentation** : Segmenter… · Fusionner (mode) · Supprimer la région ·
  Recolorer… · Vectoriser la région.
- **Broderie** : Numérisation automatique · — · Contour cousu · Remplissage
  tatami · Colonne satin *(Expérimental)* · — · Orientation du remplissage… ·
  Convertir les satins auto en tatami · — · Statistiques.
- **Affichage** : Calques ▸ · Filtres ▸ · — · Zoom +/−/Ajuster · Ajuster à la
  sélection · — · Panneaux ▸ (Inspecteur, Document, Workflow, Analyse,
  Simulation) · Masquer les panneaux (Tab) · — · Thème ▸ (Clair/Sombre) ·
  Densité ▸ (Confortable/Compact).
- **Analyse** : Analyser le motif (F5).
- **Aide** : Guide de démarrage · Raccourcis clavier · À propos.

Menu **contextuel** (clic droit sur objet), ordre imposé : action principale →
édition → conversion/type → visibilité/verrou → suppression.

---

## 3. Barre d'outils principale

Compacte, icônes **monochromes** (16 px), infobulle + `accessibleName`
obligatoires, texte sous l'icône seulement si ambigu. Groupes séparés par un
séparateur fin :

`Ouvrir image · Ouvrir projet · Enregistrer` ‖ `Annuler · Rétablir` ‖
`Zoom − · Ajuster · Zoom +` ‖ `Analyser · Aperçu points (bascule) · Exporter DST`

Pas de gros boutons colorés. « Nouveau » n'est **pas** ajouté (aucune notion de
document neuf autre que fermer/rouvrir ; ne pas simuler). Les actions suivent
`updateActions()` (désactivées hors contexte).

---

## 4. Barre d'outils contextuelle

Sous la barre principale, contenu **selon la sélection/outil**. Réglages **sûrs**
en ligne ; les opérations lourdes restent des dialogues. Elle **complète**
l'Inspecteur (accès rapide), sans le dupliquer entièrement.

| Contexte | Contenu |
|---|---|
| Aucun | dimensions motif, nb points, nb couleurs (lecture seule) |
| Image | taille physique (mm), accès rapide symétrie/rotation |
| Région | pastille couleur, aire mm², [Fusionner][Supprimer][Vectoriser] |
| Objet vectoriel | visibilité nœuds, nb nœuds, [Convertir en broderie] |
| Contour | longueur de point (mm), type (simple/double/triple) |
| Tatami | espacement, longueur, angle (spin + poignée), [compensation si exposée] |
| Satin *(Exp.)* | densité, compensation, sous-couche, largeur estimée + alerte |

Édition en ligne → **commande** à la validation/au blur (undo exact). Aperçu
temporaire pendant l'ajustement, commande au relâchement (comme la poignée
d'angle existante).

---

## 5. Palette d'outils verticale (modes d'interaction du canevas)

À gauche du canevas. **Uniquement des modes**, pas des actions. Outil actif
visuellement évident + raccourci + `accessibleName`. `ToolController` remplace
`cropMode_`/`mergeMode_`.

| Outil | Raccourci | Rôle |
|---|---|---|
| Sélection | `V` | sélectionner région/objet, défaut |
| Déplacer la vue | `H` / Espace maintenu | pan |
| Zoom | `Z` | zoom clic/glisser |
| Rectangle / Recadrage | `M` | sélection rectangulaire (recadrage image) |
| Sélection de région | `R` | pointer une région (segmentation) |
| Édition de nœuds | `N` | poignées de nœuds *(déplacement seul — partiel)* |
| Orientation | `O` | poignée de rotation du remplissage tatami |
| Mesure | `K` | **uniquement si** implémentable proprement (règle mm, vue seule) |

`Échap` = retour à **Sélection** et sortie de tout mode temporaire (fusion
comprise). L'outil actif s'affiche en barre d'état. « Exporter DST » **n'est pas**
ici (ce n'est pas un mode).

---

## 6. Inspecteur (panneau droit) — sections repliables

Contextuel à la sélection unifiée. Les valeurs **calculées** sont visuellement
distinctes (fond légèrement enfoncé, non éditable) des valeurs **modifiables**.
Les contrôles non pertinents pour le type **disparaissent** (pas juste grisés).

```
┌ INSPECTEUR ───────────────┐
│ ▸ Transformation & géom.   │  position, dimensions, aire, nb nœuds, verrou
│ ▾ Apparence                │  couleur (région/objet), fil provisoire, visibilité
│    ■ #5A7488   [Visible ☑] │
│ ▾ Type de broderie         │  [◻ Contour][▣ Tatami][◻ Satin]  (icône+libellé)
│ ▾ Paramètres de couture    │  selon type, unités explicites (mm, °, densité)
│    Espacement   0,40 mm    │
│    Longueur     3,0 mm     │
│    Angle        30 °  ⟳    │  (spin ↔ poignée synchronisés)
│ ▾ Généré (lecture seule)   │  nb points, dimensions, fil estimé, avertissements
│    Points 1 240 · 4,2 m    │
│    ⚠ Colonne large (11 mm) │
└───────────────────────────┘
```

- **Type de broderie** : trois boutons **icône technique + libellé** (jamais la
  couleur seule). Changement → `SetStitchTypeCommand` ; signale les paramètres
  réinitialisés ; annulable.
- **Paramètres** : édités via `SetStitchParamsCommand` (nouvelle commande
  générale ; voir plan). Unités toujours affichées. Bornes = celles du moteur,
  pas plus étroites arbitrairement ; valeurs recommandées en aide (infobulle).
- **Satin** : bandeau *Expérimental* discret + alerte largeur contextuelle
  (message précis, p. ex. « Colonne de 11,2 mm — au-delà de 9 mm recommandés »),
  proposition tatami, sans blocage de l'utilisateur avancé.
- **Tatami** : ne laisse **pas** croire aux sous-couches/underpath cachés/
  entrées-sorties (non implémentés).

---

## 7. Panneau Document (panneau gauche) — Objets · Régions · Ordre

Onglets (ou segments) : **Objets · Régions · Ordre de couture**. `QListView`/
`QTreeView` + **modèles Qt** + delegates (perf sur longues listes). Sélection
**bidirectionnelle** avec le canevas (`SelectionModel`).

- **Objets** : objets vectoriels et de broderie, avec type (icône+libellé),
  pastille couleur, visibilité (œil), verrou, avertissement éventuel. Clic droit =
  actions pertinentes.
- **Régions** : régions de segmentation (couleur, aire mm², nb pixels).
  Sélectionner → surligne sur le canevas.
- **Ordre de couture** (reprend `orderDock_`) : chaque ligne =
  `position · pastille · nom · type · [verrou] · [⚠] · [visibilité]`.
  Monter/descendre, glisser-déposer **si fiable**, verrou, stratégie, **coût
  avant/après** optimisation. Stratégies nommées + infobulle expliquant ce
  qu'elles optimisent (ordre du document / par couleur / par proximité / couleur
  puis proximité). Optimisation **annulable** (jamais « magique »).

Toute sélection panneau ↔ canevas est synchronisée dans les deux sens.

---

## 8. Workflow (indicateur d'étapes, repliable)

Bandeau discret (gauche, sous Document) : **Image · Régions · Vecteurs · Broderie
· Vérification · Export**. Chaque étape a un **état** rendu par icône **+**
libellé **+** couleur secondaire (jamais couleur seule) : non commencée /
disponible / en cours / terminée / attention.

L'état se **déduit** du document (image chargée ? segmentation ? objets ?
séquence ? analyse ?). Un clic met en avant les outils/panneaux de l'étape ;
**aucune** opération destructive automatique. Repère pédagogique pour débutant,
non contraignant pour l'avancé (ordre libre).

---

## 9. État d'accueil (canevas vide)

Au centre, zone **sobre** (pas d'emoji, pas d'illustration, pas de slogan, pas de
dégradé) :

```
        Aucun document ouvert

   [ Ouvrir une image ]   [ Ouvrir un projet ]   [ Importer un DST ]

   Importez une image pour commencer un nouveau motif,
   ou ouvrez un projet existant.

   Récents :  ▪ motif-fleur.osp   ▪ logo.osp        (si QSettings)
```

Les **récents** n'apparaissent **que si** la persistance `QSettings` est
implémentée (sinon, section absente — ne pas simuler). Widget dédié
`empty_state_widget`, masqué dès qu'un document existe.

---

## 10. Dialogues (uniformisation)

Tous : titre précis · courte explication si utile · labels alignés · unités ·
valeurs par défaut · **validation en ligne** · `QDialogButtonBox` (ordre système,
bouton principal évident) · taille minimale correcte. Réservés aux décisions
réelles ; **jamais** de chaîne de modales.

- **Import image** (refonte `ImportDialog`) : vignette d'aperçu · dimensions px ·
  format · transparence · largeur/hauteur mm · verrou proportions · **mm/pixel
  résultant** · taille du cadre · **alerte si l'image dépasse le cadre** · refus
  des valeurs incohérentes (validation en ligne).
- **Luminosité/contraste** : aperçu live (déjà) + valeurs affichées + réinitialiser.
- **Quantification** : nb couleurs + aperçu + phrase d'impact sur la segmentation
  + rappel non destructif.
- **Résumé pré-export DST** (nouveau) : dimensions · points · couleurs · sauts ·
  coupes · fil estimé · erreurs critiques · **dépassement de cadre** · destination.
  Rappelle que le DST **ne conserve pas** les objets. Propose d'enregistrer aussi
  le `.osp` si modifications non enregistrées. Bloque seulement l'impossible
  (séquence vide) ; confirme sur avertissement non critique.
- **Statistiques** : migrées vers l'Inspecteur / un panneau (plus de `QMessageBox`).

---

## 11. Navigation clavier

Ordre de tabulation logique (barre outils → panneaux → canevas). `Échap` sort
d'un mode ; `Entrée` valide ; `Espace` active boutons/cases ; flèches parcourent
les listes ; focus **restauré** après fermeture d'un dialogue. Raccourcis
existants **conservés** (Ctrl+O/S/Z/Y/Q, Suppr, Ctrl+±, Ctrl+0, F5). Ajouts sans
conflit Qt : `V/H/Z/M/R/N/O` (outils), `F` (ajuster à la sélection), `Tab`
(masquer panneaux, indiqué visuellement). Tous **visibles dans les menus** et
**centralisés** (table de raccourcis testable).

---

## 12. États de sélection & retours visuels (canevas)

- Sélection objet : contour **double-contour** (halo clair + trait accent sombre)
  → contraste garanti sur tout fond (cf. A6). Épaisseur cosmétique.
- Survol : léger surlignage (préparé pour `hover`).
- Poignées : représentation ~8 px mais **zone d'interaction élargie** (~14 px) ;
  états normal/survol/sélection/**focus clavier**.
- Distinction claire des couches : région source (aplat semi-opaque), contour
  vectoriel (trait), objet de broderie (points), points générés (couleur de fil).
- Orientation : axe + poignée contrastés, **angle affiché pendant le glisser**.

Retours d'action : de préférence **près de l'action** ou en barre d'état ; bandeau
non bloquant pour les confirmations légères ; modale réservée aux décisions.
Contrôle invalide mis en évidence sur place. Messages : *quoi · pourquoi ·
comment corriger* (ex. « La longueur de point doit être supérieure à 0 mm. »).

---

## 13. Calques & filtres (unifiés)

Regroupés (menu Affichage ▸ + accès rapide barre d'état/canevas).

- **Calques de représentation** : Image · Carte des régions · Vecteurs ·
  Broderie (points).
- **Filtres d'objets** : Contour · Tatami · Satin · Taille minimale · Couleurs de
  fil. Mention discrète permanente : « Les filtres n'affectent pas le fichier
  exporté. » Actions : Tout afficher · Tout masquer · Réinitialiser · Isoler la
  sélection (vue seule).

---

## 14. Analyse

Panneau (onglet avec Inspecteur ou dock dédié). Problèmes **groupés par gravité**
(Erreur / Avertissement / Information) rendus par **icône + libellé + couleur
secondaire** (jamais couleur seule, plus d'emoji). Chaque entrée : formulation
claire · objet concerné · valeur mesurée · valeur recommandée/raison · **Localiser**
· **Sélectionner l'objet**. Double-clic centre (déjà). *Ignorer pour la session*
**seulement si** le modèle le permet réellement (sinon absent). Résumé général :
points · sauts · coupes · changements de couleur · dimensions · fil estimé ·
hors-cadre · points trop courts/longs · sauts excessifs.

---

## 15. Simulation

Barre compacte en **zone réservée** (se grise si aucune séquence) : Début ·
Lecture/Pause · Fin · curseur (index) · point actuel / total · couleur/objet
courant. **Vitesse** seulement si ajoutée proprement (sinon absente — ne pas
simuler). Repère d'aiguille visible sans masquer le dessin ; sauts distinguables ;
partie cousue nette, partie à venir atténuée **si** la perf le permet ; fluide
au-delà de 4 000 points (respecter le gating `drawDots` existant).

---

## 16. Barre d'état

`x/y mm · zoom % · outil actif · sélection (type + nom) · dimensions motif · nb
points · état de sauvegarde (● modifié) · tâche en cours`. Concise. Messages
temporaires effacés après délai raisonnable, sauf attente d'interaction.

---

## 17. Prévention des erreurs & états désactivés

Unités visibles partout · bornes raisonnables (celles du moteur) · validation
immédiate · valeurs par défaut sûres · confirmation des actions destructives ·
undo · détection des modifications non enregistrées · cohérence des champs liés ·
**pas** de correction silencieuse. Une action indisponible est **désactivée** avec
infobulle explicative (« Importez une image… », « Sélectionnez une région »,
« Disponible pour un remplissage tatami », « Aucune séquence générée »).

---

## 18. Accessibilité (contrainte de conception)

- **Clavier** : toutes les fonctions principales sans souris ; focus visible
  **stylé** (pas seulement couleur) ; focus restauré après dialogue.
- **Contraste** : WCAG appliqué aux textes/séparateurs/désactivés/focus/sélection ;
  repères canevas en **double contraste**.
- **Couleur jamais seule** : gravité, type de point, sélection, calques, verrou,
  états workflow → icône/forme/libellé en plus.
- **Cibles** : boutons/poignées confortables (zone élargie).
- **Lecteurs d'écran** : `accessibleName`/`accessibleDescription` sur tout
  contrôle, relations label↔champ, champs annoncés avec nom/valeur/unité/état/
  erreur.
- **Mouvement réduit** : transitions brèves, aucune animation nécessaire à la
  compréhension.

---

## 19. Direction artistique, thème & tokens

Sobre, technique, précis ; moins austère que Qt par défaut, sans esthétique
« web » ni « IA générique ». Qualité par composition/alignement/espacements/
typographie/densité maîtrisée. **Aucune** texture de tissu, emoji, dégradé
décoratif, glassmorphism, glow, néon, cartes arrondies partout, gros rayons,
icônes multicolores, slogans. Un **seul accent** (un rappel « fil », p. ex. un
rouge-orangé sobre) ; couleurs d'état seulement quand elles signifient.

**Thème clair principal**, thème sombre (gris profonds, pas noir pur) **centralisé
uniquement**. Mêmes hiérarchies/dimensions/états dans les deux. Canevas
éventuellement thémé indépendamment.

**Design tokens** (centralisés dans `design_tokens.*`, consommés par
`app_theme.*`) :

```
color.window            color.surface           color.surface.raised
color.border            color.text              color.text.secondary
color.accent            color.accent.hover      color.selection
color.focus             color.success           color.warning
color.error             color.info
canvas.background       canvas.grid             canvas.hoop
canvas.jump             canvas.node             canvas.handle
canvas.selection.halo   canvas.selection.line
space.1..space.6        radius.sm/md            control.height.{compact,comfortable}
font.title/panel/section/label/value/secondary/help/error
icon.size.{16,20}
```

Une **seule** couleur d'accent. Les couleurs de fil appartiennent au **contenu**,
pas à l'identité de l'app. Densité : deux modes (**Confortable/Compact**) via les
tokens `control.height`/`space` — un seul point de vérité, pas de duplication.

Typographie : police système Windows (Segoe UI) / fallback redistribuable ;
hiérarchie simple (titre dialogue, titre panneau, section, label, valeur,
secondaire, aide, erreur) ; pas de titres géants, pas de gris trop clair, pas de
majuscules pour des paragraphes.

Icônes : jeu **monochrome** unique, licence compatible Apache-2.0 (vérifier
licence/poids/redistribution) ou dessin `QPainter`. Chaque icône seule : infobulle
+ nom accessible + état actif + état désactivé lisibles.

---

## 20. Petits écrans / portables

Sous ~1280 px : docks **tabifiés** (Inspecteur/Document), Workflow replié par
défaut, barre contextuelle prioritaire sur les docks. `Tab` = plein canevas. Les
tailles viennent des tokens (mode Compact conseillé). Aucune information vitale
hors écran ; scroll interne dans l'Inspecteur si nécessaire.

---

## 21. Parcours débutant vs avancé

- **Débutant** : état d'accueil → Workflow visible guide Image→…→Export ; barre
  contextuelle expose le bon réglage au bon moment ; infobulles ; messages
  *quoi/pourquoi/comment*. Les fonctions expérimentales (satin) ne sont **pas**
  mises en avant dans ce parcours.
- **Avancé** : menus complets + raccourcis outils + Inspecteur dense (mode
  Compact) + `Tab` plein canevas + accès direct à tous les paramètres et à
  l'ordre de couture ; jamais bridé sur les bornes du moteur.

---

## 22. Ce que la spécification **n'introduit pas** (honnêteté)

- Pas de palette de fils fabricant (non implémentée) — seulement RGB par objet.
- Pas de sous-couches/underpath tatami, ni d'entrées-sorties avancées.
- Pas de réglage de vitesse de simulation **sauf** ajout propre.
- Pas de sauvegarde automatique / récupération après crash (hors périmètre, sauf
  suivi « modifié » + garde à la fermeture).
- Pas de réactivation du satin auto naïf (désactivé par défaut, `use_naive_satin`).
- Pas d'édition de nœuds au-delà du déplacement (partiel) — aucune commande de
  géométrie factice.
- Pas de « Nouveau document » inventé si la notion n'existe pas côté métier.
