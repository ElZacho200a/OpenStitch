# Documentation d'OpenStitch Studio

Ce dossier contient les **sources** de la documentation utilisateur et technique,
et la chaîne qui produit le PDF.

## Résultat

- PDF : `docs/build/OpenStitch-Studio-Documentation.pdf`
- Rapport de génération : `docs/build/documentation-report.md`

## Régénérer

```powershell
.\docs\scripts\build-docs.ps1
```

ou, directement en Python :

```powershell
docs\.venv\Scripts\python.exe docs\scripts\build-docs.py
```

Le script crée un environnement Python local `docs/.venv`, installe les
dépendances de `docs/requirements.txt`, génère les diagrammes puis le PDF, et
exécute des contrôles de cohérence (liens d'images, fichiers source, marqueurs
non résolus, secrets, chapitres vides). Il retourne un code non nul en cas de
problème bloquant.

## Choix de la chaîne documentaire

| Aspect | Choix |
|---|---|
| Outils | `python-markdown` → HTML/CSS → `xhtml2pdf` (reportlab) |
| Diagrammes | SVG générés par `scripts/gen_diagrams.py`, embarqués vectoriellement |
| Prérequis | Python 3.10+ et pip (aucune dépendance système) |
| Hors ligne | oui (après l'installation pip initiale) |
| Windows | oui, sans LaTeX ni GTK |

**Raisons du choix.** L'environnement ne dispose ni de Pandoc, ni de LaTeX, ni de
Graphviz, ni de Doxygen. Une chaîne **100 % pip, sans dépendance système** est la
plus reproductible sous Windows : `xhtml2pdf`/`reportlab` sont en Python pur et
produisent un PDF A4 avec table des matières, signets (outline), en-têtes/pieds,
numéros de pages, tableaux, code monospace et encadrés. Les diagrammes sont
générés en SVG (déterministes, hors ligne) faute de moteur Mermaid/Graphviz, et
embarqués vectoriellement.

**Limitations de la chaîne.** Le rendu HTML/CSS de `xhtml2pdf` est un
sous-ensemble : pas de mise en page avancée, pas de numéros de page dans la table
des matières intégrée (la navigation se fait par les **signets** PDF et les liens
cliquables). Les diagrammes Mermaid/DOT sont fournis comme **sources**
(`assets/diagrams/`) pour un rendu ultérieur si un moteur devient disponible.

## Structure

```
docs/
  source/        chapitres Markdown (le PDF en est l'assemblage ordonné)
  assets/
    diagrams/    sources de diagrammes (Mermaid/DOT), pour édition
    generated/   diagrammes SVG produits par gen_diagrams.py
    screenshots/ captures (à ajouter)
  scripts/       build-docs.ps1, build-docs.py, gen_diagrams.py
  build/         PDF et rapport (générés)
  requirements.txt
```

## Captures d'écran

Les captures ne sont pas générées automatiquement dans l'environnement actuel.
Les emplacements prévus sont signalés dans le texte ; ajoutez les images dans
`docs/assets/screenshots/` et référencez-les depuis les chapitres.

## Politique de versionnement (Git)

- **Versionnés** : sources Markdown, scripts, sources de diagrammes, SVG générés,
  `requirements.txt`, ce README.
- **Ignorés** : `docs/.venv/` (environnement local). Le PDF et le rapport de
  `docs/build/` peuvent être versionnés pour référence ou régénérés ; voir
  `.gitignore`.
