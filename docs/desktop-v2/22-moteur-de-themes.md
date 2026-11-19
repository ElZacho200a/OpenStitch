# Moteur de thèmes du Studio

> Statut : livré (format 1, moteur niveau 1). Studio Qt Quick (`apps/desktop_v2/studio`).
> Thèmes livrés : **Neo Studio** (défaut, sombre et clair), **Neo Cobalt** (exemple d'héritage),
> **Bubblegum** (thème expérimental, test de résistance), **Marin** (nautique, deux variantes :
> palette dans `custom.*` par variante, `components.json` en simples renvois).

Un thème est un **dossier de données** (JSON, SVG, PNG, polices) chargé sans compilation. Le
moteur couvre couleurs, formes, typographie, effets (dégradés, ombres douces, reflets, relief,
textures), mouvements, icônes et styles de composants par variante et par état. Ajouter une
identité visuelle (rétro, minimaliste, futuriste…) ne demande aucune modification du code tant
qu'elle s'exprime avec les propriétés ci-dessous.

## 1. Audit de départ (2026-11-18)

| Constat | Conséquence |
|---|---|
| IHM entièrement Qt Quick (style `Basic`), aucun QSS : les Widgets V1 ne sont pas concernés. | Le moteur cible QML ; pas de feuille de style Qt à générer. |
| `Theme.qml` : singleton de ~50 jetons codés en dur (`light ? x : y`), utilisé par ~60 fichiers. | On garde la **même API publique** ; seules les valeurs changent de source. |
| Composants `Studio*` (bouton, champs, cases, curseur, popups…) déjà centralisés ; lint qui interdit hexadécimaux et tailles de police hors `Theme.qml`. | Migration ciblée sur ~15 fichiers ; le reste suit automatiquement. |
| Icônes : tracés SVG dans `StudioIcon.qml` (un `Shape`), aucune image. | Un thème peut remplacer un tracé (teinté) ou fournir un fichier. |
| Ombres de `StudioCard` : rectangles empilés (pas de shader), car les tests tournent en rendu logiciel. | Tous les effets restent en `Rectangle`/`Image` (voir §7). |
| Préférences : fichier `clé=valeur` (`core/preferences`), apparence système/clair/sombre, densité, mouvement réduit. | Le thème actif devient la clé `themeId` ; l'apparence choisit la variante. |
| Canevas GPU (`GpuCanvas`) indépendant du thème. | Changer de thème ne reconstruit aucune géométrie de points. |

## 2. Architecture

```
resources/themes/<id>/        données (manifest, jetons, composants, icônes, assets)
        │  lecture, validation, migration (sans Qt, testé en CI linux-core)
core/theme_schema    jetons connus, types, bornes, replis Neo Studio ; propriétés de style ; composants
core/theme_package   manifeste, fichiers, chemins sûrs, migration, héritage, références, résolution
core/theme_library   découverte, import (dossier / plan d'archive), dérivation, ThemeManager (activation + retour arrière)
        │  thème résolu (valeurs typées, chemins absolus)
studio/theme_controller   adaptateur Qt : QVariantMap pour QML, polices, .zip, surveillance du dossier, préférences
        │  propriétés tokens / styles / icons / meta (Main.qml → Binding)
qml/Theme.qml        façade inchangée (Theme.accent, Theme.radius…) + Theme.style(composant, variante, état)
qml/ThemedSurface    « ThemeRenderer » : traduit un style en Rectangle/Image (dégradé, ombre, reflet, relief, texture, focus)
qml/Studio*          composants consommateurs ; ThemeSection (préférences), ThemeGallery (galerie)
```

- **Séparation données / rendu** : le cœur ne connaît pas Qt ; `ThemedSurface` est le seul
  endroit qui sait dessiner un style. Un futur effet spécialisé (masque arrondi d'une texture,
  flou réel) s'ajoute là, derrière une propriété de style, sans toucher au format.
- **Repli à trois niveaux** : propriété absente d'un style → style intégré du composant (Neo) ;
  jeton absent → défaut du schéma ; thème introuvable ou cassé → thème courant conservé, au
  démarrage Neo Studio, et sans aucun fichier le thème **compilé** (`builtinFallback`).
- **Changement à chaud** : `ThemeController` remplace quatre `QVariantMap` ; les liaisons QML se
  réévaluent, aucun objet n'est recréé (vérifié par `test_desktop_v2_studio_theme`). Ni le
  document, ni la segmentation, ni les points ne sont recalculés : le contrôleur n'a aucune
  référence vers la session.
- **Déterminisme** : `std::map` partout, fichiers triés, archive exportée triée.

## 3. Format (schéma 1)

```
themes/<id>/
  manifest.json      requis
  tokens.json        jetons (familles imbriquées), variantes "dark" / "light"
  animations.json    même format que tokens.json (par convention : famille motion)
  components.json    styles de composants par variante et par état
  icons.json         tracés SVG nommés (données, jamais de script)
  assets/icons/      <nom>.svg|png : icônes fichiers (couleurs propres)
  assets/fonts/      .ttf/.otf déclarés dans manifest.fonts
  assets/textures/   images en mosaïque
  preview.png        aperçu facultatif (sinon mini-maquette dessinée avec les couleurs du thème)
```

### manifest.json

| Champ | Requis | Règle |
|---|---|---|
| `schema` | non | `"openstitch-theme"` s'il est présent |
| `schemaVersion` | oui | entier ; > 1 refusé (« mettez OpenStitch à jour ») ; 0 migré |
| `id` | oui | `[a-z0-9][a-z0-9-]{1,47}`, unique ; un thème importé ne masque jamais un thème livré |
| `name` | oui | ≤ 64 caractères |
| `description`, `author`, `version`, `license` | non | textes bornés |
| `parent` | non | identifiant du thème dont on hérite (≠ soi ; cycle, absence, profondeur > 8 = erreur) |
| `compatibility.minEngine` / `maxEngine` | non | niveau d'API du moteur (actuel : 1) |
| `variants` | non | `["dark"]`, `["light"]` ou les deux ; absent = hérité du parent, sinon sombre |
| `defaultVariant` | non | variante utilisée quand l'apparence demandée n'existe pas |
| `fonts` | non | chemins relatifs de polices à enregistrer |
| `preview` | non | image d'aperçu |

### Jetons (tokens.json, animations.json)

Familles imbriquées aplaties en `famille.nom`. Valeurs : nombre, texte, couleur
(`#rgb`, `#rrggbb`, `#rrggbbaa` — alpha en dernier — ou `transparent`), liste de familles de
polices, ou **référence** `"{famille.nom}"` vers un autre jeton (même type exigé, cycles
détectés). `custom.*` est libre : palettes du thème, réutilisées par références.

| Famille | Jetons |
|---|---|
| `color` | background, surface, panel, raised, hover, border, outline, divider, foreground, muted, faint, accent, accentPressed, accentSoft, accentInk, secondaryAccent, info, success, warning, error, focus (= accent par défaut), gloss, shadow, deskDot, guideCyan/Red/Green/Magenta |
| `font` | family, headingFamily (listes de familles par préférence), captionSize, smallSize, bodySize, headingSize, brandSize, heroSize, bodyWeight, strongWeight, headingWeight, letterSpacing, sectionLetterSpacing |
| `shape` | radius, cardRadius, borderWidth, focusWidth (≥ 2 : le focus reste visible) |
| `size` | controlHeight, rowHeight, railWidth, inspectorWidth |
| `space` | gap, panelPadding |
| `icon` | size, strokeWidth |
| `effect` | shadowLayers, shadowSpread, shadowOffset, shadowOpacity, deskStep, deskTexture, deskTextureOpacity |
| `motion` | durationMs, fastMs, slowMs, easing, easingEmphasis (courbes Qt : OutCubic, OutBack, OutElastic…), pressScale, hoverLift, popupScale |

La référence complète (valeurs Neo Studio) est `resources/themes/neo-studio/tokens.json` ; un
test vérifie qu'elle égale les défauts compilés de `theme_schema.cpp` et les replis de `Theme.qml`.
Variantes : `"variants": { "light": { … } }` dans tokens.json. Ordre de fusion : défauts du
schéma, puis de la racine à la feuille de l'héritage, pour chaque thème base puis variante.

### Styles de composants (components.json)

```json
{
  "button": {
    "normal":  { "fill": "#ffffff", "fillEnd": "{custom.lavender200}", "radius": 21, "gloss": 0.7 },
    "hover":   { "lift": 2 },
    "focus":   { "border": "{color.focus}", "borderWidth": 3 },
    "emphasized": { "normal": { "fill": "#e2569f", "fillEnd": "#a91d66", "text": "#ffffff" } }
  }
}
```

- **Composants** : `button` (variantes `emphasized`, `quiet`), `card`, `panel`, `popup`,
  `header`, `desk`, `input`, `checkbox`, `switch.track`, `switch.thumb`, `slider.track`,
  `slider.fill`, `slider.handle`, `tooltip`, `tab`, `listItem`.
- **États** : `normal`, `hover`, `pressed`, `selected`, `focus`, `disabled`. Propriétés posées
  directement sous le composant = `normal`. Fusion : défaut/normal ⊂ défaut/état ⊂
  variante/normal ⊂ variante/état. Le focus est appliqué **par-dessus** l'état : un thème ne
  peut pas le masquer, et une erreur de saisie (bordure d'alerte) l'emporte sur tout.
- **Propriétés** : `fill`, `fillEnd` (dégradé), `gradient` (`vertical`/`horizontal`), `border`,
  `borderWidth`, `radius`, `text`, `icon`, `fontFamily`, `fontWeight`, `opacity`, `height`,
  `paddingX`, `shadowColor`, `shadowLayers`, `shadowSpread`, `shadowOffset`, `shadowOpacity`,
  `gloss`, `glossHeight`, `innerHighlight`, `lift`, `scale`, `texture`, `textureOpacity`.
- Une propriété absente garde le style intégré : un thème qui ne définit que des couleurs reste
  complet et cohérent (exemple : Neo Cobalt, 8 jetons).

### Icônes

`icons.json` : `{ "save": "M5 3 L13 3 …" }` — commandes de tracé SVG seulement (caractères
contrôlés), teintées comme les icônes intégrées et dessinées avec `icon.strokeWidth`. Ou un
fichier `assets/icons/save.svg|png` (couleurs propres, non teinté). Absente : icône intégrée.

## 4. Sécurité

- Aucun code : seuls JSON, images, SVG et polices sont lus ; `.qml`, `.js`, exécutables… sont
  ignorés à l'import et jamais chargés.
- Chemins : relatifs, normalisés, refus de `..`, absolus, lettres de lecteur, `:` (flux NTFS),
  caractères de contrôle ; confinement vérifié **après** résolution des liens (`resolveInside`).
- Archives : plan d'extraction calculé par le cœur (`planArchiveExtraction`) : ≤ 512 entrées,
  ≤ 8 Mo par fichier, ≤ 64 Mo décompressés (taille réelle revérifiée à l'extraction), liens
  symboliques refusés, dossier racine unique retiré, `manifest.json` obligatoire.
- Import transactionnel : validation complète (héritage compris) avant écriture, copie dans un
  dossier temporaire puis renommage. Un thème livré ne peut être ni écrasé ni supprimé.
- SVG rendus par Qt SVG (pas de script). Les polices passent par `QFontDatabase`.

## 5. Interface

- **Préférences ▸ Thèmes** (`ThemeSection.qml`) : cartes des thèmes avec mini-maquette dessinée
  aux couleurs de chacun (ou `preview.png`), activation immédiate, détails et diagnostics,
  import d'un dossier ou d'une archive (avec confirmation de remplacement), export `.zip`,
  création d'un thème dérivé du thème sélectionné, « Rétablir Neo Studio », « Recharger »,
  ouverture du dossier des thèmes utilisateur (`%APPDATA%/OpenStitch Studio/themes`).
- **Galerie des composants** (`ThemeGallery.qml`, aussi `openstitch --theme-gallery`) : boutons
  des trois variantes dans les six états (état forcé), champs, cases, interrupteurs, curseurs,
  typographie, surfaces, onglets, lignes, les 48 icônes, et un tableau de contrastes mesurés en
  direct (texte ≥ 4,5:1), plus les diagnostics du thème.
- **Rechargement à chaud** : le dossier du thème actif est surveillé ; un fichier enregistré
  est relu en 250 ms. Un fichier cassé est refusé et le thème précédent reste affiché.
- Recettes : `openstitch --theme bubblegum` (essai sans toucher aux préférences),
  `--open-panels preferences`, `--theme-gallery`.

## 6. Accessibilité et performances

- `Réduire les animations` (préférence) **ou** le réglage Windows « Afficher les animations »
  (`SPI_GETCLIENTAREAANIMATION`) ramènent toutes les durées à 0, l'élévation à 0 et les
  échelles à 1.
- Contrastes : chaque thème livré, dans chaque variante, est vérifié en test (texte ≥ 4,5:1 sur
  six fonds, contours ≥ 3:1). Les thèmes importés reçoivent des **avertissements** de contraste
  (affichés dans les préférences et la galerie) sans être bloqués.
- Focus : anneau de largeur ≥ 2 px garanti par le schéma, toujours prioritaire sur l'état.
- Coût : un changement de thème = résolution C++ + réévaluation des liaisons, mesuré par
  `test_desktop_v2_studio_theme` (`switchingIsCheap`, quelques ms). Les couches d'effet absentes
  n'instancient rien (`Loader` inactif) : Neo Studio garde le même nombre d'éléments qu'avant
  pour ses boutons (pas d'ombre, pas de reflet). Le canevas GPU n'est pas concerné.
- DPI : tailles en pixels logiques Qt ; les SVG sont rastérisés au facteur d'échelle de l'écran.

## 7. Limites connues et suites

| Limite | Pourquoi | Alternative retenue / suite |
|---|---|---|
| Pas de flou gaussien réel pour les ombres. | `MultiEffect`/`ShaderEffect` exigent le rendu GPU ; les tests et `--software` utilisent le rendu logiciel, et un effet par bouton coûte une passe hors écran. | Ombre douce par couches translucides (`shadowLayers`, `shadowSpread`). Suite : propriété `shadowBlur` rendue par `MultiEffect` quand `GraphicsInfo.api` n'est pas logiciel, couches sinon. |
| Texture non découpée aux coins arrondis. | Un masque arrondi demande un shader. | Texture en retrait de 30 % du rayon ; idéale pour le plan de travail (`desk`). |
| Dégradés à deux arrêts, vertical ou horizontal. | `Gradient` QML a des arrêts statiques. | Suffisant pour bombé et brillant ; suite : `fillMid`. |
| La texture du plan de travail reste visible à travers le canevas (fond transparent). | Le canevas montre le plan de travail par conception. | Opacité faible dans Bubblegum (0,3). Suite : style `canvas` avec fond propre. |
| Tous les écrans ne passent pas par les composants `Studio*` (quelques `Rectangle` locaux : badges d'onglets, puces, règles). | Migration progressive. | Ils lisent les jetons de couleur et suivent donc tout thème ; les styles de forme ne s'y appliquent pas encore. |
| Variante unique par apparence (pas de « contraste élevé »). | Format 1. | Le format prévoit l'ajout d'autres clés de variante sans migration. |
| Pas d'éditeur graphique. | Hors lot. | Fondations : thème dérivé vide, rechargement à chaud, galerie, diagnostics typés, schéma introspectable (`tokenSchema()`, `stylePropertySchema()`, `componentSchema()` avec bornes et documentation). |
| Le changement de police modifie les largeurs : un thème à grosse police peut tasser l'en-tête aux petites largeurs. | Les paliers de l'en-tête sont en pixels. | Bubblegum garde la taille de corps de Neo (13 px) ; à vérifier sur les thèmes tiers via la galerie. |

## 8. Tests

- `test_desktop_v2_theme` (Catch2, sans Qt) : schéma trié, Neo fichiers = défauts compilés,
  thèmes livrés sans avertissement et lisibles, chargement valide, manifestes invalides,
  format plus récent / moteur incompatible, héritage partiel, variantes de styles, cycles et
  parents absents, références et valeurs fautives, chemins confinés (dont lien symbolique),
  archives dangereuses, import filtré et protégé, polices / icônes / assets manquants,
  gestionnaire qui garde le thème précédent, migration du format 0, persistance, déterminisme,
  replis de `Theme.qml`.
- `test_desktop_v2_studio_theme` (QTest hors écran) : changement à chaud sans recréer les
  composants, thème cassé refusé, thème futur listé non activable, persistance entre sessions,
  thème préféré disparu, apparence suivie, retour au défaut, export / import d'archive, document
  intact, coût d'un changement.
- `test_desktop_v2_studio_design` : lint QML (aucune couleur hexadécimale hors `Theme.qml`).
