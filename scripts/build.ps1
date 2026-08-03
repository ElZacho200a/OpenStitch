<#
.SYNOPSIS
    Configure et compile OpenStitch Studio (Debug et/ou Release).
.DESCRIPTION
    Enveloppe simple autour de CMake/CTest (voir docs/source/build-system.md
    et CLAUDE.md). Necessite VCPKG_ROOT et QT_ROOT dans l'environnement
    (voir README.md).
.PARAMETER Configuration
    Debug, Release, ou Both (defaut : Both).
.PARAMETER Test
    Lance ctest apres la compilation, pour chaque configuration compilee.
.PARAMETER Clean
    Supprime build/msvc avant de reconfigurer (reconfiguration complete,
    plus lente : vcpkg recompile ses dependances).
.EXAMPLE
    .\scripts\build.ps1
    Configure puis compile Debug et Release.
.EXAMPLE
    .\scripts\build.ps1 -Configuration Debug -Test
    Compile Debug seulement puis lance les tests.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Assert-EnvVar {
    param([string]$Name)
    $value = [System.Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) {
        Write-Error "$Name n'est pas defini. Voir README.md (section Compilation) : `$env:$Name = 'chemin'."
        exit 1
    }
    if (-not (Test-Path $value)) {
        Write-Error "$Name pointe vers un chemin inexistant : $value"
        exit 1
    }
}

Assert-EnvVar -Name 'VCPKG_ROOT'
Assert-EnvVar -Name 'QT_ROOT'

if ($Clean -and (Test-Path 'build\msvc')) {
    Write-Host "== Suppression de build\msvc ==" -ForegroundColor Yellow
    Remove-Item -Recurse -Force 'build\msvc'
}

Write-Host "== Configuration (cmake --preset msvc) ==" -ForegroundColor Cyan
cmake --preset msvc
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$configs = if ($Configuration -eq 'Both') { @('Debug', 'Release') } else { @($Configuration) }

foreach ($cfg in $configs) {
    $preset = "msvc-$($cfg.ToLower())"

    Write-Host "== Compilation ($preset) ==" -ForegroundColor Cyan
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if ($Test) {
        Write-Host "== Tests ($preset) ==" -ForegroundColor Cyan
        ctest --preset $preset --output-on-failure
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

Write-Host "== Termine ($Configuration) ==" -ForegroundColor Green
