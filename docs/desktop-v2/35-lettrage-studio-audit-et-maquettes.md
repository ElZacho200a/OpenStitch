# Lettrage Studio : audit, maquettes et premier incrément (UX-TEXT-01)

Chantier Notion **UX-TEXT-01** (lots A et B, premier incrément). Retour utilisateur : le lettrage
Studio est très peu pratique. Cible : un novice saisit un prénom, déplace le texte et obtient une
broderie propre sans connaître les algorithmes.

## 1. Constat sur `main` (audit statique du code, 2026-11-24)

Nature de la preuve : lecture du code et du test existant. **Aucune session Studio n'a été chronométrée
pour cet audit** ; les nombres d'interactions ci-dessous sont comptés sur le code, pas mesurés à l'écran.

| Point | Constat | Source |
| --- | --- | --- |
| Outil « Texte » (T) | Déclaré dans le registre (`"text"`, raccourci T, espaces Conception + Numérisation) mais **jamais relié au canevas** : `ToolController` ne connaissait pas l'identifiant, donc `setActiveTool("text")` répondait « outil inconnu ou pas encore disponible ». | `core/tool_registry.cpp`, `studio/tool_controller.cpp` |
| Création | Uniquement par la palette de commandes (Ctrl+K → « Ajouter du texte… »), qui ouvre un dialogue généré à partir du catalogue : **17 champs** (texte, hauteur, remplissage, alignement, rotation, police en chemin de fichier, espacements, densité, largeur satin, base, rayon, connecteurs…). | `core/action_catalog.cpp` (`text.add`) |
| Position | Aucun champ de position : le texte naît en `[0, 0]` puis doit être déplacé. | `libs/project_ops/src/op_import.cpp` (`origin_mm`) |
| Édition | Palette → « Modifier le texte… », après avoir sélectionné **toutes** les lettres. Pas de réédition au clic ni au double-clic. | `studio/action_controller.cpp` |
| Police | Chemin de fichier saisi à la main (défaut `VeraBd.ttf`) ; pas d'aperçu, pas de liste. | `action_catalog.cpp` |
| Test existant | `textLetteringJourney` appelle `ActionController::run` directement : il prouve le service, **pas le parcours utilisateur**. | `tests/unit/desktop_v2/test_studio_digitize.cpp` |

Parcours actuel d'un prénom, compté sur le code : Ctrl+K, taper « texte », choisir la ligne, effacer
« OpenStitch », taper le prénom, valider, puis déplacer le texte à la main : **7 gestes ou plus**, un
dialogue modal de 17 champs, et aucune indication de l'endroit où le texte va apparaître.

## 2. Parcours cible et ce que livre ce premier incrément

1. **T** (bouton du rail Vecteurs, ou la touche T) puis un clic au canevas : une saisie s'ouvre à
   l'endroit cliqué, focus immédiat. *Livré.*
2. Taper (accents, espaces, retours à la ligne). **Ctrl+Entrée** ou « Valider » crée ; **Échap** ou
   « Annuler » abandonne ; **un clic ailleurs** valide si le texte n'est pas vide. *Livré.*
3. Le texte est centré sur le clic, une seule étape d'annulation, ses lettres sont sélectionnées.
   *Livré.*
4. Cliquer sur un texte existant avec l'outil T rouvre sa saisie, pré-remplie ; la validation conserve
   police, taille, alignement, position et réglages (`text.edit` avec les valeurs actuelles). Un texte
   existant ne devient jamais vide en silence. *Livré.*
5. Panneau typographique, poignées de redimensionnement et rotation, aperçu avant validation, avertissements,
   double-clic en Sélection, espace Points : **non livrés** (voir §5).

Aucun second document : la saisie ne porte qu'un brouillon (`TextTool` : position, texte, hauteur).
La création et la modification passent par les actions du catalogue `text.add` et `text.edit`, donc par
les commandes du moteur (`add_text` / `set_text`), l'`UndoStack`, l'OSP et le DST existants.

## 3. Maquettes basse fidélité

### Outil T actif, avant le clic

Une pastille d'aide en bas à gauche du canevas (visible dès que T est choisi, quel que soit l'onglet du
panneau de droite) : « Cliquer où placer le texte, puis taper. Ctrl+Entrée ou clic ailleurs : valider ;
Échap : annuler. Cliquer sur un texte : le modifier. »

### Création (clic au canevas)

```text
┌ rail ┐  ┌────────────────────────── canevas ─────────────────────────┐
│ ▭ ◯  │  │                      (texte centré sur le clic)           │
│ …    │  │                              ✛                            │
│ [T]◀ │  │          ┌──────────────────────────────────┐             │
│      │  │          │ Marie▏                           │ sous le clic│
│      │  │          │ Taille des majuscules [ 12 ▲▼] mm│             │
│      │  │          │          Annuler Échap  Valider ⏎│             │
│      │  │          └──────────────────────────────────┘             │
└──────┘  └───────────────────────────────────────────────────────────┘
```

La carte se pose **sous** le point cliqué (au-dessus s'il manque de place) : elle ne masque pas ce
qui va apparaître. Deux lignes de contrôles seulement, pour que « Valider » reste dans la bordure
même à 1000 × 640.

### Texte existant (outil T, clic sur le texte)

```text
 [École de Paris]          le texte reste visible ; la carte se pose SOUS lui
 ┌────────────────────────────────────┐   (coin bas-gauche), ses lettres sont
 │ École de Paris▏                    │   sélectionnées
 │            Annuler Échap  Valider  │
 └────────────────────────────────────┘
 refus affiché en rouge sous le champ (texte vide, police introuvable…)
```

Le fil d'un **nouveau** texte est un rouge chaud (`#D2483C`), lisible sur le fond sombre de l'espace
Vecteurs comme sur le fond clair de l'espace Points (contraste d'environ 4:1 et 3,7:1) ; le noir
d'`add_text` disparaissait sur le canevas Vecteurs. Aucun texte existant, ni la CLI/MCP, n'est modifié.

Le style suit Neo Studio : `Theme.raised`, bordure d'accent, `StudioSpinBox`, `StudioButton` ; aucun
contrôle Qt nu en dehors du `TextArea` thématisé du champ.

## 4. Cartographie technique

- `studio/text_tool.{hpp,cpp}` : état de la saisie ; recherche d'un texte sous le clic par l'emprise de
  ses lettres (`StudioSceneSnapshot::boundsOf`).
- `studio/tool_controller.*` : outil `text` (clic, annulation, indice) ; propriété `tools.text`.
- `qml/TextEditorOverlay.qml` : éditeur sur le canevas et pastille d'aide, accroché dans
  `qml/spaces/VectorSpace.qml`.
- `qml/Main.qml` : Échap abandonne d'abord la saisie ouverte.
- `core/action_catalog.cpp` : `text.add` accepte `origin_x_mm` / `origin_y_mm` (bornés à ±1000 mm) et
  `rgb` (`#RRGGBB`), hors dialogue.
- Test : `tests/unit/desktop_v2/test_studio_text_tool.cpp`, vraie fenêtre, vrais clics et frappes.

## 4 bis. Première revue UX indépendante (2026-11-25) : FAIL, corrigée

La revue de `os-ux-reviewer` sur les 20 captures a conclu FAIL (aucun P0, trois P1) : la carte de saisie
débordait (« Valider » hors bordure, rogné à 1000 × 640), le texte noir était quasi invisible sur le fond
Vecteurs, l'aide de l'outil restait cachée dans l'onglet « Édition ». Corrigé ici ; chaque point a un test
(`editorCardKeepsEveryControlInsideItsBorder`, `newTextIsReadableOnTheVectorCanvas`,
`helpPillShowsOnTheCanvasOnceTheTextToolIsChosen`) et une saisie avec de vraies touches vérifie qu'aucune
lettre ne déclenche un raccourci (`typingEveryLetterNeverFiresAShortcut`). Un défaut trouvé avant la
revue : à 1000 × 640 le bouton T, placé en dernier, sortait de la fenêtre ; il est remonté après les tracés.

Non retenus (écarts assumés ou reportés) : Entrée reste « nouvelle ligne » (parcours cible Notion) ; la
taille n'est modifiable qu'à la création ; texte qui dépasse du cadre sans avertissement (lot E) ; police
et fil non modifiables à la saisie (lot C) ; contour clair général des formes sombres du canevas Vecteurs ;
rail coupé en bas à 1000 × 640 (Lasso, Ajuster), à rattacher à THEME-019 / LAY-014 / LAY-019.

## 5. Reste à faire (hors de ce premier incrément)

- Lot A : mesurer le parcours actuel et le nouveau à l'écran (interactions, latence) sur une vraie session.
- Lot B : IME, copier/coller testés ; double-clic en Sélection ; outil dans l'espace Points ; ligne
  « Texte » dans la hiérarchie des calques ; police introuvable proposée en choix plutôt qu'en erreur.
- Lot C : panneau typographique (aperçu des polices, recherche, favoris, hauteur/largeur, interlettrage).
- Lot D : déplacement, rotation, redimensionnement directs ; mode lettre ; arc et cercle à la poignée.
- Lot E : aperçu fidèle pendant la saisie, avertissements de lisibilité, mesures p50/p95.
- Lot F : fixtures OSP/DST dédiées, parité CLI/MCP vérifiée, captures avant/après et revue UX
  indépendante (`os-ux-reviewer`).

**Aucune recette Studio réelle, aucun essai textile n'a été fait** : ce document ne valide rien.
