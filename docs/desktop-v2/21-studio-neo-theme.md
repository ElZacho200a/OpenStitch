# Neo Studio — thème par défaut (première passe)

> Contrat visuel pour **Qt Quick / GPU** (`apps/desktop_v2/studio/qml/`). Il ne crée pas de moteur de thèmes personnalisables.

## Principes
- Fond graphite / bleu nuit ; la broderie et les couleurs de fils restent prioritaires.
- Framboise pour sélection, action importante et focus ; accents secondaires cyan/violet très rares.
- Texte secondaire réellement lisible, contrastes contrôlés et états hover / focus / pressed / disabled perceptibles.
- Typographie : Segoe UI Variable sur Windows si disponible ; substitution système ailleurs. Échelle 11 / 12 / 13 / 16 / 20 / 22 px (caption, small, body, heading, brand, hero).
- Rythme : marges principales 18 px, contrôles 38 px, lignes d'objets 42 px, rayon 8 px ; préserver le réglage existant de densité.
- Messages : couleurs sémantiques `Theme.error`, `Theme.warning`, `Theme.success`, `Theme.info`, plutôt que des hexadécimaux dispersés.
- Transitions de couleurs 150 ms par défaut, désactivées via « Réduire les animations ».

## Source de vérité
`Theme.qml` fournit les tokens ; `StudioButton.qml` et `SectionLabel.qml` les consomment. Les couleurs documentaires et les champs HEX de l'utilisateur ne sont pas des couleurs de thème.

## Validation à effectuer
1. Ouvrir le shell et vérifier typographie, clavier et états de boutons ; contrôler clair / sombre, densité et mouvement réduit.
2. Ouvrir un vrai `.osp` et confirmer que canevas et couleurs de fils restent intacts.
3. Comparer captures GPU/CPU, largeur minimale et HiDPI lorsque l'environnement Windows/Qt est disponible.

**Hors périmètre :** galerie multithèmes, animations configurables, personnalisation, rendu GPU, moteur et workflow.


## D1 - Systeme de design et contrastes (garde-fous automatiques)

Constat (capture reelle, Release, theme sombre) : champs et cases blancs avec texte sombre,
etiquettes de cases illisibles, typographie heterogene, controles Qt Quick Controls par defaut.

### Composants (`apps/desktop_v2/studio/qml/`, a plat)
`StudioTextField`, `StudioCheckBox`, `StudioSwitch`, `StudioSpinBox`, `StudioComboBox`,
`StudioSlider`, `StudioLabel` (roles `caption`/`small`/`body`/`heading`), `StudioPanel`
(marges + titre), `StudioToolTip`. Tous habilles par `Theme` : etats normal / survol / focus
visible (anneau accent, 2 px) / desactive / erreur (`invalid`), `Accessible.*`, cible >= 24 px.
Ecriture d'un panneau : **ne jamais** instancier un `Controls.*` de saisie ; utiliser ces types.

### Defauts globaux
`Main.qml` pose la palette de la fenetre depuis `Theme` (`palette.window/base/text/button/...`,
verifie : `Window.palette` se propage aux controles en Qt 6.8.3) ; `main.cpp` pose la police
d'application (Segoe UI Variable / sans-serif, 12 px). `Window` n'a pas de propriete `font`.
Un controle oublie reste donc sombre et lisible.

### Garde-fous (`tests/unit/desktop_v2/test_studio_design.cpp`, cible `test_desktop_v2_studio_design`)
- contraste WCAG >= 4,5:1 pour foreground/muted/faint/accent/error/warning/success/info/
  secondaryAccent sur background/surface/panel/raised/hover/accentSoft, et accentInk sur
  accent/accentPressed, en sombre et en clair ;
- >= 3:1 pour `outline` (nouveau jeton, bordure des champs/cases/curseurs) sur
  background/surface/panel, et pour `accent` sur toutes les surfaces ;
- lint de `qml/**` : echec si controle Qt non habille, `Label` nu, `ToolTip` attache, couleur
  hexadecimale (sauf `Theme.qml` et lignes marquees `lint-ok:`) ou `font.pixelSize` numerique.
  Exceptions : `Theme.qml`, composants `Studio*` (seuls autorises a instancier les controles Qt).

### Jetons corriges (rapport de contraste avant -> apres)
| Jeton (mode) | Surface | Avant | Apres |
|---|---|---|---|
| faint sombre `#99a9be` -> `#a3b2c6` | hover | 4,29 | 4,77 |
| secondaryAccent sombre `#ad99ef` -> `#b7a6f2` | hover / accentSoft | 4,20 / 4,53 | 4,77 / 5,15 |
| faint clair `#5d6475` -> `#565d6e` | hover | 4,36 | 4,85 |
| info clair `#136f91` -> `#0f6585` | hover / accentSoft | 4,16 / 4,31 | 4,80 / 4,96 |
| secondaryAccent clair `#7054bb` -> `#6446b0` | hover / accentSoft | 4,20 / 4,35 | 5,07 / 5,25 |
| `outline` sombre (nouveau, `#6b7a94`) vs `border` `#445069` | panel / surface | 1,95 / 1,81 | 3,62 / 3,36 |
| `outline` clair (nouveau, `#7a8194`) vs `border` `#aeb4c2` | panel / surface | 2,08 / 1,74 | 3,89 / 3,26 |

`border` reste le trait decoratif ; les composants interactifs utilisent `outline`.
