# Autoévaluation /20 sans API

Le laboratoire peut enregistrer une note indicative quand le numériseur évalue lui-même sa broderie. Cette note n'est **pas indépendante** et ne valide jamais un essai sur machine.

## Procédure

1. Exécuter réellement OpenStitch : produire l'OSP, son DST, la preview et les diagnostics JSON, en conservant l'original et la segmentation.
2. **Ouvrir et comparer visuellement** l'original et le rendu réel à échelle comparable. Si cela n'est pas possible, ne pas attribuer de note.
3. Rédiger le fichier review.json au format du skill openstitch-evaluate avec les champs supplémentaires :
   - `evaluation_mode: "self_review"`
   - `reviewer` identique à `digitizer`
   - `visual_review` : au moins 80 caractères de critique concrète
   - `comparison_method` : au moins 20 caractères décrivant la comparaison
4. Garder les six sous-notes et preuves du barème existant, les détails identitaires critiques et les rapports CLI authentiques.
5. Exécuter `python3 scripts/openstitch_evaluate.py essai/review.json -o essai/score.json`.
6. Journaliser `score_kind: "indicative_self_review"` sur agent-trials. Une autoévaluation >=17/20 déclenche une demande de revue indépendante, **pas une validation**.

Le mode historique `independent` reste le mode par défaut et exige un autre relecteur. Le script ne voit pas les images : il contrôle la structure des preuves déclarées, pas la véracité de l'inspection visuelle. En l'absence de rendu réel ou de preuves, conserver `score: null`.

Aucune clé API n'est nécessaire.
