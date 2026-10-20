---
name: openstitch-digitize
description: Numériser une image en broderie avec OpenStitch (image → objets → points → DST) de façon à obtenir un résultat brodable et fidèle, et diagnostiquer une numérisation décevante. À utiliser quand l'utilisateur fournit une image (illustration, logo, photo, dessin au trait) à broder, demande « numérise ça », « pourquoi c'est moche / lourd / plein de coupes », compare une numérisation automatique à une version retouchée à la main (.osp), ou quand on règle `digitize`, `--dark-lines`, `--satin-bands`, `--skip-background`, `--max-colors`, `--min-region-mm2`.
---

# Bien numériser une image en broderie

La numérisation automatique donne un **point de départ**, pas un résultat fini. La méthode
ci-dessous vient d'un cas réel : une illustration plate de navire (1448 × 1086 px, 300 mm), où
une version retouchée à la main par l'utilisateur était nettement meilleure que la sortie
automatique. Chaque règle porte la mesure qui la justifie. Documentation de référence :
`docs/source/auto-numerisation.md` (à lire par `search_docs`, voir le skill `openstitch-docs`).

## 1. Regarder l'image avant de lancer quoi que ce soit

| Question | Pourquoi |
|---|---|
| **Plate (aplats, contours nets) ou photo ?** | Plate : mode `shapes` (défaut). Dessin au trait : `contours`. Photo : `stipple`, ou la réduire à 6–10 couleurs avant (une photo en `shapes` donne des milliers de petites régions). |
| **Y a-t-il un fond ?** | Un fond (papier, crème, blanc) ne se brode pas. Voir « Le fond » ci-dessous. |
| **Taille réelle voulue (mm) ?** | `--dpi` fixe l'échelle : mm/px = 25,4 / dpi. 1448 px pour 300 mm → `--dpi 122.6`. Sans `--dpi`, 96 est supposé et la pièce sort 1,3 fois trop grande. Les détails fins se jugent en mm, pas en pixels. |
| **Combien de vraies couleurs de fil ?** | `--max-colors` (défaut 8). Compter les teintes distinctes visibles ; mettre 1 à 2 de plus que ce compte, jamais 64 : deux quasi-doublons = un changement de fil de plus pour rien. |
| **Contours noirs fins (mâts, haubans, bords) ?** | `--dark-lines`, voir `references/parametres.md`. |

## 2. Lancer, mesurer, regarder

Binaire : `openstitch-cli` (voir `openstitch-build-test` pour le build). Sorties dans le
**scratchpad**, jamais à la racine du dépôt.

**Boucle de travail (tout est dans le CLI, déterministe, sans script maison) :**

1. **Comparer des réglages d'un coup** : `project sweep` (grille d'options → tableau objectif + planche de
   zooms réalistes). C'est le point d'entrée, avant tout essai à la main.
2. **Numériser la variante retenue** : `workflow.sh` (ci-dessous) ou `project digitize … --debug-dir`.
3. **Chercher les défauts** : `project diagnose` (détecteurs avec objets et option conseillée), puis
   `project fidelity --map`, `project density --heatmap`.
4. **Voir comme l'utilisateur** : `project render` (réaliste, fond vert), zoom avec `--crop`.
5. **Pourquoi cet endroit est-il ainsi ?** `project at --px X,Y` (quel objet, quel type, quels
   paramètres) et `digitize --debug-dir` (`traits.png` : rouge = trait cousu, bleu = barre, vert =
   séparation non cousue, orange = fragment trop court).

```bash
# 1. balayage : 8 variantes en ~40 s, tableau + planche
openstitch-cli project sweep img.png --dpi 122.6 --max-colors 12 --dark-lines --dark-lines-bars 4 \
    --min-region-mm2 1 --fill-inset 0 --satin-bands --vary island-cleanup=0,20 \
    --vary curve-tolerance=0,0.4 --vary no-underpath=0,1 --sheet $SCRATCH/planche.png \
    --sheet-crop 0.12,0.3,0.46,0.72 --journal docs/journal.md --title "Mon image"
# 2. variante retenue, de bout en bout (cadre, contrôle, diagnostic, fidélité, densité, rendu)
CLI=/chemin/openstitch-cli .claude/skills/openstitch-digitize/scripts/workflow.sh \
    img.png $SCRATCH/v1 300 210 122.6 --max-colors 12 --dark-lines --dark-lines-bars 4 \
    --dark-lines-min-length 6 --min-region-mm2 1 --fill-inset 0 --satin-bands \
    --curve-tolerance 0.4 --no-underpath --hidden-travel
```

`workflow.sh` : image, dossier de sortie, largeur et hauteur du cadre (mm), dpi, puis les options de
`digitize`. Il écrit `p.osp`, `out.dst`, `debug/` (masques), `realiste.png`, `fidelite.png`,
`densite.png` et imprime les chiffres. Une variante = un nouveau dossier.

Commandes unitaires :

```bash
openstitch-cli stats sortie.dst        # points, sauts, coupes, changements de fil, dimensions
openstitch-cli project digitize img.png -o p.osp --dst sortie.dst --dpi 122.6 --debug-dir dbg
openstitch-cli project diagnose p.osp                # défauts connus
openstitch-cli project fidelity p.osp --map err.png  # tissu visible, détails perdus
openstitch-cli project density p.osp --heatmap d.png # épaisseurs de fil
openstitch-cli project at p.osp --px 640,410         # qui a cousu ce point ?
openstitch-cli project render p.osp -o r.png --px-per-mm 14 --crop 0.1,0.25,0.5,0.75
```

**Ne jamais juger sur un rendu de script en lignes** (fausses alertes de l'essai 1 : crème « disparu »,
trous invisibles) : `project render` est le moteur de l'aperçu de l'application.

Lire **dans cet ordre** :

1. **Dimensions** : si elles dépassent la taille voulue, le fond a été brodé (« Le fond »).
2. **Messages de `digitize`** : fond détecté ou non, `Surface non couverte`, nombre d'objets.
3. **`stats`** : points, coupes, changements de fil. Repères observés sur une image de ~300 mm
   à 12 couleurs : 80 000 à 120 000 points, 1 400 à 1 700 coupes. Au-delà de 3 000 coupes, un
   nombre excessif d'objets séparés est en cause (voir `references/diagnostic.md`).
4. **Le rendu**, en regardant la lisibilité des mâts, des contours et des petites zones, pas
   seulement les chiffres.

Le rendu du script est **approximatif** (une couleur par bloc, tatami en lignes pointillées) ;
ce n'est pas le rendu réaliste de l'application. Ne jamais le présenter comme tel, et le dire.
Si une version retouchée existe (`.osp`), lui passer `--palette` (couleurs des objets par ordre
de couture) car sa boîte englobante ne correspond plus à l'image.

## 3. Le fond : la première cause d'échec

Détection automatique (`segmentation::background_candidate`, même règle CLI et bureau) :

- fond **clair** (L\* > 90) qui touche au moins 3 bords ; **ou**
- fond **coloré** (crème, beige, aplat) qui occupe au moins 85 % du pourtour et touche les 4 bords.

Un ciel ou une mer plein cadre n'est **jamais** ignoré (ils ne remplissent pas tout le pourtour).
Le CLI imprime `Région candidate au fond` et `Ignorer la plus grande région : oui/non`. Si ce
n'est pas le bon choix : `--skip-background yes|no`. Cas vécu : fond beige non détecté
(L\* 86) → 299 × 224 mm et +22 % de points, alors que le motif fait 290 × 200 mm.


## 4. Réglages, diagnostic, historique : références à la demande

Ne pas tout charger. Lire le fichier utile, et seulement la plage nécessaire (`rg -n` d'abord).

| Besoin | Fichier (dans `references/`) |
|---|---|
| Choisir ou régler une option (`--dark-lines`, `--island-cleanup`, `--fill-inset`, `--satin-bands`, `--min-region-mm2`, mode `contours`/`stipple`…), comprendre ce qu'apporte une retouche manuelle | `parametres.md` |
| Symptôme précis (pièce trop grande, ciel en pointillés, > 3 000 coupes, interstices, trop de fils) | `diagnostic.md` |
| Mesures et leçons des essais 1–14 (pré-traitements `--pre`, `--flow-fill`, `--rings-satin`, barres, fond vert…) | `journal-essais.md` |

Règle d'or : **un réglage à la fois**, mesuré avant/après avec `stats` (même `--dpi`, même `--max-colors`).
Options à connaître sans ouvrir le fichier : `--dark-lines` (mâts, haubans ; +40–60 % de coupes),
`--island-cleanup 20` (îlots ; −49 % d'objets mesuré), `--fill-inset 0` (interstices ; +16 % de points),
`--satin-bands` (désactivé par défaut : relecture brevet requise, voir `parametres.md`).

## 5. Dans l'application

La boîte *Numérisation automatique* reprend les mêmes réglages (fond, couleurs, satin des
bandes, chevauchement). Ensuite, les retouches se font par commandes annulables : fusion de
zones, étirement de tatami, ordre des couleurs. « Optimiser les trajets » se lance à la fin,
juste avant l'export, jamais avant : il ne modifie ni l'ordre des couleurs ni la couche visible.


## 6. Ce qu'il ne faut pas faire

- Présenter le rendu du script comme le rendu final de l'application.
- Annoncer « plus propre » sans avoir comparé `stats` avant/après au même `--dpi`.
- Activer `--satin-bands` par défaut dans le code sans la relecture brevet.
- Contourner la règle d'or des modules : toute logique de numérisation va dans `libs/autodigitize`
  ou `libs/segmentation` (Qt-free), pas dans `apps/desktop`.
- Écrire des fichiers de travail dans le dépôt (les `.dst`, `.png` d'essai vont au scratchpad).


## 7. Ce skill s'enrichit

Une nouvelle image qui révèle un échec ou une règle : l'ajouter à `references/journal-essais.md` avec **la mesure qui la justifie** (avant/après, image de référence) et dans `docs/source/auto-numerisation.md`. Pas d'hypothèse non vérifiée ; toute erreur de méthode va dans le tableau d'erreurs.
