---
name: openstitch-docs
description: Consulter ou modifier la documentation OpenStitch (docs/ et docs/source/*.md) en passant TOUJOURS par le MCP mesh-mcp (search_docs). À utiliser dès qu'une question porte sur le fonctionnement, l'historique, les décisions ou les limites d'un sous-système (satin, auto-satin, tatami, remplissage directionnel, overrides, format .osp, DST…), avant de modifier du code dans un domaine documenté, et avant d'écrire ou de mettre à jour une page de doc.
---

# Documentation OpenStitch via mesh-mcp

La doc fait plusieurs centaines de Ko (`docs/source/satin.md` seul fait
~340 Ko / 2 700+ lignes). Ne jamais la lire en entier ni la parcourir « à
l'aveugle » : on **localise d'abord avec `search_docs`**, puis on lit
uniquement les plages de lignes utiles.

## 1. Localiser — `mcp__mesh-mcp__search_docs`

- Charger l'outil si besoin : `ToolSearch("select:mcp__mesh-mcp__search_docs")`.
- Appel : `search_docs(query, max_sections)`. Chaque résultat contient le
  titre, le fichier, la plage `fichier.md:début-fin` **et le texte complet
  de la section** — souvent suffisant sans autre lecture.
- Les sections peuvent être longues (jusqu'à ~200 lignes, ex. « Dette
  technique connue » de `limitations.md`) : garder `max_sections` à 2–3
  pour une question précise ; ne monter à 6–8 que pour cartographier un
  sujet, et avec une requête spécifique.
- Faire **plusieurs requêtes courtes** plutôt qu'une longue, en variant :
  - le terme français de la doc (« jonction », « remplissage directionnel »,
    « sous-couche », « repli tatami ») ;
  - le nom du symbole C++ (`build_satin_columns`, `effective_sequence`,
    `extend_tip`, `DirectionalFillParams`) ;
  - le nom anglais du concept (« junction », « underlay », « skeleton »).
- Lancer les requêtes indépendantes **en parallèle** dans le même message.

## 2. Compléter — `Read` avec `offset`/`limit`

Uniquement si le texte renvoyé ne suffit pas (besoin de la section parente,
des sections voisines, ou avant d'éditer la page) : lire le fichier autour
de la plage indiquée (chemin Windows : remplacer `/mnt/c/Users/zache/...`
par `C:\Users\zache\...`). Ne lire un fichier entier que s'il est petit
(< ~15 Ko) — jamais `satin.md` (~340 Ko), `moteur-de-points.md` ou
`testing.md` en entier.

## 3. Repli si mesh-mcp ne répond pas

Si les outils `mcp__mesh-mcp__*` sont absents ou en erreur (serveur non
démarré → `/mcp` pour le reconnecter), ou si `search_docs` ne trouve rien
de pertinent après 2–3 reformulations : `Grep` sur `docs/` (mode `content`,
`-C 3`), puis `Read` ciblé comme ci-dessus. Signaler à l'utilisateur que le
MCP était indisponible.

## 4. Modifier la doc

1. `search_docs` sur le sujet **avant d'écrire** : compléter la section
   existante plutôt que d'en créer une doublon ailleurs.
2. Respecter le style des chapitres existants : français, en-tête
   « *État : …* » (Présent / Testé / Expérimental…), noms de symboles et de
   fichiers entre backticks, cause racine + correctif + preuve de test pour
   chaque bug corrigé (cf. `satin.md`).
3. Un nouveau chapitre dans `docs/source/` n'entre dans le PDF que s'il est
   ajouté à la liste `CHAPTERS` de `docs/scripts/build-docs.py` (ordre
   éditorial).
4. Régénérer le PDF : `powershell -File docs/scripts/build-docs.ps1`.
5. Relancer un `search_docs` sur le nouveau contenu pour vérifier qu'il est
   bien retrouvé. S'il ne l'est pas, l'index n'a pas encore été rafraîchi :
   reconnecter le serveur (`/mcp`) avant de conclure à un problème.

## Carte rapide

| Sujet | Où chercher en premier |
|---|---|
| Satin, auto-satin, jonctions, guides, routage | `docs/source/satin.md` (+ `docs/auto-satin-*.md`) |
| Tatami | `docs/source/tatami.md` |
| Remplissage directionnel | `docs/source/directional-fill.md` |
| Moteur de points, overrides, `effective_sequence` | `docs/source/moteur-de-points.md`, `docs/lot8-manual-editing-design.md` |
| Auto-numérisation | `docs/source/auto-numerisation.md` |
| Modules, « où changer X » | `docs/source/module-reference.md` |
| État des fonctionnalités, dette | `docs/source/limitations.md` |
| Format `.osp` / DST | `docs/source/project-format.md`, `docs/source/dst-format.md` |
| Tests | `docs/source/testing.md` |
| ADR / décisions initiales | `docs/phase0/` |
