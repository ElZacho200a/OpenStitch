# À propos de ce document

Cette documentation décrit **OpenStitch Studio**, une application de bureau libre
de numérisation pour broderie machine. Elle couvre l'usage du logiciel, les
concepts de broderie qu'il met en œuvre, son architecture logicielle, ses
formats de fichiers et sa compilation.

Le document s'adresse à quatre publics : l'**utilisateur débutant**, l'**utilisateur
avancé en broderie**, le **développeur contributeur** et le **mainteneur**. Chaque
chapitre indique clairement à qui il s'adresse en priorité.

## Principe de véracité

Les descriptions de fonctionnalités ont été **rapprochées du code et des tests**
présents dans le dépôt (lire le code confirme qu'une fonction existe et ce
qu'elle fait, mais ne garantit pas que son résultat est de qualité — d'où la
distinction des niveaux de validation, voir *Limitations*). Lorsqu'une capacité
était prévue mais n'est pas encore disponible, elle est marquée explicitement :

- **Implémenté** : disponible et testé ;
- **Partiellement implémenté** : présent mais incomplet ;
- **Expérimental** : présent, non stabilisé ;
- **Prévu** : conçu dans l'architecture, non implémenté ;
- **Non implémenté** : absent.

L'annexe *Audit du dépôt* recense l'inventaire factuel qui sert de base à ce
document. Chaque chapitre technique se termine par une section **Implémentation
associée** listant les fichiers, classes, fonctions et tests réels.

## Comment lire ce document

- Vous voulez **utiliser** le logiciel : lisez [Introduction](introduction.md),
  [Installation](installation.md) (sans compiler : binaire de la page Releases),
  [Guide de prise en main](getting-started.md), puis
  [Guide utilisateur détaillé](user-guide.md). En cas de souci :
  [Dépannage](troubleshooting.md) et [Glossaire](glossary.md).
- Vous voulez **automatiser** ou diagnostiquer : [Ligne de commande](cli.md).
- Vous voulez **comprendre la broderie** telle qu'implémentée : lisez *Concepts*
  ([point droit](stitch-generation.md), [satin](satin-squelette.md),
  [tatami](tatami.md)) et [Numérisation automatique](auto-numerisation.md).
- Vous voulez **contribuer** : lisez [Architecture](architecture.md),
  [Référence des modules](module-reference.md), [Modèle de données](data-model.md),
  [Algorithmes](algorithms.md), [Compilation et développement](build-system.md),
  [Tests](testing.md).
- Vous voulez **maintenir** : ajoutez [Limitations](limitations.md),
  [Roadmap](roadmap.md), [Licences](licenses.md) et le
  [Guide de contribution](contributing.md).

## Table des matières

**Utiliser le logiciel**

- [Introduction](introduction.md)
- [Guide de prise en main](getting-started.md)
- [Installation et premier démarrage](installation.md)
- [Guide utilisateur détaillé](user-guide.md)
- [Ligne de commande (openstitch-cli)](cli.md)
- [Dépannage](troubleshooting.md)
- [Glossaire](glossary.md)

**Du pixel au point**

- [Traitement d'image](image-processing.md)
- [Segmentation](segmentation.md)
- [Vectorisation](vectorization.md)
- [Objets de broderie](embroidery-objects.md)
- [Numérisation automatique](auto-numerisation.md)
- [Génération de points](stitch-generation.md) et
  [moteur de points](moteur-de-points.md)
- [Satin par squelette](satin-squelette.md), [colonne satin](satin.md),
  [tatami](tatami.md), [remplissage directionnel](directional-fill.md)
- [Retouche des points](stitch-editing.md)
- [Palettes et fils](palettes-and-threads.md), [simulation](simulation.md),
  [analyse et validation](analysis-and-validation.md)

**Formats, architecture et développement**

- [Format DST](dst-format.md), [format de projet `.osp`](project-format.md)
- [Architecture](architecture.md), [référence des modules](module-reference.md),
  [modèle de données](data-model.md), [algorithmes](algorithms.md)
- [Compilation et développement](build-system.md),
  [guide du développeur](developer-guide.md), [tests](testing.md),
  [contribution](contributing.md)
- [Limitations](limitations.md), [roadmap](roadmap.md), [licences](licenses.md)
- Recherche brevets : [synthèse](patent-research.md),
  [satin](patent-sheets-satin.md), [auto-numérisation](patent-sheets-autodigitize.md),
  [sous-couches](patent-sheets-underlay-geometry.md),
  [décoratif et appliqué](patent-sheets-decorative-applique.md)
- [Annexe : audit du dépôt](generated-project-audit.md)

Dans le PDF, tous ces liens sont cliquables (et la table des matières générée
au début du document renvoie aussi aux chapitres).

Note : Ce document est généré automatiquement à partir des fichiers Markdown de
`docs/source/`. Ne le modifiez pas directement dans le PDF.
