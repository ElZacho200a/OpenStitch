# 11 — Checklist de bascule V1 → V2

État au 2026-11-05. **La bascule n'est pas encore possible** : la V2 couvre le parcours « image → broderie →
export » avec un contrôle fin, mais plusieurs parcours critiques de la V1 n'ont pas d'équivalent. Cette liste
dit honnêtement ce qui bloque, ce qui est prêt et comment le vérifier.

## Prêt (preuve : tests + `09-matrice-finalisation.md`)

- [x] Importer une image avec réglages de segmentation en aperçu, numériser, retoucher, sauvegarder, exporter (DST + formats du registre).
- [x] Protection des documents (nouveau, ouvrir, quitter, récupération, chargements périmés ignorés).
- [x] Dessiner rectangle / ellipse / polygone, déplacer / insérer des nœuds, courber un segment, magnétisme au déplacement.
- [x] Colonne satin à deux rails : rails éditables au canevas, barreaux dessinés, largeur annoncée.
- [x] Fondu entre deux fils : création, axe manipulable (début, fin, déplacement, rotation).
- [x] Contrôles : diagnostic de brodabilité, carte de densité, risque de plissement du tissu (estimation non calibrée).
- [x] Optimisation de l'ordre avec mesures réelles avant → après, acceptation en une étape.
- [x] Compagnon IA via MCP : pont sur le document ouvert, approbation avec avant/après, historique unique.
- [x] Fils, ordre de couture, coloris ; écran d'accueil et parcours guidé.
- [x] Thème clair / sombre, retours discrets, mouvement réduit.

## Bloquant avant bascule (non fait)

- [x] Correction de segmentation APRÈS numérisation : outil « Régions » (X, menu Outils) : choisir une région au clic ou dans la liste, la supprimer, la fusionner dans une voisine, nettoyer les îlots, régénérer les objets (une étape d'annulation). Recoloration, fusion de couleurs, trous et retouche des frontières au pinceau (outil U) sont faits.
- [x] Plume, Crayon, suppression de nœuds, poignées de tangentes Bézier (contours et rails de satin).
- [x] Édition des barreaux des rails de satin (commandes) ; diagnostic des croisements de rails. Reste : glisser un barreau au canevas.
- [x] Comparaison nominal ↔ prédit de la compensation de tirage et assistant d'étalonnage sur pièce-test (commandes `analysis.pull`, `analysis.calibration`). Reste : adopter les coefficients ajustés dans le projet ; essai sur tissu réel.
- [x] Options machine avancées d'export (départ, coupes, points d'arrêt) et bibliothèque de cadres (commandes `export.options`, `export.check`). « Mes cadres » (enregistrer, appliquer, supprimer ses propres cadres) est fait.
- [ ] Segmentation IA / SAM (hors priorité produit ; état de disponibilité à afficher honnêtement).
- [x] Chat intégré (dock « Discussion IA », commande `chat.open`) ; le client MCP externe reste possible. Reste : essai réel avec une clé API, fournisseur « processus Claude Code », analyse sémantique automatique de la V1.
- [x] Nuanciers de fils et mélange (commande `thread.palette`), texte avancé et modification d'un texte (`text.add` étendu, `text.edit`), préférences (thème et densité d'interface, menu Affichage). Reste : choix de la langue (aucune infrastructure de traduction : un réglage de langue serait faux), texte sur chemin libre (`base: path`). L'édition lettre par lettre (`text.glyph`) est faite.

## À mesurer / vérifier à la main avant bascule

- [ ] Rendu visuel réel du thème, des poignées, des repères et de la boîte avant/après (jamais vérifié à l'écran : captures sans police).
- [ ] Performances en **Release** (les chiffres de `10-performances.md` sont en Debug sauf mention contraire).
- [ ] Installateur Windows : `openstitch_v2.exe` est inclus (`installer.iss`), à tester sur une machine propre.
- [ ] Un ancien `.osp` créé avec la V1 s'ouvre, se modifie et se réexporte à l'identique (test de non-régression au niveau octet à ajouter sur un corpus).

## Critère de bascule

La V1 peut être retirée du menu de démarrage quand toutes les cases « Bloquant » sont cochées OU explicitement
écartées par décision écrite, et que la recette manuelle des cinq scénarios (voir `06`) a été passée sur une
machine Windows réelle.
