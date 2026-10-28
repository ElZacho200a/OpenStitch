# ADR 0001 — Compagnon IA : contrat de session live, révisions et permissions

Statut : **proposée** (issue #173, épopée #172). À relire avant le code des tickets 1 et 2.
Public : mainteneurs, auteurs du pont live, du serveur MCP live, du dock Qt et du fournisseur.

## 1. Contexte et décision

`openstitch-mcp` (voir `docs/source/mcp.md`) travaille sur **sa propre copie** du projet
(`mcp::Session::project`). Le desktop garde la sienne (`MainWindow::project_` + `UndoStack`).
Brancher un agent sur le projet ouvert exige de choisir **un seul maître**.

**Décision.** Le document actif du desktop est le seul maître des mutations en mode live.
Le serveur MCP live n'a **aucune** copie durable : il demande au desktop un instantané en lecture
seule, calcule (dry-run, qualité, rendu) sur cet instantané, et ne mute que par un
`commit` que le desktop exécute sur son propre `UndoStack`.
Le mode autonome (`openstitch-mcp` stdio, `project_open`, `project_apply`…) reste **strictement
inchangé** ; le mode live est un second type de session, négocié explicitement (§5).

Invariants de `CLAUDE.md` respectés : Qt uniquement dans `apps/desktop` ; toute mutation par
`commands::ICommand` ; points jamais stockés (`effective_sequence`) ; déterminisme.

## 2. Flux

```
 Agent (Claude/autre)          openstitch-mcp --live            OpenStitch Desktop (Qt)
 ────────────────────          ────────────────────           ───────────────────────
  tools/call ───────────────▶  relais stdio MCP  ──IPC local──▶  LiveHost (GUI thread)
                                  (Qt-free)       (pipe nommé)      │
                                                                    ▼
                                                   libs/project_ops::LiveBridge   (Qt-free)
                                                    ├─ snapshot / preview (copie)
                                                    └─ commit → UndoStack du desktop
                                                                    │
                                                                    ▼
                                                   MainWindow::project_ (maître unique)
```

- `LiveBridge` (`libs/project_ops`, sans Qt) porte `session_id`, `revision`, instantané,
  prévisualisation et commit. Le desktop lui prête `Project&` et `UndoStack&`.
- `LiveHost` (`apps/desktop`) : transport IPC, sérialisation GUI-thread, événements Qt.
- Le relais stdio ne contient **aucune logique métier** : il traduit JSON-RPC MCP ↔ messages IPC.

## 3. Session, révision, identifiants

- `session_id` : chaîne aléatoire éphémère (128 bits, hex) créée à l'ouverture du pont, **un
  par processus desktop et par document ouvert** ; un `nouveau`/`ouvrir` crée un nouveau
  `session_id` (les anciens passent en `E_SESSION_CLOSED`).
- `revision` : entier 64 bits **monotone** par session, incrémenté à chaque changement du
  document visible : commande exécutée, **Undo, Redo**, restauration d'autosave, remplacement du
  projet. Un changement de sélection n'incrémente pas la révision du document mais une
  `selection_revision` séparée. Enregistrer n'incrémente rien (le document est identique).
- Toute lecture renvoie `{session_id, revision, selection_revision}` ; toute mutation envoie
  la `revision` lue et échoue en `E_STALE_REVISION` si elle diffère. Aucune fusion silencieuse.

## 4. Contrats JSON versionnés (`openstitch.live/1`)

Enveloppe commune : `{"schema":"openstitch.live/1","session_id":"…","revision":N, …}`.
Erreurs : `{"error":{"code":"E_…","message":"…","hint":"…"}}` (codes stables, jamais renommés).

| Message | Entrée | Sortie |
|---|---|---|
| `live_attach` | `{client, versions:[1], pid}` | `{session_id, revision, version:1, document:{name, dirty, never_saved, object_count, canvas_mm}, permissions}` |
| `live_snapshot` | `{session_id}` | idem + `selection:[ids]`, `active_thread` ; le **projet complet** reste côté desktop : les outils lecture (`project_inspect`, `project_palette`, `project_select`, `project_check`, `project_render`, `project_satin_compare`, `project_fabric_risk`) s'exécutent sur une copie `shared_ptr<const Project>` figée à cette révision |
| `live_selection` | `{session_id}` | `{selection:[ids], selection_revision}` |
| `live_preview_batch` | `{session_id, revision, batch}` | rapport `openstitch.batch-report/1` (dry-run), **sans** toucher dirty ni Undo ; `preview_id` valable tant que `revision` ne change pas |
| `live_commit_batch` | `{session_id, revision, batch, preview_id?, label}` | `{revision, applied, report}` ; exige l'approbation (§6) |
| `live_events` | `{session_id, since}` | `{events:[{seq, type, revision}], next}` ; types : `document_changed`, `selection_changed`, `session_closed`, `approval_pending`, `approval_resolved` |
| `live_detach` | `{session_id}` | `{}` ; libère les previews du client, jamais le document |

Codes : `E_STALE_REVISION` (révision périmée : relire un snapshot), `E_SESSION_CLOSED`
(document remplacé/app fermée), `E_APPROVAL_REQUIRED` (mutation sans approbation valide),
`E_APPROVAL_DENIED` (rejetée par l'utilisateur), `E_BUSY` (réentrance : un commit ou un dialogue
modal est en cours), `E_LOCKED` (objet verrouillé touché), `E_UNSUPPORTED_VERSION`, plus les codes
de `project_ops::code`.

## 5. Transport local

- IPC **hôte desktop** : pipe nommé Windows / socket de domaine Unix (`QLocalServer`), nom
  dérivé du `session_id` du processus, **jamais** de port TCP/HTTP. Droits : ACL limitée à
  l'utilisateur courant (Windows : SID de l'utilisateur ; Unix : `0600`).
- Authentification : jeton aléatoire par instance, écrit dans un fichier lisible par l'utilisateur
  seul (`%LOCALAPPDATA%\OpenStitch\live\<pid>.json` : nom du pipe, jeton, version) ; vérification du
  PID client quand l'OS le permet. Un client sans jeton valide est refusé avant toute lecture.
- `openstitch-mcp --live` est un **relais stdio** : mêmes outils MCP que le mode autonome, mais les
  appels sont exécutés sur le projet du desktop. Sans desktop joignable : erreur
  `E_SESSION_CLOSED` avec conseil actionnable (« ouvrez OpenStitch Studio »), jamais de repli
  silencieux sur un autre projet. `openstitch-mcp` sans `--live` et `--doctor` ne changent pas.
- Compatibilité : le relais déclare la version du contrat ; le desktop répond `E_UNSUPPORTED_VERSION`
  avec les versions supportées si aucune ne convient.

## 6. Permissions par outil

| Classe | Outils | Politique |
|---|---|---|
| Lecture | `project_inspect`, `project_palette`, `project_select`, `project_check`, `project_render`, `project_satin_compare`, `project_fabric_risk`, `project_operations`, `live_snapshot/selection/events` | **auto** (aucune confirmation, aucune mutation) |
| Mutation | `project_apply` (hors dry-run), `live_commit_batch`, `project_restore` | **approbation explicite** par lot dans le dock (liste d'objets, avant/après) ; `dry_run` libre |
| Fichier / cycle de vie | `project_open`, `project_new`, `project_digitize`, `project_save`, export DST/PES | **refusés** en mode live par défaut ; accessibles seulement via action explicite de l'utilisateur dans le desktop |
| Interdits | exécution de commande, lecture du disque hors projet, réseau | jamais exposés |

L'approbation est liée à `(session_id, revision, hash(batch))` : un changement manuel entre
l'aperçu et l'approbation invalide l'approbation (`E_STALE_REVISION`). Un client ne peut ni
approuver, ni élargir ses propres droits ; `permissions` de `live_attach` est calculé par le desktop.

## 7. Propriétaires, invalidation, cycle de vie

| Objet | Propriétaire | Invalidé quand |
|---|---|---|
| Document, Undo/Redo, dirty | desktop (`MainWindow`) | — |
| Checkpoints de l'agent | session MCP live (mémoire du relais, **hors** document et hors `.osp`) | changement de `session_id` ; jamais restaurés sur une autre révision sans relecture |
| Previews (`preview_id`) | `LiveBridge` | toute évolution de `revision`, `live_detach` |
| Historique du chat | dock (fournisseur) ; jamais dans le `.osp` | fermeture du dock/projet (l'utilisateur peut l'exporter) |

Cas limites : fermeture de l'app → `E_SESSION_CLOSED` aux clients en vol, aucune mutation à moitié ;
agent interrompu → les previews sont abandonnées, rien n'est commité ; nouveau document → nouvelle
`session_id` et événement `session_closed` ; édition manuelle concurrente → `E_STALE_REVISION` au
commit suivant ; autosave/restore → `revision` incrémentée si l'état visible change.

## 8. Table des états de la session live

| État | Entrée | Sorties |
|---|---|---|
| `detached` | défaut | `live_attach` → `attached` |
| `attached` | attach valide | `preview` → `previewed` ; document change → `attached` (revision+1) ; nouveau doc → `closed` |
| `previewed` | preview réussi | commit approuvé → `committing` ; revision change → `attached` ; detach → `detached` |
| `committing` | approbation reçue | succès → `attached` (revision+1, 1 pas Undo) ; échec → `attached` (inchangé) |
| `closed` | document/app fermé | tout appel → `E_SESSION_CLOSED` ; `live_attach` → nouvelle session |

## 9. Parcours spécifiés

**A. Modification simple.** attach → snapshot (rev 7) → preview(rev 7, lot) OK → commit(rev 7) avec
approbation → desktop exécute le lot (§10) → rev 8, canvas rafraîchi, dirty=true.

**B. Dry-run + commit + Undo.** preview → rapport de refus éventuels sans effet ; commit → un seul
pas d'annulation nommé `IA : <label>` ; `Ctrl+Z` → état identique octet pour octet, rev 9 ;
`Ctrl+Y` → rev 10, état identique au commit.

**C. Modification concurrente.** snapshot (rev 7) ; l'utilisateur déplace une forme (rev 8) ;
commit(rev 7) → `E_STALE_REVISION`, aucun changement ; l'agent relit un snapshot (rev 8) et
recompose.

```
Agent        Relais       LiveHost/Bridge          Desktop (GUI)        Utilisateur
  │ snapshot ──▶│──────────▶ rev 7 ─────────────────▶│                     │
  │ preview  ──▶│──────────▶ dry-run sur copie (rev 7), rien ne change    │
  │ commit   ──▶│──────────▶ approval_pending ───────▶ dock : avant/après ▶│ approuve
  │             │            vérifie rev==7, exécute ReplaceProject/Composite sur UndoStack
  │◀── rev 8 ───│◀──────────  événement document_changed ─▶ rafraîchit canvas, inspecteur, dirty
```

## 10. Mutation sur la pile du desktop : options comparées

`apply_batch` simule sur `work = project` puis `project = std::move(work)` hors commandes. Cela
casserait l'Undo et le dirty du desktop. Deux options :

- **A. Rejouer le lot sous `CompositeCommand`** : récupérer les `ICommand` du lot et les exécuter
  sur le projet vivant. *+* mémoire minimale, historique fin. *−* exige que chaque commande soit
  rejouable depuis un projet distinct de celui où elle fut créée (identifiants alloués, états
  capturés au premier `apply`) : risque de divergence silencieuse.
- **B. `ReplaceProjectCommand` atomique** : le lot est validé et exécuté sur une copie (sémantique
  actuelle), le résultat est installé par **une** commande portant `avant`/`après` (instantanés
  immuables partagés). `apply` installe `après`, `revert` rétablit `avant` : exactitude garantie,
  identifiants et sélection cohérents, un seul pas d'annulation. *−* mémoire d'un projet par lot
  (acceptable : les points ne sont jamais stockés ; l'image source est partagée par `shared_ptr`
  quand c'est possible).

**Choix : B pour le MVP** (garantie forte, aucun risque de divergence), A restant une
optimisation ultérieure derrière la même interface `LiveBridge::commit`. Le desktop invalide caches
de points/aperçu, sélection (retirée des identifiants disparus), propriétés, ordre, palette,
statistiques après chaque installation, comme pour Undo/Redo.

## 11. Plan de migration

1. Ticket 1 : `commands::ReplaceProjectCommand`, `project_ops::LiveBridge`, câblage `MainWindow`,
   tests (Catch2 + QTest). Aucun changement des outils MCP.
2. Ticket 2 : relais `--live`, serveur IPC dans le desktop, outils lecture/écriture live.
3. Tickets 3–6 : dock, fournisseur, parcours agentique, packaging.
CLI `project apply` et MCP autonome : mêmes tests de non-régression à chaque étape.

## 12. Registre des risques

| Risque | Gravité | Parade |
|---|---|---|
| Deux maîtres du document | critique | pas de copie durable côté MCP live ; révision vérifiée à chaque mutation |
| Écrasement d'une édition manuelle | élevé | `E_STALE_REVISION`, approbation liée à `(revision, hash)` |
| Pipe accessible à un autre utilisateur/processus | élevé | ACL utilisateur, jeton par instance, pas de port TCP |
| Réentrance GUI (commit pendant dialogue modal/calcul long) | moyen | file sérialisée sur le fil GUI, `E_BUSY` |
| Instantané trop coûteux (centaines d'objets) | moyen | copie différée/partagée, mesure en ticket 6 |
| Mutation par fichier/export sans consentement | élevé | classe « Fichier » refusée en live |
| Divergence CLI/MCP vs live | moyen | mêmes `project_ops` ; tests de parité |
| Fuite de secrets/chemins vers le fournisseur | élevé | politique de données et journal masqué (tickets 4 et 6) |
