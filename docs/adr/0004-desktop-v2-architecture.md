# ADR 0004 — Architecture de Desktop V2 : coexistence, modèles partagés, un seul chemin de mutation

Statut : **accepté** (2026-11-04).

Le shell cible est révisé par l'[ADR 0005 — Studio Qt Quick / GPU](0005-studio-qtquick-gpu.md)
(2026-11-06). Les modèles communs, l'historique unique et le chemin de mutation de cette ADR sont conservés.

## Contexte

`MainWindow` porte 174 actions et ≈ 449 champs d'état ; la sélection, les tâches et les outils n'ont pas de modèle
partagé (`docs/desktop-v2/01-audit-dette.md`). Les audits antérieurs recommandaient des corrections incrémentales.

## Décisions

1. **Coexistence.** `apps/desktop_v2/` est une application distincte ; `apps/desktop` reste le desktop principal et
   continue d'être corrigé (PR #236) jusqu'à ce que la matrice de parité (`02`) soit satisfaite. Aucune dépendance de
   V2 vers l'ancien desktop (hors l'`enum Tool`, lu par un test de parité).
2. **Quatre modèles Qt-free**, dans `core/` : `DocumentSession`, `SelectionModel`, `TaskManager`, `ToolRegistry`.
   Les widgets s'y abonnent ; ils ne portent ni état de document, ni état de sélection, ni logique de calcul.
3. **Un seul chemin de mutation, deux entrées.**
   - gestes interactifs : `DocumentSession::execute(unique_ptr<ICommand>)` ;
   - lots (MCP, Claude, scripts, et gestes simples comme renommer/masquer) : `DocumentSession::applyBatch(JSON)` →
     `project_ops::apply_batch` sur une copie → `ReplaceProjectCommand` (une étape d'annulation exacte).
   Il n'y a **pas** de classe `CommandDispatcher` distincte à ce stade : elle ne ferait que renommer `DocumentSession`.
   Si un besoin apparaît (journalisation, politique d'approbation, files d'attente), elle sera extraite alors.
4. **Règle « catalogue unique ».** Toute capacité V2 doit exister comme opération de lot (ou composition) avant d'être
   câblée à l'interface. Écart mesuré : 55 opérations de lot contre ≈ 91 commandes ; chaque lot de migration comble
   l'écart de sa famille. Le compagnon n'a ainsi aucune API privée.
5. **Instantanés immuables** (`DocumentSession::snapshot`) pour tout calcul d'arrière-plan : le fil de travail ne voit
   jamais le projet vivant. Une tâche annulée ne livre rien.
6. **Les espaces de travail sont des filtres**, pas des documents : même projet, même sélection, même historique.
   Quatre à ce stade (Conception, Numérisation, Optimisation, Production) — hypothèse de la mission **conservée** parce
   que (a) le registre d'outils se partitionne naturellement ainsi (20 outils : 10 propres à la Conception, 4 propres à la Numérisation, 3 communs à tous les espaces, 3 partagés entre deux ou trois espaces),
   (b) aucun parcours étudié n'impose un enchaînement : on peut revenir à la segmentation à tout moment. À reconsidérer
   si la recette de parcours (`05`) montre des allers-retours d'espace trop fréquents.
7. **Pas de réécriture du moteur.** Aucune lib n'est modifiée par cette ADR.

## Alternatives écartées

- *Poursuivre l'extraction de `MainWindow` seule* : réduit la taille des fichiers, pas le couplage (un seul `this`).
- *Réécrire en QML d'emblée* : voir ADR 0003.
- *Un `CommandDispatcher` générique avec registre de commandes par chaîne* : spéculatif tant que le catalogue de lot existe déjà.

## Conséquences / dette assumée

- Deux desktops à maintenir pendant la migration ; la PR #236 modifie l'ancien et n'est pas en conflit (fichiers disjoints).
- `saveProject` et la reconstruction de la liste d'objets sont encore **synchrones** sur le fil d'interface (acceptable
  à 138 objets ; à basculer en tâche et à virtualiser avant les gros documents).
- Le test de parité des outils lit `apps/desktop/tools.hpp` : à supprimer à la bascule.
