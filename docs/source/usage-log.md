# Journal d'usage local (KPI)

Studio écrit, **sur votre ordinateur seulement**, un journal des outils et des commandes utilisés.
Il sert à savoir comment OpenStitch est réellement utilisé (quels outils, quelles commandes,
combien de sessions) sans rien envoyer nulle part. Aucun code, aucun service réseau : un fichier
texte et un script de lecture.

## Où est le journal

| Système | Dossier par défaut |
|---|---|
| Windows | `%LOCALAPPDATA%\OpenStitch\usage` |
| Linux | `~/.local/share/OpenStitch/usage` |
| macOS | `~/Library/Application Support/OpenStitch/usage` |

Le dossier ne dépend ni du nom de l'application ni de sa version : **les données s'accumulent d'une
version à l'autre** et sont partagées par toutes les installations de la machine. Il est hors du
dépôt : rien n'est committé. Un fichier par mois : `usage-AAAA-MM.jsonl` (jamais supprimé ni réécrit,
les événements sont seulement ajoutés).

Pour regrouper plusieurs machines ou un dossier partagé d'équipe, définir `OPENSTITCH_USAGE_DIR`
avant le lancement (Studio écrit alors dans ce dossier), ou copier les fichiers `usage-*.jsonl`
dans un même dossier.

## Format

Une ligne JSON par événement :

```json
{"v":1,"t":"2026-11-12T10:00:00.123","sid":"9f3a1c0b2d4e5a60","app":"studio","ver":"0.1 experimental","os":"windows","k":"tool","id":"draw.ellipse"}
```

| Champ | Sens |
|---|---|
| `v` | version du format (1) |
| `t` | date et heure UTC, au millième de seconde |
| `sid` | identifiant aléatoire d'un lancement : relie les événements d'une session, sans identifier la personne |
| `app`, `ver`, `os` | application, version, système |
| `k` | type d'événement : `click` (bouton), `tool` (outil actif), `action` (commande de la palette ou du menu), `space` (espace de travail), `session` (`start` / `end`) |
| `id` | identifiant du contrôle : `objectName` du bouton (à défaut son libellé), identifiant d'outil, d'action ou d'espace |
| `d` | détail facultatif (`refused` quand une action est refusée) |

**Ce qui n'est jamais enregistré** : noms ou chemins de fichiers, textes saisis (y compris le texte
des lettrages), contenu ou nom des images, coordonnées de la souris, contenu du projet. Un
identifiant est plafonné à 160 octets.

## Désactiver

- **Préférences ▸ Usage** : décocher « Enregistrer l'usage de l'application sur cet ordinateur ». Le choix
  est conservé par un fichier `DISABLED` dans le dossier du journal, donc valable d'une version à l'autre ;
- ou variable d'environnement `OPENSTITCH_USAGE_LOG=0` (ou `false`, `off`, `non`) avant le lancement
  (la case des Préférences est alors grisée) ;
- ou créer soi-même un fichier vide nommé `DISABLED` dans le dossier du journal.

Le journal ne gêne jamais le travail : une erreur d'écriture (disque plein, dossier inaccessible) est
ignorée.

## Lire les KPI

```bash
python scripts/usage_report.py                       # dossier par défaut
python scripts/usage_report.py --dir D:\kpi --top 20 # un autre dossier (répétable)
python scripts/usage_report.py --since 2026-11-01 --json > kpi.json
```

Le rapport donne le nombre d'événements, de sessions et de jours actifs, la médiane d'événements par
session, la répartition par version, et les classements des boutons, outils, actions et espaces les
plus utilisés. Les lignes illisibles sont comptées et ignorées.

## Pour les développeurs

- Cœur sans Qt : `apps/desktop_v2/core/usage_log.{hpp,cpp}` (`UsageLog`, horloge injectable, tests
  Catch2 `[usage]`).
- Adaptateur Qt : `apps/desktop_v2/studio/usage_controller.{hpp,cpp}`, exposé au QML sous `usage`.
  `StudioButton` journalise ses clics ; `UsageController::observe` écoute `ActionController::ran` et
  `ToolController::activeToolChanged` ; `Main.qml` déclare les changements d'espace.
- Ajouter un nouveau type d'événement : appeler `usage.track("<type>", "<identifiant>")` côté QML ou
  `UsageController::record` côté C++, puis le documenter ici. Ne jamais y mettre de donnée
  personnelle.
