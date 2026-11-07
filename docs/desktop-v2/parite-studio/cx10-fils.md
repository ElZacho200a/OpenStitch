# CX10 — Fils personnels et coloris

« Mes fils et coloris » ouvre le stock personnel, les nuanciers importés, la comparaison
CIEDE2000, le mélange en lumière linéaire et les aperçus des coloris du document.
Le stock est conservé dans `thread-stock.json` dans le profil. Les écritures utilisent
la primitive atomique commune du moteur ; un fichier corrompu bloque les changements
jusqu’à sa correction et son rechargement. Le retrait d’un nuancier conserve les références
possédées, avec un libellé explicite si leur nuancier est indisponible.

Les calculs réutilisent `thread_palette_service` et `libs/thread_palette`. L’aperçu CPU
s’exécute sur une copie immuable via TaskManager. Une modification du document ou une
annulation invalide le résultat en vol. Seule l’activation explicite du coloris modifie le
document, via la commande existante de DocumentSession, en une étape d’annulation.
Le mélange reste une estimation à vérifier sur tissu.

`test_desktop_v2_studio_threads2` prépare des vérifications sur la persistance, la corruption,
les références ΔE00 (noir/blanc et Sharma), les ratios invalides, les vraies saisies QML,
la possession, l’import CSV réel (appel contrôleur), le retrait de nuancier, les aperçus,
l’annulation et la sauvegarde/réouverture OSP avec comparaison des octets DST.

Validation : cible et QTest Debug réussis (1/1 CTest, 3,06 s). Les deux échecs QML initiaux provenaient de Accessible attaché à Popup au lieu de son Item de contenu ; le test refuse ces avertissements et passe après correction. Suite Studio complète, cœur et Release restent à exécuter avant push.
Les identifiants d’import/retrait sont transférés de famille D pour éviter les doublons.
