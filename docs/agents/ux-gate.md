# UX Gate — prévention des défauts ergonomiques du Studio

## Pourquoi

Une UI qui compile et passe QTest peut malgré tout cacher le motif sous un panneau,
afficher un bouton inaccessible ou imposer un parcours incompréhensible. Le **critère
premier est l'expérience d'une personne qui découvre la broderie**. Ce protocole
réutilise QA-STUDIO-001, les contrôles existants et le skill openstitch-studio-ux.

Il s'applique aux modifications de production dans apps/desktop_v2/studio/,
apps/desktop_v2/core/ et resources/themes/. Pas aux simples changements de
documentation, de tests ou d'outillage. Les changements moteur qui introduisent une
nouvelle action Studio doivent aussi appliquer ce protocole lors de leur câblage UI.

## Trois portes de qualité

1. **Avant d'implémenter.** L'agent écrit un scénario novice ("je veux corriger une
   fleur après import ORA"), son chemin idéal et son résultat. Nombre de clics
   visé et possibilité d'annuler/rééditer. Relire le composant de l'espace voisin,
   garder les paramètres avancés hors du parcours simple.
2. **Après l'implémentation.** QTest ciblés et captures réelles à deux tailles sur
   les quatre espaces. Les captures peuvent révéler un rognage, un panneau
   occultant le travail, des libellés incohérents, mais ne prouvent pas l'action
   d'un bouton : les QTest et les gestes réels restent nécessaires.
3. **Revue contradictoire.** Utiliser le sous-agent os-ux-reviewer, **distinct de
   l'implémenteur**. Lui donner l'objectif du débutant, pas le patch de l'auteur.
   Il inspecte les PNG puis vérifie le parcours. Les frictions P0/P1/P2
   documentées doivent être corrigées avant un verdict PASS. Relier les constats
   au registre QA existant et ajouter des tests de non-régression.

## Captures sous Windows

Le Studio comporte déjà les paramètres --screenshot, --open-space, --window-size,
--open-panels. Réutiliser un OSP réel (et la configuration Qt/VS/CMake habituelle).

Depuis la racine du dépôt dans PowerShell :

    python scripts/ux/verify.py capture --studio build/msvc/apps/desktop_v2/studio/Release/openstitch.exe --project C:/chemin/motif.osp --out build/ux-gate/latest --backend native

Pour un repli software si la carte ou le contexte GPU n'est pas disponible :

    python scripts/ux/verify.py capture --studio build/msvc/apps/desktop_v2/studio/Release/openstitch.exe --project C:/chemin/motif.osp --out build/ux-gate/latest --backend software

Le script supprime chaque ancienne capture avant son essai, attend chaque processus,
contrôle l'intégrité CRC/structure du PNG, sa taille attendue et détecte les
captures identiques pour les espaces différents. Il écrit report.json avec
le HEAD, une empreinte de l'arbre de travail, les fichiers et leurs SHA256.

**Limite importante :** il ne sait pas juger la beauté ou la découvrabilité.
La CI Windows utilise offscreen/software ; le reviewer doit signaler si une
recette native GPU et des interactions au clic restent à faire.

## Consigner la relecture indépendante

Lancer os-ux-reviewer sur les huit PNG et le scénario utilisateur. Une fois
son rapport reçu et ses frictions critiques corrigées, consigner le verdict
réel, par exemple :

    python scripts/ux/verify.py review --report build/ux-gate/latest/report.json --reviewer "os-ux-reviewer/session-123" --verdict PASS --notes "Les 4 espaces et les 2 tailles ont ete examines ; parcours novice verifie, pas de P0/P1/P2."

Puis vérifier :

    python scripts/ux/verify.py check-report --report build/ux-gate/latest/report.json --require-review

Ne renseigner PASS ni nom d'agent sans revue réellement réalisée. Une capture
software ne vaut pas preuve d'une inspection GPU native. Le rapport est local
(non versionné) et doit être régénéré après chaque changement du HEAD ou du
working tree. Une PR doit pointer vers les **captures inspectées** (artefact
CI ou capture publiée) et le compte rendu du relecteur.

## Hooks Claude Code

- SessionStart : sauvegarde un instantané des fichiers UI de la session dans
  build/ux-gate/sessions/.
- Stop : si les fichiers UI ont effectivement changé pendant cette session,
  vérifie le manifeste et la revue via le même script. Sans preuve, Claude
  reçoit une instruction pour poursuivre. Pour éviter une boucle infinie
  lorsque l'outil ne peut pas lancer le Studio, le hook ne rebloque pas un
  arrêt déjà provoqué par un Stop hook.
- Les hooks fournissent un filet de sécurité, **pas une preuve** que Claude a
  lancé un autre agent. Si l'environnement ne permet pas la recette, signaler
  explicitement BLOCKED et laisser la PR en brouillon.

La configuration est dans .claude/settings.json. Redémarrer une session Claude
Code pour charger les nouveaux hooks.

## CI et PR

- .github/workflows/ci.yml : étend le screenshot Studio existant à huit prises
  (quatre espaces × deux fenêtres) et publie les PNG + report.json dans
  l'artefact openstitch-studio-screenshots-windows.
- .github/workflows/ux-gate.yml : exécute les tests unitaires Python puis exige
  la section "## UX Gate" dans la description des PR touchant une UI.
- Les cinq champs sont : Parcours novice, Clics avant/après (nombres),
  Captures inspectées, Revue indépendante (agent + verdict), Tests UX.
  La CI vérifie leur présence et rejette les placeholders, mais **elle ne
  prouve pas l'honnêteté de la revue**.
- Pour rendre ce contrôle bloquant au merge au niveau de GitHub, activer le
  check obligatoire **UX Gate — Studio / ux-contract** dans les règles de branche.
  Le dépôt conserve la règle existante : ne jamais merger une CI rouge.

Une PR ne doit pas être marquée Validé sur Notion sur la seule base d'un merge :
le test physique du textile, quand nécessaire, reste séparé. Réutiliser
le chantier et les IDs du registre QA-STUDIO-001 au lieu de créer un doublon.

## Critères de réussite

- Moins de retours humains post-merge sur les chevauchements, le langage, les
  contrôles inactifs et les parcours trop longs.
- Pour chaque PR UI : un scénario novice, clics mesurés, captures inspectées,
  relecteur indépendant, QTest ciblés et preuve de la révision.
- Tout défaut récurrent donne lieu à un test de non-régression, pas seulement
  à une note de review.

Ce premier gate ne fournit **ni robot de clics complet ni critique visuelle IA
automatisée dans GitHub Actions**. L'agent local effectue la revue ; un futur
runner avec accès à un modèle vision pourrait effectuer une passe supplémentaire.
