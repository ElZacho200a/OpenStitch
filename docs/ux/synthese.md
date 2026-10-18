# Synthèse de l'audit UX/UI — 2026-10-18

Version courte du [rapport](audit-ux-ui-2026-10-18.md). Ce qui est **constaté** (code lu ou mesure)
est distingué de ce qui est **estimé** ; rien n'a été testé avec de vrais utilisateurs et
l'interface n'a pas été pilotée interactivement.

**Périmètre** : 4 analyses indépendantes en lecture seule, ≈ 60 constats (`PAR-`, `BRO-`, `QT-`),
état réel d'une trentaine de constats hérités (`UX-xx`), 1 mesure hors écran. Résultat : **30 nouvelles entrées
de roadmap** (HP-UX-017…044, HP-PERF-009, HP-QA-008), 7 entrées précisées. Aucun constat P0
nouveau : les P0 de l'audit du 13/10 sont corrigés (UX-02) ou portés par `HP-PERF-001` (UX-04).

## Les trois problèmes structurels

1. **Deux paradigmes de saisie qui se contredisent.** Des dialogues modaux créent les objets
   avec des valeurs en dur (tatami, contour, satin, numérisation, F7) pendant que l'inspecteur
   règle ensuite en direct. S'y ajoutent 4 modes d'édition et ~15 outils à activer par menu, une
   sélection qui obéit à trois règles, et Échap qui ne remonte pas d'un niveau. *Pistes :
   HP-UX-002/028 (création directe), HP-UX-018 (Échap), HP-UX-019 (un seul prédicat),
   HP-UX-001 (double-clic).*
2. **Une interface dimensionnée pour un grand écran.** **Mesuré** : minimum de fenêtre
   523 × 441 ; à 1024 × 640 le canevas ne mesure que 360 × 421 et la palette déborde. Cinq rangées
   de barres, cinq docks empilés, dialogues sans borne d'écran (estimés jusqu'à 900 px de haut),
   densité « confortable » même sur 768 px. *Pistes : HP-QA-008, HP-UX-024 à 027.*
3. **L'interface ne dit pas l'état ni la portée de ce qu'elle fait.** Retouches manuelles
   invalidées sans avertissement, valeurs « communes » qui sont celles d'un seul objet, workflow
   qui lance sans rendre compte, erreurs en barre d'état éphémère, régénération synchrone à
   chaque pas d'un champ, rendu réaliste muet après une simulation. *Pistes : HP-UX-029, 038, 039,
   HP-PERF-009, HP-UX-022.*

## Les dix améliorations au meilleur rapport impact/effort

| # | Amélioration | Tâche | Effort | Pourquoi |
|---|---|---|---|---|
| 1 | Échap remonte d'un niveau (jusqu'aux objets) | HP-UX-018 | S | geste le plus fréquent ; aujourd'hui ne désélectionne que les régions |
| 2 | Création directe, dialogues supprimés (option « avec réglages ») | HP-UX-002 / 028 | S-M | un clic et une modale de moins par objet, répété des dizaines de fois |
| 3 | Ctrl+O ouvre tout | HP-UX-017 | S | un `.osp` tapé dans Ctrl+O donne « Image illisible » |
| 4 | Un seul prédicat de sélection + tolérance de clic | HP-UX-019 | M | tracés ouverts et petits détails quasi inclicables ; Alt+clic peut ajouter un objet invisible |
| 5 | Docks en onglets, docks Fils et Couleurs défilants | HP-UX-024 | M | rend le canevas à un portable |
| 6 | Dialogues bornés à l'écran | HP-UX-025 | M | boutons OK/Annuler hors écran sur petit écran (estimé) |
| 7 | Sortie de simulation, rendu réaliste après lecture | HP-UX-022 | S | rendu réaliste muet sans explication |
| 8 | Avertissement avant d'invalider des retouches | HP-UX-029 | M | protège des heures de retouches manuelles |
| 9 | F7 sans modale (Maj+F7 pour régler) | HP-UX-021 | S | 3 gestes de moins à chaque vectorisation |
| 10 | Densité compacte auto + barres du bas fusionnées | HP-UX-026 | M | gain de hauteur immédiat sur écran 768 px |

## Décisions

### Déjà arbitrées (2026-10-18)

- **Verrou d'édition** : mode « Isoler » de session, **pas de champ `.osp`** (HP-UX-034).
- **Ctrl+O** : universel (HP-UX-017).
- **Dialogues de création** : supprimés, avec « Créer avec réglages… » (HP-UX-002).

### Restent à arbitrer

| Décision | Enjeu | Recommandation |
|---|---|---|
| Rendu réaliste après simulation : sortie simple, ou tracé progressif ? | la 2ᵉ est M et touche le rendu | commencer par la sortie (S) |
| Mode « Remodeler » unique : fusionner les modes ou seulement double-clic + Échap ? | refonte L contre geste S | double-clic + Échap d'abord, fusion seulement si insuffisant |
| Mode portable : automatique sous 1000 px ou sur action ? | surprise de l'utilisateur contre découverte | proposé automatiquement, jamais imposé |
| Import non proportionnel (HP-UX-044) : retirer le champ ou appliquer les deux axes ? | l'option anisotrope touche `libs/document` | retirer (état actuel documenté) sauf besoin métier |
| Réduction de couleurs à 5 endroits (PAR-16) : unifier ? | touche segmentation, fils, numérisation | cadrer avec `HP-THR-*` avant de planifier |

## Ordre de réalisation recommandé

1. **`HP-QA-008`** : mesurer les dialogues et poser la garde de taille minimale (rouge d'abord).
2. **Lots A et B en parallèle** (fichiers disjoints, pas de dépendance technique) : les gestes
   (Ctrl+O, Échap, sélection, F7, simulation) et le responsive (docks, dialogues, barres).
3. **Lot C** : création directe, type 1/N formes, retouches, valeurs mixtes, numérisation, qualité,
   export. Après `HP-PERF-001/009` ; les fichiers communs (`main_window.cpp`) gagnent à passer
   après `HP-QA-001`.
4. **Lot D** : Isoler, style, bascule de vue, liste Document, contrat d'état (le contrat IA est retiré de la roadmap).
5. **Lot E** : finitions.

## Posture critique sur ces propositions

- Les chiffres de **taille de dialogue sont des estimations** : à mesurer avant d'implémenter
  `HP-UX-025`. Si une taille estimée est fausse, la tâche se réduit au seul utilitaire de borne.
- Plusieurs recommandations **déplacent** de la complexité (création directe → inspecteur
  réactif ; fusion des listes → liste plus riche ; panneau Qualité → risque de « fourre-tout ») ;
  la stratégie les découpe en étapes et nomme la condition de chacune.
- Rien ne remplace un **test avec des brodeurs** (≈ 5 personnes, tâches réelles) pour valider
  les lots C et D ; il n'est pas planifié ici.

Détails : [rapport](audit-ux-ui-2026-10-18.md) · [parcours](parcours-utilisateur.md) ·
[stratégie](strategie-interface.md) · `docs/roadmap-parite-hatch.md` §20.
