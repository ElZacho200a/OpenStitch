# CX03 — Pinceau et pot de peinture

Le rail propose le pinceau de déformation (W) et le pot de peinture (Alt+G).
Ce raccourci évite le G de la grille, qui reste actif dans le même canevas.
Le pinceau agit sur UNE broderie liée à un contour vectoriel. Son rayon (0,5–60 mm)
et son intensité (5–100 %) se règlent dans les options ; Maj lisse, [ et ] changent le rayon.
Le pot remplit un vide fermé par les formes visibles avec la fermeture de 100 µm de V1.
Il crée uniquement une forme vectorielle, à convertir ensuite avec les outils de broderie.

Les services Qt-free réutilisent apply_brush_dab, make_brush_stroke_command et fill_gap_at.
Les calculs ont lieu sur un instantané immuable dans TaskManager. Au maximum un calcul de
pinceau est actif à la fois ; les événements reçus pendant le calcul sont regroupés pour
l’aperçu suivant. Le geste est borné à 4096 positions. Échap, le changement d’outil et les
révisions périmées annulent le résultat. Une seule commande est appliquée au relâchement.
Aucune géométrie ni séquence du document ne change pendant l’aperçu.

Tests préparés : Catch2 sur les bornes, la géométrie déterministe, les vides ouverts/occupés,
l’annulation et le refus des révisions périmées ; QTest sur le vrai shell QML/canevas, les
aperçus, Échap, la commande unique, la sauvegarde OSP et les octets DST.
Validation Debug/Release en attente ; aucune ligne TESTED.
