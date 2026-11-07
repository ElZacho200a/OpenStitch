# Fixtures OSP des schémas 1 à 9

Ces neuf archives ont été préparées une fois pour CX14, à partir de la structure de fixture
de `tests/unit/project_io/test_schema_migrations.cpp`. Elles ne prétendent pas avoir été
enregistrées par une ancienne application : ce sont des archives de compatibilité synthétiques,
conformes aux versions de schéma prises en charge par le lecteur.

Chacune contient un satin réel, avec deux rails séparés de 4 mm, un nom indiquant la version,
une couleur, une résolution physique et un compteur d'identifiants. Les versions 1 à 3 omettent
`maxWidthHard` ; les versions 4 à 9 incluent 30000 µm. La version 1 omet les barreaux. Les valeurs
par défaut de migration restent celles du lecteur de production.

Les archives ZIP ont une date interne fixe (2000-01-01), un JSON UTF-8 trié et aucune compression.
Le parcours 19 les lit, les ouvre dans la vraie QML, édite un rail et sauvegarde exclusivement
dans un répertoire temporaire. Il vérifie enfin que les octets du fichier source sont inchangés.
**Un test ne doit jamais réécrire ces fixtures.**
