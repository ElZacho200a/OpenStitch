# Compagnon IA

Public : utilisateurs, intégrateurs, mainteneurs. Le compagnon est un **assistant optionnel** :
un agent IA peut inspecter et, **avec votre approbation**, modifier le projet actuellement ouvert
dans OpenStitch Studio (même non enregistré), avec Annuler/Rétablir fiable. Sans lui, OpenStitch
fonctionne à l'identique. Décisions d'architecture : [ADR 0001](../adr/0001-compagnon-ia-session-live.md)
(session live, révisions, permissions) et [ADR 0002](../adr/0002-fournisseur-claude.md) (fournisseur
Claude). Détail des outils : [Serveur MCP](mcp.md).

## Trois modes d'utilisation

| Mode | Conversation | Connexion au projet ouvert | Prérequis |
| --- | --- | --- | --- |
| **Claude Code dans le dock** | Directement dans OpenStitch (chat) | CLI headless et MCP --live, approbation dans le dock | CLI Claude Code installée et authentifiée ; **pas de clé API OpenStitch** |
| **Claude Code dans un terminal** | Terminal Windows/Linux externe | CLI Claude Code et MCP --live, approbation dans le dock | CLI Claude Code installée et authentifiée |
| **Fournisseur Anthropic dans le dock** | Directement dans OpenStitch (chat) | Agent en processus + pont live, approbation dans le dock | Clé API Anthropic dans le stockage sécurisé |

Pour le parcours recommandé, afficher **Compagnon IA** (Ctrl+Maj+J), puis choisir
**Discuter avec Claude Code ici (via MCP)…**. Le mode terminal reste disponible depuis
le même accueil ou le menu Aide. Le chat API est **un mode différent** : les identifiants
et la facturation de Claude Code ne deviennent pas une clé API Anthropic.

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

## Parcours utilisateur recommandé

1. Ouvrir un projet OSP (ou créer un projet) dans OpenStitch.
2. Afficher le panneau Compagnon IA (Ctrl+Maj+J).
3. Cliquer **Discuter avec Claude Code ici (via MCP)…**. L'application prépare un workspace
   propre à l'instance, puis active son pont local. Aucun terminal n'est nécessaire.
4. Écrire une demande en français, par exemple : « Mets les objets orange compatibles
   en satin avec une compensation de tirage prédictive ».
5. Claude Code lit le contexte du projet ouvert, cible les **couleurs de fil** et non les
   couleurs de segmentation, compare les trois types de satin, construit un aperçu.
6. Le dock affiche la proposition. **Appliquer** valide un lot atomique, **Rejeter**
   ne modifie rien ; **Ctrl+Z** annule le lot appliqué dans OpenStitch.

Le chat intégré démarre la CLI Claude Code avec **QProcess** en mode non interactif
(sortie JSON structurée et reprise par identifiant de session) : le texte de la demande
est fourni via l'entrée standard, pas par un interpréteur de commandes. Les outils
OpenStitch sont accessibles par le MCP local : le chat ne modifie jamais le projet
en écrivant directement un OSP. Le processus headless utilise `--tools ""`,
`--strict-mcp-config`, `--mcp-config` et `--permission-mode dontAsk` pour ne mettre
à disposition que les outils MCP de cette instance OpenStitch.

**Ouvrir Claude Code dans un terminal** est l'option secondaire. Elle conserve les outils,
les skills et les approbations, mais la conversation reste dans le terminal externe.

L’analyse sémantique automatique est désactivée par défaut. Elle peut être activée dans
les préférences et envoie alors des rendus au fournisseur Claude Code lors du lancement.

## Configuration et isolation

OpenStitch crée un espace de travail par **processus Desktop** dans son dossier de données
locales. La configuration de Claude Code contient un serveur MCP nommé openstitch démarré
avec les arguments **--live --pid N**, où N correspond au processus OpenStitch courant.

Le workspace intègre CLAUDE.md, un hook SessionStart et trois skills de travail :
openstitch-live-edit et openstitch-live-satin. En cas de changement de document,
l'historique de conversation du mode intégré est réinitialisé afin d'éviter les
propositions visant le projet précédent.

Le transport local est un tube nommé ou un socket avec découverte restreinte et jeton
de session. Aucune ouverture de port réseau n'est nécessaire. Le projet peut être
non enregistré : les opérations live s'appliquent à son état en mémoire.

## Coûts, données et sécurité

- **Mode Claude Code (intégré ou terminal)** : l'authentification et la facturation
  utilisent votre installation Claude Code. OpenStitch n'a pas besoin d'une clé API
  Anthropic distincte. Les données envoyées à Claude dépendent des autorisations
  et politiques de Claude Code.
- **Mode API Anthropic** : utilise une clé API configurée dans OpenStitch ; coûts
  et quotas API distincts.
- Les demandes de modification live passent uniquement par les outils MCP autorisés.
  Le mode chat CLI autorise les outils du serveur OpenStitch ; l'accès Bash et les
  modifications directes de fichiers sont refusés par la configuration de lancement.
  L'utilisateur conserve l'approbation du changement dans le dock.
- Les requêtes portent une **révision** du document ; si l'utilisateur modifie le
  projet entre le calcul et l'approbation, la proposition devient périmée.
- L'annulation native garde le projet réversible ; le serveur MCP autonome refuse
  par défaut de remplacer un projet non sauvegardé.

Les résultats du calcul de risque textile et le tirage prédictif restent des estimations :
un test de broderie réel est nécessaire.

## Dépannage

| Symptôme | Cause possible | Vérification |
| --- | --- | --- |
| Claude Code non détecté | CLI absente du PATH | Installer Claude Code ou choisir la version native disponible |
| MCP introuvable | Installation incomplète du composant MCP | Vérifier le paquet OpenStitch et le binaire openstitch-mcp |
| Le chat reste en attente | Processus Claude Code bloqué, erreur de démarrage | Cliquer Interrompre et consulter le diagnostic |
| E_SESSION_CLOSED | Desktop fermé, projet ou pont local indisponible | Garder OpenStitch ouvert et activer le pont |
| E_STALE_REVISION | Projet modifié manuellement pendant le calcul | Relire le contexte, proposer un nouveau lot |
| E_APPROVAL_DENIED | Proposition rejetée | Ne pas appliquer, demander une retouche |
| E_AUTH dans le mode API | Clé API erronée | Vérifier Aide ▸ Compagnon IA |
| Aucun message dans le dock après lancement externe | Conversation dans le terminal | Utiliser le terminal ou choisir le chat intégré |

Le bouton **Configurer Claude…** concerne uniquement le fournisseur API. Une validation de ses
préférences ramène au mode API ; il reste possible de choisir de nouveau Claude Code.

## Critères de recette obligatoires

Tests automatisés : Catch2 test_mcp et test_agent, QTest test_live_server,
test_live_bridge_ui, test_companion_dock et test_claude_backend, incluant
le parseur JSON du mode headless et l'activation du chat sans clé API.

Tests **manuels** avant fusion : sur une installation Windows propre, avec Claude Code
réellement installé et authentifié, vérifier le dialogue depuis le dock, le chargement
du MCP, une demande comportant plusieurs outils, les approbations, le refus, Ctrl+Z,
le changement de document en cours de session, le lancement terminal et l'isolation
entre deux processus OpenStitch.

La présence des tests simulés et du code n'atteste pas à elle seule du fonctionnement
avec une version particulière de la CLI Claude Code. Si l'essai réel échoue, la PR doit
rester en brouillon jusqu'à correction.

## Installation et désinstallation

L'installeur copie le binaire MCP à côté de l'application. Il ne fournit pas la CLI
Claude Code ni vos identifiants. La désinstallation retire les fichiers de découverte
du pont et la clé API du stockage sécurisé Windows.
