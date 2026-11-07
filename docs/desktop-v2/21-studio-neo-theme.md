# Neo Studio — thème par défaut (première passe)

> Contrat visuel pour **Qt Quick / GPU** (`apps/desktop_v2/studio/qml/`). Il ne crée pas de moteur de thèmes personnalisables.

## Principes
- Fond graphite / bleu nuit ; la broderie et les couleurs de fils restent prioritaires.
- Framboise pour sélection, action importante et focus ; accents secondaires cyan/violet très rares.
- Texte secondaire réellement lisible, contrastes contrôlés et états hover / focus / pressed / disabled perceptibles.
- Typographie : Segoe UI Variable sur Windows si disponible ; substitution système ailleurs. Échelle 11 / 12 / 13 / 16 / 20 / 22 px (caption, small, body, heading, brand, hero).
- Rythme : marges principales 18 px, contrôles 38 px, lignes d'objets 42 px, rayon 8 px ; préserver le réglage existant de densité.
- Messages : couleurs sémantiques `Theme.error`, `Theme.warning`, `Theme.success`, `Theme.info`, plutôt que des hexadécimaux dispersés.
- Transitions de couleurs 150 ms par défaut, désactivées via « Réduire les animations ».

## Source de vérité
`Theme.qml` fournit les tokens ; `StudioButton.qml` et `SectionLabel.qml` les consomment. Les couleurs documentaires et les champs HEX de l'utilisateur ne sont pas des couleurs de thème.

## Validation à effectuer
1. Ouvrir le shell et vérifier typographie, clavier et états de boutons ; contrôler clair / sombre, densité et mouvement réduit.
2. Ouvrir un vrai `.osp` et confirmer que canevas et couleurs de fils restent intacts.
3. Comparer captures GPU/CPU, largeur minimale et HiDPI lorsque l'environnement Windows/Qt est disponible.

**Hors périmètre :** galerie multithèmes, animations configurables, personnalisation, rendu GPU, moteur et workflow.
