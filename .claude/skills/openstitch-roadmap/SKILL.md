---
name: openstitch-roadmap
description: Roadmap technique de parité Hatch (docs/roadmap-parite-hatch.md), à croiser obligatoirement avec la roadmap Notion en mode autonome sur OpenStitch. À utiliser dès que l'utilisateur dit « continue », « fais la suite », « avance », « prochaine tâche », « qu'est-ce qu'il reste à faire », « travaille en autonomie », lance un /loop ou une routine sans tâche précise, demande ce qui manque par rapport à Hatch. Ne pas l'utiliser pour enchaîner de sa propre initiative après une mission ciblée.
---

# Priorité Notion pour les missions autonomes

**Depuis l'adoption du Project HQ Notion**, ce skill n'est plus un sélecteur autonome unique. Pour « continue OpenStitch » et toute tâche non ciblée, **commencer par** `openstitch-notion` et `docs/agents/notion-protocol.md`. Chercher la tâche canonique et les réservations dans Notion ; réconcilier ensuite ce fichier technique, qui reste une source de contraintes et d'idées de parité Hatch, mais **ne peut pas contredire un chantier Notion déjà en cours**. Reprendre d'abord les PR engagées et la transition Studio.

Si l'accès Notion ou les baux ne sont pas vérifiables, ne pas choisir un nouveau lot autonome à partir du présent document seul. Ne pas marquer la parité Studio « Validé » parce que le backend existe. Les descriptions de lot et de tests ci-dessous restent applicables à la réalisation technique quand la tâche est autorisée.

# Suivre la roadmap de parité Hatch

L'objectif du projet est de **concurrencer Hatch Embroidery en open source**.
`docs/roadmap-parite-hatch.md` liste tout ce qui rend OpenStitch moins bon
(formats, fils, texte, points, édition, rendu, performance, distribution,
validation physique), avec priorités (P0 → P3), vagues de livraison,
dépendances et critères d'acceptation. **En mode piloté, c'est la liste de
travail** : on ne choisit pas une tâche au hasard ou « parce qu'elle est
intéressante ».

Une consigne explicite de l'utilisateur passe **toujours** avant la roadmap. La
roadmap sert quand il n'y a pas de consigne, ou pour choisir la suite.

## 1. Choisir l'entrée

Lire le §0 et le §1 du fichier (ne pas relire le fichier entier à chaque fois :
chercher les statuts avec Grep sur `▶ En cours` puis sur la vague courante).

1. Une entrée `▶ En cours` existe → la terminer d'abord.
2. Sinon, prendre la **première entrée non terminée de la vague la plus basse**
   (§1), dans l'ordre de la vague, en respectant `Dépend de`.
3. Ne jamais sauter une P0 pour une P1/P2. Exception : une dépendance technique
   (ex. HP-QA-001 découpe de `main_window.cpp` avant HP-OBJ-001) — la noter.
4. Entrée trop grosse pour un lot livrable → la découper en sous-entrées
   `HP-XXX-NNN.a`, `.b`… **dans le fichier** (même format), puis faire `.a`.
5. Entrée qui exige l'humain (machine réelle HP-PHYS-*, signature de code,
   choix de nom, décision de licence) → préparer ce qui peut l'être, marquer
   `◐ Partiel` avec « attend : … », et passer à la suivante.

Annoncer en une ligne à l'utilisateur quelle entrée est prise et pourquoi.

## 2. Vérifier avant de coder

Le fichier a été écrit le 2026-09-22 ; le code a pu bouger depuis.

- Vérifier dans le code que le manque **existe toujours** (Grep/Read du module
  cité). Si c'est déjà fait : marquer `☑ Fait` (ou `◐`) avec ce qui a été
  constaté, et reprendre au §1.
- Vérifier le comportement réel de Hatch si l'entrée en dépend (manuel public
  Hatch/Wilcom) — l'entrée dit *quoi* combler, pas *comment*.
- Relire les règles du projet (`CLAUDE.md`) : cœur sans Qt, **aucun code
  GPL/AGPL** (Ink/Stitch, Potrace, Poppler, Ghostscript, MuPDF sont exclus),
  µm entiers, `ICommand` pour toute mutation, `effective_sequence`,
  déterminisme. Une lib tierce nouvelle → licence vérifiée et ajoutée à
  `THIRD_PARTY_LICENSES.md`.

## 3. Réaliser avec les autres skills

**Câblage bureau obligatoire.** Sauf demande explicite contraire de l'utilisateur,
une fonctionnalité n'est livrée que si elle est **câblée dans le bureau**
(`apps/desktop` : menu ou outil, dialogue, panneau, annulation par `ICommand`,
test QTest headless) en plus du cœur et du CLI. Une entrée dont seul le cœur
existe reste `◐ Partiel` avec « Reste : câblage bureau » : ne jamais la marquer
`☑ Fait`, et ne pas choisir une autre entrée tant que celle-ci n'est pas câblée,
sauf si le câblage dépend d'un travail humain ou d'une autre entrée (le noter).

- Doc d'un sous-système : `openstitch-docs` (toujours via `search_docs`).
- Nouveau paramètre / type de point / champ persistant : `openstitch-stitch-param`.
- Défaut trouvé en route : `openstitch-bugfix`.
- Rendu des points à vérifier : `openstitch-cli-debug`.
- Build et tests : `openstitch-build-test` (tests d'abord pour le cœur ;
  QTest offscreen pour l'UI).
- Livraison : `openstitch-commit` (un commit par entrée ou sous-entrée,
  message qui cite l'identifiant `HP-XXX-NNN`).

Respecter les **critères d'acceptation** de l'entrée ; s'ils sont irréalistes,
les amender dans le fichier en expliquant pourquoi — ne pas les ignorer.

## 4. Mettre à jour la roadmap (obligatoire)

Cette obligation vaut **pour tout travail qui aboutit à un commit**, piloté
ou sur consigne explicite (elle est aussi vérifiée par `openstitch-commit`
et rappelée par le hook de session). Dans le même commit que le code :

- Statut de l'entrée : `☑ Fait (AAAA-MM-JJ, <hash court>)` + ligne
  « Livré : … » (ce qui existe maintenant, limites restantes). Ne jamais
  supprimer une entrée terminée.
- Livraison partielle : `◐ Partiel` + « Reste : … ».
- Manque nouveau découvert face à Hatch : ajouter une entrée (prochain numéro
  de la catégorie, priorité justifiée) et, si P0/P1, l'insérer dans une vague.
- Une ligne dans le §28 (journal) : date, entrée, changement.
- Mettre aussi à jour `docs/source/limitations.md` / `roadmap.md` si le
  tableau des fonctionnalités change, puis régénérer le PDF
  (`powershell -File docs/scripts/build-docs.ps1`).

## 5. Enchaîner

Entrée terminée, tests verts, commit et PR faits → rendre compte en quelques lignes
(entrée livrée, preuve, prochaine entrée prévue). Reprendre au §1 **uniquement en mode
autonomie continue** (l'utilisateur a demandé d'enchaîner : « continue », « travaille en
autonomie », `/loop`) ; en mission ciblée, s'arrêter et proposer la suivante en une ligne
(modes : `openstitch-commit`). Pour chaque lot enchaîné : un seul `Grep` de statuts (pas de
relecture du fichier), et si le contexte devient lourd, point de reprise
(`openstitch-token-efficiency` §4) plutôt que d'accumuler.
