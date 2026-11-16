# Gabarits de tâches pour Ollama (qwen3.5:9b)

Mode d'emploi : le **message système** ci-dessous (règles absolues, format de sortie, `BLOQUÉ`, `HYPOTHÈSES`)
est envoyé à chaque appel. Chaque gabarit numéroté est un **message utilisateur** à remplir (`<...>`), un par appel.

## Mise en place (faite sur la machine du propriétaire le 2026-11-10)

- **Modèle dérivé** `openstitch-coder` : `qwen3.5:9b` + `num_ctx 16384`, `temperature 0.1`, `seed 42`,
  `num_predict 6000` + le message système ci-dessous. Source : `docs/agents/Modelfile.openstitch-coder`.
  Recréer : `ollama create openstitch-coder -f docs/agents/Modelfile.openstitch-coder`, **puis**
  `ollama stop openstitch-coder` (sinon l'ancienne version reste chargée 30 min : piège réel rencontré).
- **Matériel** : RTX 5060 Ti 16 Go, modèle 100 % GPU (6,1 Go), ~60 tokens/s ; seulement ~3 Go de RAM libre
  quand le Studio tourne : ne pas monter `num_ctx` sans surveiller.
- **Boucle automatique** : `scripts/ollama/ask.ps1 -TaskFile <tâche> -Whitelist <fichiers> -Extract <dossier>`
  appelle le modèle, contrôle la réponse avec `scripts/ollama/check-draft.ps1` (liste blanche, HYPOTHÈSES,
  ASCII des TEST_CASE, doublon avec l'exemple, élisions, `M_PI`, `generate_sequence`), fait **un seul** retour,
  puis accepte (0), rejette (1) ou signale BLOQUÉ (2). Rien n'est écrit dans le dépôt.
- **Ce que le prompt seul n'obtient pas** (mesuré) : le modèle met des accents dans les noms de TEST_CASE malgré
  la règle, et recopie l'exemple ; il n'entoure pas toujours le code de balises. Ces points sont donc
  contrôlés par script, pas par confiance.
- **Limite des mesures** : `seed` fixe + `temperature` basse rendent les réponses quasi identiques d'un appel à
  l'autre ; répéter 3 fois ne donne pas un taux de réussite, seulement la reproductibilité. Un taux réel demande
  des tâches différentes.

## Message système (identique à chaque appel)

```text
Tu es un assistant de développement C++23 pour le dépôt OpenStitch (broderie machine, Qt 6, Catch2 v3).
Tu écris un BROUILLON qui sera compilé, testé et relu par un humain. Tu n'es jamais le dernier juge.

RÈGLES ABSOLUES
1. Tu ne modifies QUE les fichiers de la LISTE BLANCHE de la tâche. Tout autre fichier = interdit.
2. Tu n'utilises QUE des fonctions, types et includes que tu VOIS dans les extraits fournis.
   Si un nom n'apparaît pas dans le contexte, tu ne l'inventes pas : tu réponds BLOQUÉ.
3. Tu ne touches jamais à : CMakeLists.txt, .github/, CLAUDE.md, .claude/, docs/roadmap-*, fichiers de parité.
4. Tu n'ajoutes aucune dépendance, aucune bibliothèque, aucun code copié de projets GPL/AGPL.
5. Invariants du projet (si la tâche les touche, réponds BLOQUÉ : ce n'est pas pour toi) :
   - coordonnées en Micrometers/Vec2um (jamais de double brut pour une coordonnée du document) ;
   - toute mutation du document passe par une commands::ICommand ;
   - jamais generate_sequence : toujours stitch_generation::effective_sequence(project) ;
   - même entrée => même sortie, octet pour octet (pas de conteneur non ordonné dont l'ordre fuit) ;
   - pas de logique métier dans apps/desktop (câblage, rendu, déclenchement seulement).
6. Style : C++23, indentation 4 espaces, accolades même pour une ligne, pas de M_PI (std::numbers::pi),
   pas de membre nommé `slots`. Commentaires comme le fichier voisin (français).
7. Tests : noms de TEST_CASE en ASCII PUR = AUCUN caractère accentué (é è à ç ê...) : écris « negative »,
   jamais « négative ». Calque le style sur l'exemple fourni, mais ne le recopie pas : écris UNIQUEMENT les
   nouveaux cas demandés (l'exemple existe déjà ailleurs).
8. Tu ne supprimes aucun test et tu n'affaiblis aucune assertion pour le faire passer.

FORMAT DE SORTIE (strict, rien d'autre avant ou après)
- Soit un ou plusieurs blocs « ### FICHIER: chemin/relatif.cpp » suivis d'un bloc de code contenant le
  contenu COMPLET du fichier (jamais de « ... » ni de « reste inchangé »).
- Soit une seule ligne : BLOQUÉ: <raison précise, ce qui manque dans le contexte>
- Termine par « HYPOTHÈSES : » + liste de ce que tu as supposé sans pouvoir le vérifier,
  ou « HYPOTHÈSES : aucune ». N'omets jamais cette section.

Si tu hésites entre deviner et BLOQUÉ, choisis BLOQUÉ.
```

## Gabarits
Réglages d'appel : `think:false`, `options:{num_ctx:16384, temperature:0.1, seed:42}`.
Portes obligatoires après chaque sortie : liste blanche respectée (`git diff --stat`), compilation de la
cible, test, clang-format **18**, relecture de la section HYPOTHÈSES. Un seul retour en cas d'échec, puis on jette.

Légende : 🟢 faible risque · 🟡 relecture attentive · 🔴 ne le fais pas faire sans Claude/humain en parallèle.

---

## A. Tests (le meilleur usage)

### 1. 🟢 Cas Catch2 supplémentaires pour une fonction pure
```text
TÂCHE : ajouter <N> TEST_CASE à <fichier_test> pour la fonction <nom>, sur le modèle de l'exemple.
LISTE BLANCHE : <fichier_test>
CRITÈRE DE FIN : cible <cible>, commande <cible> "[<tag>]", tous les cas passent.
EXEMPLE À IMITER : <un TEST_CASE voisin, copié tel quel>
CONTEXTE : <déclaration de la fonction + types>
CAS DEMANDÉS : <liste numérotée : entrée -> sortie attendue, tirés de la spec, pas inventés>
CONTRAINTES : noms de TEST_CASE en ASCII pur ; ne modifie aucun test existant ; pas de nouvelle include hors liste.
```

### 2. 🟡 Test d'équivalence « version optimisée == version de référence »
```text
TÂCHE : écrire un test qui vérifie que <fonction_rapide> renvoie exactement le résultat de <fonction_reference>
sur des entrées pseudo-aléatoires reproductibles.
LISTE BLANCHE : <fichier_test>
CRITÈRE DE FIN : <cible> "[<tag>]" passe ; le test échoue si on casse volontairement <fonction_rapide>.
EXEMPLE À IMITER : le test d'équivalence de la grille de pick (LCG fixe 1664525/1013904223, 20 000 requêtes,
tolérances variées, requêtes hors scène).
CONTEXTE : <signatures des deux fonctions, type de retour comparable par ==>
CONTRAINTES : générateur déterministe (pas std::random_device) ; plusieurs tailles d'entrée ; inclure les cas
limites (vide, un élément, valeur non finie) ; CHECK sur chaque requête, pas seulement un échantillon.
```

### 3. 🟢 Test de cas limites
```text
TÂCHE : ajouter un TEST_CASE de cas limites pour <fonction> : vide, un seul élément, valeurs négatives,
NaN/infini si applicable, très grande taille.
LISTE BLANCHE : <fichier_test>
CRITÈRE DE FIN : <cible> "[<tag>]" passe.
EXEMPLE À IMITER : <test voisin>
CONTEXTE : <signature + comportement documenté pour chaque cas ; sinon écris « comportement non spécifié »>
CONTRAINTES : si le comportement attendu d'un cas n'est pas documenté dans le contexte, réponds BLOQUÉ pour ce
cas au lieu de le deviner ; n'écris aucune assertion que tu ne sais pas justifier.
```

### 4. 🟡 Test de déterminisme
```text
TÂCHE : écrire un test qui exécute <opération> deux fois sur la même entrée et vérifie l'égalité octet pour octet.
LISTE BLANCHE : <fichier_test>
CRITÈRE DE FIN : <cible> "[<tag>]" passe.
EXEMPLE À IMITER : <test de déterminisme existant, ex. tests/unit/.../test_*determin*.cpp>
CONTEXTE : <fonction de construction de l'entrée + fonction testée + type de sortie comparable>
CONTRAINTES : pas de sleep, pas de thread sauf si l'opération en utilise déjà ; comparer le contenu, pas les adresses.
```

### 5. 🟡 Test QTest headless d'un contrôleur
```text
TÂCHE : ajouter un slot de test à <fichier_test_qt> qui vérifie <comportement observable du contrôleur>.
LISTE BLANCHE : <fichier_test_qt>
CRITÈRE DE FIN : exécution avec QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software, 0 échec.
EXEMPLE À IMITER : <slot existant du même fichier, copié tel quel, avec le Rig/fixture>
CONTEXTE : <API publique du contrôleur : accesseurs, signaux, propriétés>
CONTRAINTES : ni pixels ni sleep (QTRY_* seulement) ; réutilise la fixture existante ; n'ajoute aucune méthode
de production ; si une méthode n'existe pas dans le contexte, réponds BLOQUÉ.
```

### 6. 🟢 Passer un test répétitif en table
```text
TÂCHE : réécrire <TEST_CASE> pour parcourir un tableau de cas (struct {entrée, attendu}) au lieu de copies,
sans changer aucune valeur ni aucune assertion.
LISTE BLANCHE : <fichier_test>
CRITÈRE DE FIN : même nombre d'assertions exécutées qu'avant (<N>), test vert.
CONTEXTE : <le TEST_CASE actuel complet>
CONTRAINTES : ne supprime aucun cas ; ne change aucune valeur attendue ; conserve le nom du TEST_CASE.
```

---

## B. Mesure et banc

### 7. 🟢 Banc [.perf] avec percentiles
```text
TÂCHE : ajouter un TEST_CASE caché "[.perf][<tag>]" qui mesure <opération> pour les tailles <liste>, 5 passes,
et affiche p50/p95/p99 par taille.
LISTE BLANCHE : <fichier_perf>
CRITÈRE DE FIN : <cible> "[<tag>][.perf]" s'exécute et imprime une ligne par taille.
EXEMPLE À IMITER : tests/unit/desktop_v2/test_perf_budget.cpp (helpers msSince/percentile, std::printf).
CONTEXTE : <signature de l'opération, comment construire l'entrée de taille N>
CONTRAINTES : horloge std::chrono::steady_clock ; entrée construite hors de la zone mesurée ; graine fixe ;
aucune assertion de seuil de temps (dépend de la machine), seulement CHECK de cohérence du résultat.
```

### 8. 🟢 Script PowerShell : lancer un banc N fois et agréger
```text
TÂCHE : écrire scripts/performance/<nom>.ps1 qui lance <exe> <args> N fois (défaut 3), extrait les lignes
« [perf-...] » et affiche, par ligne, min/médiane/max.
LISTE BLANCHE : scripts/performance/<nom>.ps1
CRITÈRE DE FIN : exécution sur la sortie d'exemple ci-dessous donne le tableau attendu.
EXEMPLE DE SORTIE À PARSER : <coller 6 lignes réelles>
CONTRAINTES : PowerShell 5.1 (pas de &&, pas de ??) ; écrire les sorties brutes dans un dossier daté ;
UTF-8 explicite ; échec clair si l'exécutable est introuvable.
```

### 9. 🟢 Tableau avant/après depuis deux sorties de banc
```text
TÂCHE : produire un tableau Markdown « avant / après / gain » à partir des deux sorties collées.
CONTEXTE : AVANT : <sortie brute> ; APRÈS : <sortie brute>
FORMAT : | Taille | avant p50 | avant p95 | après p50 | après p95 | gain p95 |
CONTRAINTES : ne recalcule rien d'autre que le gain ((avant-après)/avant, 1 décimale) ; recopie les chiffres
sans arrondir ; si une ligne manque d'un côté, écris « n/d » ; ajoute une ligne « Limites » qui cite
textuellement celles fournies, sans en retirer.
```

### 10. 🟡 Section de doc à partir de mesures
```text
TÂCHE : rédiger une section de doc FR pour docs/desktop-v2/18-studio-benchmarks.md à partir des faits ci-dessous.
FAITS (seule source autorisée) : <liste à puces : protocole, machine, chiffres, limites, ce qui n'est PAS mesuré>
FORMAT : titre ##, 1 paragraphe de protocole, 1 tableau, 1 paragraphe « Non mesuré », 4 phrases maximum de lecture.
CONTRAINTES : n'invente aucun chiffre ni comparaison ; conserve TOUTES les réserves fournies (« non mesuré »,
« machine non étalonnée ») avec leurs mots ; pas de superlatif (« excellent », « rapide »).
```

---

## C. Texte de livraison

### 11. 🟡 Message de commit conventionnel
```text
TÂCHE : écrire le message de commit (français, sans accents dans la première ligne si possible) pour le diff ci-dessous.
DIFF : <git diff --stat + extraits>
FORMAT : « type(portée): résumé » ≤ 72 caractères, ligne vide, corps 3–6 lignes (quoi, pourquoi, preuves
réellement exécutées, non vérifié), puis la ligne « roadmap : aucune entrée concernée ».
CONTRAINTES : ne cite que des tests/commandes listés dans PREUVES ; PREUVES : <liste réelle> ;
NON VÉRIFIÉ : <liste réelle> — à reprendre mot pour mot ; ne rajoute aucune attribution (je l'ajoute).
```

### 12. 🟡 Description de PR
```text
TÂCHE : écrire la description d'une PR (max 18 lignes, Markdown) : Quoi / Pourquoi / Preuves / Limites.
FAITS (seule source) : <liste>
LIMITES À REPRENDRE TEXTUELLEMENT, SANS EN OMETTRE : <liste : CI non vérifiée, revue non faite, etc.>
CONTRAINTES : la section Limites doit contenir chaque limite ci-dessus ; aucun chiffre hors FAITS ;
n'écris pas de titre de document en tête.
```
(Cas vécu : sans la consigne « sans en omettre », le modèle a supprimé « CI non vérifiée ».)

### 13. 🟢 Point de reprise de session (≤ 15 lignes)
```text
TÂCHE : écrire un point de reprise de 15 lignes maximum : objectif, décisions, branche/commit, tests passés,
prochaine action.
FAITS : <notes brutes>
CONTRAINTES : une ligne par rubrique ; ne mentionne que des tests réellement passés (liste fournie) ;
« non vérifié » écrit en toutes lettres quand c'est le cas.
```

### 14. 🟡 Entrée de journal Notion
```text
TÂCHE : rédiger une entrée de journal Notion (Markdown) pour le travail ci-dessous.
FAITS : <liste>
FORMAT : ligne de titre en gras avec statut (« En PR », « À valider », jamais « Validé »), puis puces :
Démontré / Tests / CI / Non démontré.
CONTRAINTES : « Validé » est interdit sans mention explicite de la preuve fournie dans FAITS ; copie les limites telles quelles.
```

---

## D. Code mécanique

### 15. 🟢 Renommage mécanique dans une liste fermée
```text
TÂCHE : renommer <ancien> en <nouveau> dans les fichiers de la liste, uniquement les occurrences listées.
LISTE BLANCHE : <fichiers>
OCCURRENCES : <sortie de rg -n "<ancien>" sur ces fichiers>
CRITÈRE DE FIN : compilation de <cible> ; rg "<ancien>" ne trouve plus rien dans la liste.
CONTRAINTES : ne reformate rien ; ne change aucune autre ligne ; sortie = fichiers complets.
```

### 16. 🟢 Commentaires de documentation sur déclarations publiques
```text
TÂCHE : ajouter un commentaire de 1–2 lignes (français) au-dessus de chaque déclaration publique de <en-tête>.
LISTE BLANCHE : <en-tête>
CONTEXTE : <en-tête complet>
CONTRAINTES : ne change aucun code, aucune signature, aucun include ; décris ce que le nom et la signature
montrent, rien de plus ; si le rôle n'est pas évident, écris « (rôle à confirmer) » au lieu d'inventer.
```

### 17. 🟡 Ajouter [[nodiscard]] / const sur une liste de signatures
```text
TÂCHE : ajouter [[nodiscard]] aux fonctions listées de <en-tête> dont la valeur de retour ne doit pas être ignorée.
LISTE BLANCHE : <en-tête>
FONCTIONS : <liste>
CRITÈRE DE FIN : compilation de <cible> sans nouvel avertissement ; aucun appelant cassé.
CONTRAINTES : ne touche pas aux autres lignes ; si un appelant ignore la valeur, signale-le en HYPOTHÈSES.
```

### 18. 🟡 Données de test synthétiques
```text
TÂCHE : écrire une fonction de test qui construit <scène/projet> de N éléments (N variable) de façon déterministe.
LISTE BLANCHE : <fichier_test>
EXEMPLE À IMITER : syntheticPickScene / gridBatch de tests/unit/desktop_v2/test_perf_budget.cpp.
CONTEXTE : <types du document et factory réellement disponibles>
CONTRAINTES : coordonnées en entiers µm (Micrometers) pour tout objet du document ; aucune aléa non seedé ;
pas de nouvelle fonction de production.
```

---

## E. Lecture seule (aucun fichier modifié)

### 19. 🟢 Résumé de journal de build / de test
```text
TÂCHE : résumer ce journal en 10 lignes max : statut, erreurs distinctes avec fichier:ligne, avertissements nouveaux.
JOURNAL : <coller les 150 dernières lignes>
CONTRAINTES : cite uniquement fichier:ligne présents dans le journal ; ne propose AUCUN correctif ; si le
journal est tronqué, dis-le.
```

### 20. 🟡 Second avis sur un diff (checklist, sans réécrire)
```text
TÂCHE : relire ce diff et lister, SANS le modifier, ce qui semble incohérent avec les règles du projet.
DIFF : <diff>
RÈGLES À VÉRIFIER : coordonnées en Micrometers ; mutations par ICommand ; effective_sequence ; déterminisme ;
pas de logique métier dans apps/desktop ; tests ASCII ; aucun test affaibli ; fichiers hors périmètre.
FORMAT : une ligne par constat « fichier:ligne — règle — pourquoi (citation de la ligne) » ; si rien :
« aucun constat ». 
CONTRAINTES : ne cite que des lignes présentes dans le diff ; indique un niveau de confiance (haut/moyen/bas) ;
les constats sont des pistes à vérifier par un humain, pas des verdicts.
```

### 21. 🟢 Relecture orthographique d'un document
```text
TÂCHE : corriger orthographe, accords et ponctuation du texte ci-dessous, sans modifier le sens.
TEXTE : <texte>
CONTRAINTES : ne touche à aucun nombre, nom de fichier, identifiant, chemin ou bloc de code ; renvoie le texte
complet puis la liste des corrections (avant → après).
```

### 22. 🟢 Classer des résultats de recherche
```text
TÂCHE : trier ces résultats de recherche (chemin:ligne — extrait) en 3 groupes : « définition », « appelant »,
« test / doc ».
RÉSULTATS : <sortie de rg>
CONTRAINTES : recopie chaque ligne telle quelle dans un seul groupe ; n'en ajoute ni n'en supprime aucune ;
si le groupe est incertain, mets « à vérifier ».
```

---

## Ce qu'il ne faut PAS lui confier seul (🔴)

- Le diagnostic d'un bug sans reproduction, la cause racine, un choix d'architecture.
- Tout code touchant `ICommand`, `effective_sequence`, la conversion µm/mm, l'ordre de couture, les finitions.
- Le code concurrent ou asynchrone (verrous, `TaskManager`, révisions) et les CMakeLists.
- Les décisions de statut Notion (« Validé ») et tout texte censé conclure qu'une preuve existe.
