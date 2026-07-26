# À propos de ce document

Cette documentation décrit **OpenStitch Studio**, une application de bureau libre
de numérisation pour broderie machine. Elle couvre l'usage du logiciel, les
concepts de broderie qu'il met en œuvre, son architecture logicielle, ses
formats de fichiers et sa compilation.

Le document s'adresse à quatre publics : l'**utilisateur débutant**, l'**utilisateur
avancé en broderie**, le **développeur contributeur** et le **mainteneur**. Chaque
chapitre indique clairement à qui il s'adresse en priorité.

## Principe de véracité

Toutes les affirmations sur le fonctionnement du logiciel sont vérifiées dans le
code réel du dépôt. Lorsqu'une capacité était prévue mais n'est pas encore
disponible, elle est marquée explicitement :

- **Implémenté** : disponible et testé ;
- **Partiellement implémenté** : présent mais incomplet ;
- **Expérimental** : présent, non stabilisé ;
- **Prévu** : conçu dans l'architecture, non implémenté ;
- **Non implémenté** : absent.

L'annexe *Audit du dépôt* recense l'inventaire factuel qui sert de base à ce
document. Chaque chapitre technique se termine par une section **Implémentation
associée** listant les fichiers, classes, fonctions et tests réels.

## Comment lire ce document

- Vous voulez **utiliser** le logiciel : lisez *Introduction*, *Installation*,
  *Guide de prise en main*, puis *Guide utilisateur détaillé*.
- Vous voulez **comprendre la broderie** telle qu'implémentée : lisez *Concepts*
  (points, satin, tatami) et *Pipeline image vers broderie*.
- Vous voulez **contribuer** : lisez *Architecture*, *Référence des modules*,
  *Modèle de données*, *Algorithmes*, *Compilation et développement*, *Tests*.
- Vous voulez **maintenir** : ajoutez *Limitations et roadmap*, *Licences* et le
  *Guide de contribution*.

Note : Ce document est généré automatiquement à partir des fichiers Markdown de
`docs/source/`. Ne le modifiez pas directement dans le PDF.
