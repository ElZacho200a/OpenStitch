# 14 — Dépendances entre représentations, parcours et validation visuelle

État au 2026-11-06 (tête de la branche de la PR #277 + lots de la session 3).

## 1. Les quatre représentations du document

| # | Représentation | Où elle vit | Ce qui la produit | Ce qui la rend obsolète |
|---|---|---|---|---|
| 1 | **Image source** | `project.source_image`, `project.processed` (image de travail) | ouverture d'une image, prétraitement (luminosité, CLAHE, recadrage…) | rien : elle ne dépend d'aucune autre ; un prétraitement ne touche pas ce qui est derrière |
| 2 | **Segmentation et régions** | `project.segmentation` (étiquettes, régions, couleurs) | segmentation de l'image (couleurs, plus petite région, lissage) ; **re-segmenter (F3)** | une autre image de travail, d'autres réglages de segmentation |
| 3 | **Objets** (vecteurs + broderie, paramètres) | `project.vector_objects`, `project.embroidery_objects` (types de points, `overrides` = retouches) | génération depuis les régions ; formes dessinées, SVG, DXF, texte | une correction de régions (le panneau Régions passe en « corrections en attente ») |
| 4 | **Points** | jamais stockés : `stitch_generation::effective_sequence(project)` | calculés à la demande depuis les objets | n'importe quel changement d'objet, de fil, de cadre ou de machine |

### Règle d'invalidation (ce qui a été mis en place)

- Une modification invalide **seulement ce qui en dépend** : changer la couleur d'un fil ne reconstruit ni les régions ni la géométrie
  (les points sont recalculés, pas stockés) ; recolorer une **région** ne change pas les objets tant que l'utilisateur n'a pas
  régénéré (le guide le dit : « les objets ne suivent plus la segmentation corrigée »).
- La **validité d'un contrôle ou d'un export est liée à la révision du document** (`analysedRevision_`, `machineCheckRevision_`,
  `exportedRevision_`). Un changement — même annulé ensuite — les rend `obsolètes` : une révision passée ne redevient pas valide.
- La **régénération ne remplace jamais en silence** :
  - les objets sans région d'origine (formes dessinées, SVG, DXF) sont **conservés** (`Regeneration::preserved`) ;
  - les objets issus de régions qui portent des retouches de points sont **comptés** (`lostRetouches`) et, avant le calcul, une
    confirmation annonce « N objet(s) portent des retouches qui seront remplacées » ;
  - tout s'applique en **une** étape d'annulation (`ReplaceProjectCommand`) : Ctrl+Z rend les retouches.
- Ce qui n'est **pas** fait : régénération sélective région par région (préserver aussi les objets retouchés issus de régions
  inchangées). La géométrie d'un objet retouché dépend de sa région ; la conserver demande de reconnaître « région inchangée »,
  non implémenté. Documenté comme écart.

## 2. Le parcours en quatre phases (guide)

Les phases sont des **contextes d'outils**, pas des écrans verrouillés. Le guide (`core/workflow_guide`) est une fonction pure de
l'état réel et n'interdit rien.

| Phase | Contenu | États possibles |
|---|---|---|
| **Préparer** | image, dimensions, palette, prétraitement, segmentation et correction des régions | non commencée, terminée |
| **Numériser** | objets, types de points, direction locale, sous-couches, compensation, régénération | non commencée, **obsolète** (segmentation corrigée non répercutée), terminée |
| **Contrôler** | diagnostic, simulation, densité, tirage, optimisation de l'ordre | non commencée, en cours, **obsolète**, **bloquée** (erreurs), terminée **avec avertissements**, terminée, **ignorée** (facultative, explicite) |
| **Produire** | machine, cadre, fils, contraintes physiques, contrôle machine, export, fiche | non commencée, en cours, **obsolète** (exporté puis modifié), **bloquée** (hors cadre, hors champ machine, format refusé), terminée |

Principes :
- Un **diagnostic sans erreur n'est pas une garantie machine** : la phase Produire exige son propre **contrôle machine** pour la
  révision courante (`export.check`, F9).
- Le bouton principal propose toujours l'action la plus pertinente : d'abord ce qui est obsolète ou bloquant, puis ce qui fait
  avancer. Chaque phase qui a une action de retour est **cliquable** dans le bandeau.
- Le contrôle (facultatif) peut être **ignoré explicitement** (palette : « Parcours : ignorer le contrôle du motif »).
- Changer d'espace **ne perd ni sélection, ni historique, ni préférences, ni focus** (test `switchingWorkspaces…`).

Les espaces (`Formes`, `Numérisation`, `Zones`, `Optimisation`, `Production`) filtrent outils et panneaux ; tout outil reste appelable
depuis la palette (Ctrl+K). Corriger une région depuis Production est possible : l'outil bascule seul vers Zones, l'état de correction
survit au retour.

## 3. Validation visuelle réelle (Windows, build Debug)

28 captures PNG obtenues avec le **vrai moteur de rendu Windows** (`openstitch_v2.exe --screenshot`, plateforme Qt « windows »), dans
`docs/desktop-v2/captures/` : `v2-<taille>-<thème>-<espace>.png` pour 1280×720, 1920×1080 et 1000×640, thèmes clair et sombre,
espaces Formes, Zones, Optimisation, Production, plus une vue à échelle 2 (1920×1080, écran dense). Journal des prises :
`captures/_captures.log`. Image de test : `tests/fixtures/tentabrode.png` à 80 mm, trois objets sélectionnés.

**Inspectées une à une (5 images)** : 1280×720 clair Formes, Zones ; 1280×720 sombre Optimisation ; 1000×640 clair Production ;
1920×1080 échelle 2 sombre Zones. Les 23 autres sont prises mais **non inspectées** ; ne pas en tirer de conclusion.

Défauts constatés puis corrigés (recontrôlés sur de nouvelles captures pour les trois premiers) :
1. icône du bouton « Tous les outils » invisible en thème clair (peinte à la couleur du thème de démarrage) ;
2. boutons fermer / détacher des panneaux quasi invisibles en thème sombre ;
3. texte du diagnostic coupé à droite, sans retour à la ligne ;
4. liste des régions réduite à deux lignes à 720 px de haut (hauteur minimale 150 px ajoutée) ;
5. bouton « Tous les outils » plus haut que les boutons d'espace (icône ramenée à 16 px).

Défauts vus et **non corrigés** :
- à 1000×640 le groupe « Réglages de couture » de l'inspecteur se chevauche (champ de recherche et « Réglages avancés ») ;
- à 1000 px de large la barre de simulation déborde derrière le menu de débordement de la barre d'outils ;
- en thème sombre, l'étape non commencée « 4. Produire » du guide est peu contrastée.

**Non validé** : build Release, écrans haute densité réels (seule l'échelle logique 2 a été émulée), machine de broderie physique,
thème « système » sur une autre machine, accessibilité aux lecteurs d'écran.
