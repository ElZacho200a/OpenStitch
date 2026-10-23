# Contribuer à OpenStitch

Les contributions sont bienvenues : signalement de bug reproductible,
proposition d'ergonomie, tests, documentation et correctifs C++/Qt.

1. **Lire [CLAUDE.md](CLAUDE.md) et [GUARDRAILS.md](GUARDRAILS.md)** :
   licence Apache-2.0, cœur sans Qt, unités en micromètres et `ICommand` pour
   chaque mutation du document.
2. Consulter [la roadmap](docs/roadmap-parite-hatch.md) avant de commencer
   pour vérifier qu'une fonctionnalité n'est pas déjà livrée.
3. Créer une branche sur votre fork, implémenter un lot ciblé et ajouter des
   tests. Les instructions de build et de tests se trouvent dans le
   [guide de contribution détaillé](docs/source/contributing.md).
4. Ouvrir une Pull Request contre `main`. Décrire la commande de test, le
   résultat réel et, pour tout changement visible, le parcours desktop et
   son test QTest. La CI GitHub doit être verte avant toute fusion.

Utiliser les [modèles d'issues](.github/ISSUE_TEMPLATE/) pour signaler un bug
ou demander une fonctionnalité. Ne pas publier de secrets, fichiers client
ou images protégées sans autorisation. Les essais sur machine réelle doivent
être clairement distingués des tests logiciels.
