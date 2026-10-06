# Docker

Deux images, deux usages. **Le produit livré est un binaire Windows (MSVC + Qt 6.8.3)** ;
la vérité de référence est le job `windows-msvc` de `.github/workflows/ci.yml`.

| Image | Pour quoi | Où ça tourne | État |
|---|---|---|---|
| `linux-qt/` | Compiler cœur + CLI + **desktop Qt** et lancer toute la suite (QTest en headless) rapidement et de façon reproductible | Docker sur Linux, ou Docker Desktop (mode Linux) sous Windows/macOS/WSL2 | Recette reprise des commandes exécutées à la main le 2026-10-06 (cœur + CLI : 814 tests passés ; build desktop Qt : voir l'état des audits UI). L'image elle-même n'a **pas** été construite (pas de démon Docker disponible). |
| `windows/` | Reproduire localement le build MSVC de la CI dans un conteneur Windows | **Hôte Windows uniquement**, Docker en mode « Windows containers » | **Non testée** : à valider sur une machine Windows. |

```powershell
# Linux headless (depuis la racine du dépôt) :
docker build -t openstitch-linux-qt -f docker/linux-qt/Dockerfile .
docker run --rm -v "${PWD}:/src" openstitch-linux-qt

# Windows (hôte Windows, Docker en mode Windows containers) :
docker build -t openstitch-windows -f docker/windows/Dockerfile .
docker run --rm -v "${PWD}:C:\src" openstitch-windows
```

Limites à connaître :

- L'image Linux utilise le Qt 6.4 d'Ubuntu, pas le 6.8.3 de la CI : elle détecte les erreurs de
  câblage, de compilation et de logique, pas les écarts propres à Qt 6.8 ou au style Windows.
- Aucun conteneur ne remplace un essai sur un vrai bureau Windows : rendu, DPI, thème sombre
  et raccourcis restent à vérifier à la main.
- Les tests desktop n'affichent rien à l'écran (`QT_QPA_PLATFORM=offscreen`) : pas de
  comparaison de pixels, conformément aux conventions du projet.
