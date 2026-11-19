<#
.SYNOPSIS
  Prepare un shell pour le POC sccache (preset CMake "msvc-sccache").
.DESCRIPTION
  Dot-sourcer : `. scripts/dev/sccache-env.ps1`. Charge l'environnement MSVC x64
  (vcvars64) et Ninja, fixe un dossier de cache hors depot et borne sa taille.
  Optionnel : le build standard (preset "msvc") n'en depend pas.
#>
param(
    [string]$CacheDir = (Join-Path $env:LOCALAPPDATA 'sccache-openstitch'),
    [string]$CacheSize = '10G'
)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$keep = @{ VCPKG_ROOT = $env:VCPKG_ROOT; QT_ROOT = $env:QT_ROOT }
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$bat = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
# vcvars64 fixe VCPKG_ROOT sur le vcpkg embarque de VS : on garde celui de l'utilisateur.
foreach ($k in $keep.Keys) { if ($keep[$k]) { Set-Item "env:$k" $keep[$k] } }
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
if (Test-Path $ninja) { $env:PATH = "$ninja;$env:PATH" }
if (-not (Get-Command sccache -ErrorAction SilentlyContinue)) {
    Write-Warning 'sccache introuvable : winget install Mozilla.sccache'
}
$env:SCCACHE_DIR = $CacheDir
$env:SCCACHE_CACHE_SIZE = $CacheSize
Write-Host "sccache : cache $CacheDir (max $CacheSize), cl = $((Get-Command cl).Source)"
