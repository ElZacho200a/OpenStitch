# SPDX-License-Identifier: Apache-2.0
# Génère la documentation PDF d'OpenStitch Studio de façon reproductible.
# Crée un environnement Python local (docs/.venv), installe les dépendances
# documentaires (gratuites, hors ligne après pip), génère les diagrammes et le
# PDF, puis vérifie le résultat. Ne modifie pas l'environnement Python global.

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$docs = Split-Path -Parent $here
$venv = Join-Path $docs ".venv"
$py   = Join-Path $venv "Scripts\python.exe"
$pdf  = Join-Path $docs "build\OpenStitch-Studio-Documentation.pdf"

Write-Host "== Documentation OpenStitch Studio ==" -ForegroundColor Cyan

# 1. Prérequis : Python
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Error "Python introuvable. Installez Python 3.10+ et réessayez."
    exit 1
}

# 2. Environnement local
if (-not (Test-Path $py)) {
    Write-Host "Création de l'environnement virtuel docs/.venv…"
    python -m venv $venv
}

# 3. Dépendances documentaires
Write-Host "Installation des dépendances documentaires…"
& $py -m pip install --quiet --upgrade pip | Out-Null
& $py -m pip install --quiet -r (Join-Path $docs "requirements.txt")
if ($LASTEXITCODE -ne 0) { Write-Error "Échec de l'installation des dépendances."; exit 1 }

# 4-8. Diagrammes + construction du PDF + contrôles (dans build-docs.py)
Write-Host "Génération des diagrammes et du PDF…"
& $py (Join-Path $here "build-docs.py")
$code = $LASTEXITCODE

# 9. Vérification
if (-not (Test-Path $pdf)) {
    Write-Error "Le PDF n'a pas été produit : $pdf"
    exit 1
}
Write-Host ""
Write-Host "PDF généré : $pdf" -ForegroundColor Green
Write-Host "Rapport    : $(Join-Path $docs 'build\documentation-report.md')"

# 10. Code de sortie (non nul si contrôles bloquants)
if ($code -ne 0) {
    Write-Warning "La génération a signalé des problèmes de cohérence (voir le rapport)."
    exit $code
}
exit 0
