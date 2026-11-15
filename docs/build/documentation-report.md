# Rapport de génération de la documentation

- **PDF** : `docs/build/OpenStitch-Studio-Documentation.pdf`
- **Existe** : oui
- **Pages** : 312
- **Taille** : 2348.0 Kio (2404316 octets)
- **Chaîne** : python-markdown -> HTML/CSS -> xhtml2pdf (reportlab), SVG via svglib
- **Commande** : `docs\scripts\build-docs.ps1` (ou `python docs/scripts/build-docs.py`)
- **Chapitres produits** : 47

## Chapitres
- À propos de ce document
- Introduction et présentation générale
- Guide de prise en main
- Installation et premier démarrage
- Guide utilisateur détaillé
- Ligne de commande (openstitch-cli)
- Interface programmatique : projets `.osp` en ligne de commande
- Traitement d'image
- Segmentation
- Vectorisation
- Objets de broderie
- Numérisation automatique
- Génération de points — point droit et fondations
- Moteur de génération de points
- Satin par squelette et traversées orientées
- Colonne satin
- Remplissage tatami
- Remplissage directionnel
- Lettrage (texte brodé)
- Retouche des points et de la géométrie
- Recherche brevets broderie
- Fiches brevets : satin, squelette et jonctions
- Fiches brevets : auto-numérisation (famille Goldman et Brother)
- Fiches brevets : sous-couches, contours auto-intersectés et points cachés
- Fiches brevets : stippling, analyse de points et appliqué
- Palettes et fils
- Simulation de couture
- Analyse et validation
- Fiche de production
- Format DST (Tajima)
- formats-pes-jef-exp
- Format de projet `.osp`
- Architecture logicielle
- Référence des modules
- Modèle de données
- Algorithmes
- Compilation et développement
- Guide du développeur
- Tests
- Guide de contribution
- Dépannage
- Journal d'usage local (KPI)
- Limitations et état des fonctionnalités
- Roadmap
- Glossaire
- Licences
- Annexe — Audit du dépôt

## Contrôles de cohérence
**Problèmes (bloquants) :**
- ⛔ Lien vers un chapitre inconnu (index.md) : mcp.md
- ⛔ Lien vers un chapitre inconnu (index.md) : ai-companion.md
- ⛔ Lien vers un chapitre inconnu (index.md) : i18n.md
- ⛔ Lien vers un chapitre inconnu (licenses.md) : ai-companion.md
