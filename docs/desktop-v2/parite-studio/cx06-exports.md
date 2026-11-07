# CX06 — Exports avancés

L’export machine utilise ExportController, avec délégation depuis FileController et une
boîte QML dédiée. Les options du fichier comprennent l’optimisation des trajets et des
sous-couches, les coupes, changements de couleur et arrêts supportés par les codecs, les
fichiers compagnons CSV/INF et le DST compensé. Les contours de découpe d’appliqué utilisent
l’export SVG/DXF de project_ops. Les réglages de génération du document restent annulables.

Le contrôle du cadre, du champ machine et des limites du format précède l’écriture atomique.
Les calculs réutilisent export_sequence, le registre des codecs et le solveur de décalage des
couleurs, sur un instantané immuable via TaskManager. Aucune séquence n’est stockée dans l’OSP.
La compensation reste une estimation non calibrée à vérifier sur tissu.

Une demande d’export avec les réglages déjà actifs réussit sans ajouter d’étape d’historique.
Cette sémantique utilise le paramètre typé allowUnchanged, pas une comparaison fragile avec
le texte traduit d’une erreur. Les appels ordinaires de réglage conservent leur refus de no-op.

Test dédié Debug réussi : CTest **1/1**, **18,52 s**. Les vrais exports DST/PES/JEF/EXP/VP3
sont comparés aux codecs de production, avec routes, sous-couches, arrêts et fichiers de
couleurs. Autres contrôles : compensation, refus sans écrasement, annulation et OSP des
réglages, découpe d’appliqué et vrai changement de case QML après exposition de la fenêtre.
Les tests de fichiers attendent le même nom de motif que la production dans l’en-tête DST.
Les sélecteurs de fichiers natifs ne sont pas automatisés ; les exports passent par le contrôleur.

Suite Studio complète, tests cœur/commandes et Release restent à exécuter avant push/PR.
