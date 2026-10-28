# Compagnon IA

Public : utilisateurs, intégrateurs, mainteneurs. Le compagnon est un **assistant optionnel** :
un agent IA peut inspecter et, **avec votre approbation**, modifier le projet actuellement ouvert
dans OpenStitch Studio (même non enregistré), avec Annuler/Rétablir fiable. Sans lui, OpenStitch
fonctionne à l'identique. Décisions d'architecture : [ADR 0001](../adr/0001-compagnon-ia-session-live.md)
(session live, révisions, permissions) et [ADR 0002](../adr/0002-fournisseur-claude.md) (fournisseur
Claude). Détail des outils : [Serveur MCP](mcp.md).

## Deux façons de l'utiliser

| Mode | Qui parle à qui | Mise en route |
|---|---|---|
| **Agent externe** (Claude Code, autre client MCP) | le client lance `openstitch-mcp --live`, qui relaie au desktop | **Aide ▸ Pont IA local (MCP)** puis, dans le client MCP : `"command": "…\mcp\openstitch-mcp.exe", "args": ["--live"]` |
| **Dock intégré** (Claude via votre clé API) | le dock appelle l'API Anthropic ; l'agent utilise les outils en processus | **Aide ▸ Compagnon IA…** : clé API, « Activer le fournisseur Claude » ; ouvrir **Affichage ▸ Panneaux ▸ Compagnon IA** (`Ctrl+Maj+J`) |

Les deux modes partagent les mêmes outils, la même politique de permissions et le même point
d'approbation (le dock).

## Ce que l'agent peut faire

- **Lire** (automatique, sur une copie, sans bloquer l'interface) : `live_status`, `live_context`,
  `live_target`, `project_inspect`, `project_palette`, `project_select`, `project_check`,
  `project_render`, `project_satin_compare`, `project_fabric_risk`, `live_plan_satin`,
  `live_preview`…
- **Demander** : `live_ask_user` pose une question dans le dock (cas ambigu) ; rien n'est modifié
  avant votre réponse.
- **Modifier** : seulement `project_apply`, après **votre approbation** (liste des opérations,
  avertissements), en **un seul pas d'annulation**. Une modification manuelle entre-temps donne
  `E_STALE_REVISION` : rien n'est écrasé.
- **Jamais** : ouvrir, créer, numériser, enregistrer ou exporter un fichier ; exécuter une
  commande ; lire le disque ; ouvrir un port réseau.

Exemple : « Mets toutes les formes orange compatibles en satin avec un tirage prédictif » →
l'agent cible les **fils** orange (pas les teintes de segmentation), vous demande si des fils à la
frontière rouge/orange doivent être inclus, compare `skeleton` / `parallel_sweep` / `medial_axis`,
justifie ses refus (le tatami est conservé), montre l'avant/après, puis attend votre approbation.
`Predicted` est une compensation de tirage **empirique, non calibrée sur machine** : un essai sur
tissu reste nécessaire.

## Politique de données — ce qui quitte la machine

| Situation | Données transmises |
|---|---|
| Pont IA local seul (agent externe) | **Rien** par OpenStitch : le tube nommé est local (accessible à votre compte seul, jeton aléatoire par session, aucun port réseau). Ce que votre client MCP envoie à son propre fournisseur dépend de ce client. |
| Fournisseur Claude du dock activé | À chaque message que **vous** envoyez : votre texte, les résultats d'outils (statistiques, listes d'objets, aperçus de rendu PNG) et votre clé API (en-tête HTTPS) vers `api.anthropic.com`. Rien n'est envoyé tant que vous n'écrivez pas. |
| Journal de diagnostic (désactivé par défaut) | Reste en mémoire sur votre machine ; clés et chemins masqués; contenu des messages **jamais** inclus sans case dédiée cochée. |

La **clé API** est stockée dans le Gestionnaire d'identifiants Windows (jamais dans un fichier,
`QSettings`, un projet `.osp` ni un journal) et retirée à la désinstallation. Aucun abonnement
Claude grand public n'est utilisable : une clé API est nécessaire. **Désactiver entièrement le
compagnon** (Aide ▸ Compagnon IA…) coupe le fournisseur et le pont local.

## Coûts et usage

Le dock affiche le fournisseur et les jetons consommés par la demande (entrée/sortie). Le coût en
euros dépend de votre compte Anthropic (consultez votre console). Limites de sécurité :
12 allers - retours d 'outils par demande, résultats d' outils tronqués à 60 000 caractères,
    reprises réseau bornées(≤ 5, recul exponentiel),
    bouton **Interrompre **immédiat.

        ##Dépannage

        | Symptôme | Cause probable | Action | | -- - | -- - | -- - |
        | « OpenStitch Desktop est injoignable » (`E_SESSION_CLOSED`) |
        application fermée ou pont inactif | ouvrir OpenStitch Studio,
    cocher **Aide ▸ Pont IA local **,
    relancer le client MCP | | `E_UNAUTHORIZED` | jeton périmé(application relancée) |
        relancer le client MCP
    : il relit le fichier de découverte
      |
      | `E_STALE_REVISION` | le projet a changé pendant le calcul |
      l'agent doit relire `live_status`/`live_context` puis recommencer | | `E_APPROVAL_DENIED` |
      vous avez rejeté la modification | normal;
reformulez la demande | | « Clé API refusée » (`E_AUTH`) | clé absente, révoquée ou fausse |
                                                                            Aide ▸ Compagnon IA…
    : enregistrer une clé valide
      |
      | Quota / limite de débit(`E_QUOTA`) | limite chez le fournisseur | attendre;
OpenStitch reste utilisable | | Réseau / délai(`E_NETWORK`, `E_TIMEOUT`) | hors ligne
    | le dock reste sur l'accueil ; mode hors ligne = édition normale | |
    Le dock affiche l'accueil | aucun fournisseur actif | configurer Claude, ou utiliser le pont avec un agent externe |

    Pour un rapport de bug : activer le** journal de diagnostic**(Aide ▸ Compagnon IA…),
    reproduire,
    **Copier le journal**(clés et chemins masqués)
            .

        ##Protocole de tests(mainteneurs)

        - `test_agent` (Catch2)
    : fournisseur Anthropic avec faux HTTP(erreurs, reprises, secrets),
boucle d'agent (allow/deny, interruption, limites), journal de diagnostic. - `test_mcp` (`[live]`, `[agentic]`, `
                                                                                             [bench]`)
    : politique d'outils, protocole hôte/relais, clients concurrents,
fermeture / remplacement du document en cours d'appel, ciblage, plan satin, aperçu avant / après,
performances sur 500 objets.- QTest
    headless(`test_live_bridge_ui`, `test_live_server` avec * *vrai tube nommé**,
  `test_companion_dock`, `test_claude_backend` dont le scénario de bout en bout
  `targetPhraseWorksEndToEndFromTheDesktop`)
    : aucun réseau,
aucune clé, fournisseur simulé.- Garde `check_companion_security` : aucun port réseau,
    aucune exécution de commande,
    aucun secret dans `QSettings` dans `libs / agent`, `libs / mcp` et les fichiers desktop du
                                                               compagnon.-
                                                           Mode MCP autonome et `--doctor`
    : non
      -
      régression à chaque étape(`test_mcp`, CLI)
          .

      ##Performances

      Mesure de référence(build** Debug**, 500 objets, machine de développement)
    : instantané du
      projet(copie tenue sur le fil
                 d 'interface) et `live_context` / `live_target` (hors fil d' interface)
          restent sous quelques centaines de millisecondes;
les tests `[bench]` échouent au - delà de * * 2 s **(instantané)et * * 5 s **(outils),
    seuils volontairement larges pour détecter une explosion plutôt qu
    'un écart. Le calcul d' un outil ne bloque jamais l 'interface : seuls l' instantané,
    l'approbation et l 'installation du résultat passent par le fil d' interface.

    ##Installation et désinstallation

    L'installeur Windows copie `openstitch-mcp.exe` et une notice dans `mcp\` à côté de l' application(
        aucun environnement développeur requis);
la désinstallation supprime les fichiers de découverte du pont(`% LOCALAPPDATA %\OpenStitch\live`)
    et la clé API du Gestionnaire d'identifiants. Aucune bibliothèque tierce ni agent externe n'est embarqué par le compagnon.
