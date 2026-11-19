<#
.SYNOPSIS
    Build de developpement rapide : Ninja + MSVC, Studio seul par defaut.
.DESCRIPTION
    Utilise les presets dev-debug / dev-relwithdebinfo / dev-notests de
    CMakePresets.json (Widgets V1 non construits, OPENSTITCH_BUILD_LEGACY_V1=OFF).
    Charge l'environnement Visual Studio (cl.exe) et Ninja si absents du PATH,
    puis configure (si besoin) et compile uniquement la cible demandee.
    Les presets historiques (msvc, msvc-relwithdebinfo) et scripts/build.ps1
    ne sont pas modifies.
.PARAMETER Preset
    dev-debug (defaut), dev-relwithdebinfo, dev-notests (sans tests).
.PARAMETER Target
    Cible(s) CMake (defaut : openstitch_v2_studio, produit openstitch.exe).
.PARAMETER Jobs
    Taches paralleles (defaut 2 : poste partage 16 Go).
.PARAMETER Run
    Lance l'executable Studio apres compilation (arrete apres -RunSeconds).
.PARAMETER Reconfigure
    Force la reconfiguration CMake.
.PARAMETER VcpkgInstalledDir
    Reutilise un dossier vcpkg_installed existant (defaut : build/msvc/vcpkg_installed
    s'il existe) pour ne pas recompiler/dupliquer les dependances.
.EXAMPLE
    .\scripts\build-dev.ps1
.EXAMPLE
    .\scripts\build-dev.ps1 -Preset dev-notests -Target openstitch-cli -Jobs 4
#>
[CmdletBinding()]
param(
    [ValidateSet('dev-debug', 'dev-relwithdebinfo', 'dev-notests')]
    [string]$Preset = 'dev-debug',
    [string[]]$Target = @('openstitch_v2_studio'),
    [ValidateRange(1, 64)]
    [int]$Jobs = 2,
    [switch]$Run,
    [int]$RunSeconds = 8,
    [switch]$Reconfigure,
    [string]$VcpkgInstalledDir
)

$ErrorActionPreference = 'Stop'
$savedVcpkgRoot = $env:VCPKG_ROOT   # VsDevCmd le remplace par le vcpkg embarque
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Measure-Step([string]$Name, [scriptblock]$Block) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'   # stderr natif (avertissements) n'est pas fatal
    & $Block
    $ErrorActionPreference = $old
    if ($LASTEXITCODE -ne 0) { throw "$Name a echoue (code $LASTEXITCODE)" }
    Write-Host ("[build-dev] {0} : {1:N1} s" -f $Name, $sw.Elapsed.TotalSeconds)
}

if (-not $env:VCPKG_ROOT -or -not $env:QT_ROOT) {
    throw 'VCPKG_ROOT et QT_ROOT doivent etre definis (voir docs/source/installation.md).'
}

# cl.exe : charger l'environnement VS x64 si necessaire.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $devCmd)) { throw "VsDevCmd.bat introuvable dans $vsPath" }
    $env:Path = "$(Split-Path -Parent $vswhere);$env:Path"   # VsDevCmd appelle vswhere via le PATH
    # Importe l'environnement de VsDevCmd (x64) dans ce processus.
    cmd /c "`"$devCmd`" -arch=amd64 -host_arch=amd64 -no_logo >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'cl.exe introuvable apres chargement de l environnement Visual Studio.' }
$env:VCPKG_ROOT = $savedVcpkgRoot
# Ninja : PATH, sinon celui telecharge par vcpkg.
if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
    $n = Get-ChildItem (Join-Path $env:VCPKG_ROOT 'downloads\tools') -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $n) { throw 'ninja.exe introuvable (winget install Ninja-build.Ninja).' }
    $env:Path = "$($n.DirectoryName);$env:Path"
}

$binDir = Join-Path $repoRoot "build\$Preset"
if (-not $VcpkgInstalledDir) {
    $shared = Join-Path $repoRoot 'build\msvc\vcpkg_installed'
    if (Test-Path $shared) { $VcpkgInstalledDir = $shared }
}

if ($Reconfigure -or -not (Test-Path (Join-Path $binDir 'build.ninja'))) {
    $cfg = @('--preset', $Preset)
    if ($VcpkgInstalledDir) { $cfg += "-DVCPKG_INSTALLED_DIR=$VcpkgInstalledDir" }
    Measure-Step 'configure' { & cmake @cfg }
}

$bld = @('--build', $binDir, '--parallel', $Jobs, '--target') + $Target
Measure-Step 'build' { & cmake @bld }

if ($Run) {
    $exe = Get-ChildItem $binDir -Recurse -Filter openstitch.exe | Select-Object -First 1
    if (-not $exe) { throw 'openstitch.exe introuvable apres build.' }
    $proc = Start-Process $exe.FullName -PassThru
    Start-Sleep -Seconds $RunSeconds
    if ($proc.HasExited) { throw "Studio s'est ferme (code $($proc.ExitCode))." }
    Write-Host "[build-dev] Studio demarre (pid $($proc.Id)), arret."
    Stop-Process -Id $proc.Id -Force
}
