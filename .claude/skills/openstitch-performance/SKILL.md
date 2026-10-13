---
name: openstitch-performance
description: Écrire, mesurer et paralléliser du code OpenStitch sans perte de qualité ni de déterminisme (banc de mesure, `parallel_for`, calcul hors du fil d'interface, caches à clé exacte). À utiliser dès qu'on écrit ou modifie une boucle sur des régions, des objets, des pixels ou des points ; avant d'ajouter une étape au pipeline image → points ; quand l'utilisateur parle de lenteur, de gel de l'interface, d'optimisation, de performance ou de multi-threading ; et avant d'annoncer un gain de vitesse.
---

# Performance et parallélisme OpenStitch

Trois règles, dans cet ordre :

1. **Mesurer avant, mesurer après.** Aucun gain n'est annoncé sans chiffre du
   banc, en Release, à chaud.
2. **Sortie identique octet pour octet.** Une accélération ne change ni la
   résolution, ni la densité, ni l'ordre : les empreintes du banc doivent être
   les mêmes avant et après. Sinon ce n'est pas une optimisation, c'est un
   changement de comportement (à traiter comme tel).
3. **Paralléliser ce qui est indépendant, assembler en séquence.**

Référence chiffrée et historique : `docs/performance-audit.md`.

## 1. Mesurer

```powershell
cmake --build --preset msvc-release --target openstitch-bench openstitch-bench-ui
build\msvc\tests\bench\Release\openstitch-bench.exe tests\fixtures\tentabrode.png --reps 4 --save-osp t.osp
build\msvc\tests\bench\Release\openstitch-bench.exe tests\fixtures\tentabrode.png --reps 2 --satin-bands
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"; $env:QT_QPA_PLATFORM = "offscreen"
build\msvc\tests\bench\Release\openstitch-bench-ui.exe t.osp --reps 5
```

- Lire la colonne **chaud med** ; « froid » inclut les caches vides.
- Noter les lignes `empreinte segmentation` et `empreinte séquence effective`
  **avant** de modifier quoi que ce soit, et les comparer après.
- `$env:OPENSTITCH_THREADS = "1"` force le séquentiel : c'est la mesure de
  référence pour juger un gain de parallélisme (et pour diagnostiquer).
- Jamais de chronomètre dans un test CTest : un temps n'est pas un critère de
  réussite. Une nouvelle étape coûteuse s'ajoute au banc
  (`tests/bench/openstitch_bench.cpp`), pas à la suite de tests.
- Un seul build ou banc à la fois : deux en parallèle faussent les temps.

## 2. Paralléliser : `openstitch::parallel_for`

`libs/core/include/openstitch/core/parallel.hpp` (sans Qt, utilisable dans
toutes les libs). Modèle obligatoire, en trois phases :

```cpp
// 1. Séquentiel : lister les unités de travail (ordre déterministe).
// 2. Parallèle : chaque tâche LIT des données constantes et n'écrit que dans SA case.
std::vector<Out> slots(items.size());
parallel_for(items.size(), [&](std::size_t i) { slots[i] = compute(items[i]); });
// 3. Séquentiel : identifiants, insertion dans le document, ordre de couture.
```

Règles :

- **Aucun identifiant attribué dans la phase parallèle** (`ids.next()`,
  `push_back` dans un conteneur partagé, compteur) : tout ce qui dépend de
  l'ordre se fait en phase 3. Exemples : `auto_digitize`
  (`libs/autodigitize/src/autodigitize.cpp`), `generate_sequence`
  (`libs/stitch_generation/src/generate.cpp`).
- La tâche ne doit pas modifier ce qu'elle lit : passer par une référence
  `const` (`const Segmentation& constSeg = seg;`) pour que le compilateur le
  vérifie.
- `Result<T>` n'est pas constructible par défaut : utiliser
  `std::vector<std::optional<Result<T>>>` (cf. `vectorize_regions`).
- État partagé interdit, sauf cache protégé par mutex dont la valeur est une
  fonction pure de la clé (cf. cache du squelette, `skeleton_satin.cpp`).
- **Granularité.** `parallel_for` crée ses fils à chaque appel (~1 ms). Une
  tâche = une région, un objet, une classe de couleur ou une ligne d'image.
  Ne pas l'appeler dans une boucle serrée ni pour une recopie simple : le coût
  des fils dépasserait le gain (la recopie des étiquettes de `segment` est
  restée séquentielle pour cette raison).
- **Mémoire.** Si chaque tâche alloue une image entière, borner le nombre de
  tâches simultanées par lots (cf. composantes connexes dans
  `segmentation.cpp`, plafond ~256 Mio).
- Proposer une variante par lot dans la lib plutôt qu'une boucle chez
  l'appelant : `vectorization::vectorize_regions` au lieu d'une boucle sur
  `vectorize_region`.
- OpenCV est compilé **sans** backend parallèle dans ce projet : ses filtres
  sont séquentiels, c'est à nous de répartir (par classe, par ligne).

Test obligatoire pour toute fonction parallélisée : **même résultat avec
`OPENSTITCH_THREADS=1` et `=8`** (modèle : « meme resultat avec un seul fil et
avec plusieurs » dans `test_segmentation.cpp` et `test_autodigitize.cpp`).

## 3. Ne pas geler l'interface

- Tout calcul qui peut dépasser ~100 ms passe par `BusyIndicator::run`
  (`apps/desktop/main_window.cpp`) : il s'exécute dans un fil de travail,
  l'interface continue de peindre. Le calcul ne lit qu'un instantané que
  l'interface ne modifie pas pendant l'attente ; suspendre les minuteurs qui
  lisent le document (sauvegarde automatique, simulation).
- La mutation du document reste sur le fil de l'interface, via une
  `ICommand`, **après** le retour du calcul.
- Code de génération = fonctions pures sur des instantanés immuables.
- Ce qui tourne sur un minuteur (sauvegarde automatique toutes les 2 min)
  compte comme interactif : viser < 50 ms.

## 4. Éviter le travail inutile

- **Cache à clé de contenu exacte** (comparaison des octets ou clé
  structurelle complète), jamais une simple adresse ni une empreinte seule ;
  taille bornée ; un test prouve l'invalidation (modèle : PNG de
  `save_project`, test « le PNG en cache suit le contenu de l'image »).
- Ne pas recalculer dans une boucle ce qui ne dépend pas de l'itération
  (aires, boîtes englobantes, recherche linéaire d'un objet par identifiant).
- Parcourir une image par pointeur de ligne (`mat.ptr<T>(y)`), pas
  `at<T>(y, x)` avec division/modulo par pixel.
- Ne pas recompresser des données déjà compressées (PNG stocké tel quel dans
  le `.osp`).
- Complexité : une passe par région sur l'image entière est interdite
  (O(régions × pixels)) ; garder la liste des pixels ou une boîte englobante.
- Réserver (`reserve`) les conteneurs dont la taille est connue ; déplacer
  (`std::move`) les gros résultats au lieu de les copier.

## 5. Avant de conclure

1. Banc avant/après dans le message de commit ou la PR (étape, ms avant, ms
   après, empreintes identiques).
2. `ctest --preset msvc-release -j 8` et `ctest --preset msvc-debug` verts.
3. `linux-core` : rien de Qt ni de Windows dans le cœur (`parallel.hpp`
   n'utilise que la bibliothèque standard).
4. Mettre à jour `docs/performance-audit.md` (section datée : mesures,
   corrections, hypothèses écartées avec leurs chiffres).
