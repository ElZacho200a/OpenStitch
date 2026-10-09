<#
.SYNOPSIS
    Configure et compile OpenStitch Studio (Debug et/ou Release).
.DESCRIPTION
    Enveloppe autour de CMake/CTest (voir docs/source/build-system.md et
    CLAUDE.md). VOIE RAPIDE : si la chaine d'outils est incomplete (CMake,
    Visual Studio Build Tools avec le workload C++, vcpkg, Qt 6.8.3 via
    aqtinstall), le script LISTE ce qui manque, DEMANDE CONFIRMATION puis
    l'installe (telechargements de plusieurs Go, variables utilisateur
    VCPKG_ROOT/QT_ROOT ecrites de facon permanente) avant de
    configurer/compiler. -Yes confirme d'avance (CI, scripts) ;
    -SkipBootstrap ne verifie ni n'installe rien (poste deja configure :
    utilise VCPKG_ROOT et QT_ROOT tels que definis). VOIE MANUELLE : voir
    docs/source/installation.md.
    En cas d'echec, le script leve une erreur (code de sortie non nul).
.PARAMETER Configuration
    Debug, Release, ou Both (defaut : Both).
.PARAMETER Test
    Lance ctest apres la compilation, pour chaque configuration compilee.
.PARAMETER Clean
    Supprime build/msvc avant de reconfigurer (reconfiguration complete,
    plus lente : vcpkg recompile ses dependances).
.PARAMETER SkipBootstrap
    Ignore la verification/installation automatique de CMake, Visual
    Studio Build Tools, vcpkg et Qt -- utilise directement VCPKG_ROOT et
    QT_ROOT tels que definis dans l'environnement.
.PARAMETER Yes
    Confirme d'avance l'installation des outils manquants (sans invite).
    Sans -Yes et sans console interactive, le script refuse d'installer.
.EXAMPLE
    .\scripts\build.ps1
    Liste les outils manquants, demande confirmation, les installe puis
    configure et compile Debug et Release.
.EXAMPLE
    .\scripts\build.ps1 -SkipBootstrap -Configuration Release
    Poste deja configure : aucune verification, aucune installation.
.EXAMPLE
    .\scripts\build.ps1 -Configuration Debug -Test
    Compile Debug seulement puis lance les tests.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    [switch]$Test,
    [switch]$Clean,
    [switch]$SkipBootstrap,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$QtVersion = '6.8.3'
$QtArch = 'win64_msvc2022_64'
$VsBuildToolsUrl = 'https://aka.ms/vs/17/release/vs_buildtools.exe'
$VsRequiredComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'

function Test-CommandExists {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

# Les installeurs (winget, VS Build Tools, CMake) modifient le PATH
# machine/utilisateur en registre, mais ce processus PowerShell deja lance
# ne le relit jamais automatiquement -- sans ce refresh, une commande tout
# juste installee resterait "introuvable" jusqu'a la prochaine session.
function Update-SessionPath {
    $machine = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [System.Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = @($machine, $user) -join ';'
}

function Ensure-Winget {
    if (Test-CommandExists 'winget') {
        return
    }
    throw "winget est introuvable. Installez 'App Installer' depuis le Microsoft Store (present par defaut sur Windows 10 22H2+/Windows 11), ou installez CMake/Visual Studio manuellement puis relancez avec -SkipBootstrap."
}

function Ensure-CMake {
    if (Test-CommandExists 'cmake') {
        return
    }
    Write-Host "== CMake introuvable : installation via winget ==" -ForegroundColor Cyan
    Ensure-Winget
    winget install --id Kitware.CMake -e --silent --accept-source-agreements --accept-package-agreements
    Update-SessionPath
    if (-not (Test-CommandExists 'cmake')) {
        throw "CMake reste introuvable apres installation. Ouvrez un nouveau terminal (PATH mis a jour) et relancez le script."
    }
}

function Test-VisualStudio {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires $VsRequiredComponent -property installationPath
        return -not [string]::IsNullOrWhiteSpace($vsPath)
    }
    return $false
}

function Test-Vcpkg {
    $existing = [System.Environment]::GetEnvironmentVariable('VCPKG_ROOT')
    return [bool]($existing -and (Test-Path (Join-Path $existing 'scripts\buildsystems\vcpkg.cmake')))
}

function Test-Qt {
    $existing = [System.Environment]::GetEnvironmentVariable('QT_ROOT')
    if ($existing -and (Test-Path (Join-Path $existing 'bin\qmake.exe'))) {
        return $true
    }
    foreach ($candidate in @((Join-Path $HOME "Qt\$QtVersion\msvc2022_64"), "C:\Qt\$QtVersion\msvc2022_64")) {
        if (Test-Path (Join-Path $candidate 'bin\qmake.exe')) {
            return $true
        }
    }
    return $false
}

function Ensure-VisualStudioBuildTools {
    if (Test-VisualStudio) {
        return
    }
    Write-Host "== Visual Studio (workload C++) introuvable : installation de Visual Studio Build Tools ==" -ForegroundColor Cyan
    Write-Host "   (telechargement ~5 Go, plusieurs minutes ; une invite d'elevation peut apparaitre)" -ForegroundColor DarkGray
    $installer = Join-Path $env:TEMP 'vs_buildtools.exe'
    Invoke-WebRequest -Uri $VsBuildToolsUrl -OutFile $installer
    $proc = Start-Process -FilePath $installer -ArgumentList @(
        '--quiet', '--wait', '--norestart', '--nocache',
        '--add', 'Microsoft.VisualStudio.Workload.VCTools',
        '--add', $VsRequiredComponent,
        '--includeRecommended'
    ) -Wait -PassThru
    # 3010 = succes, redemarrage recommande (pas bloquant pour compiler).
    if ($proc.ExitCode -ne 0 -and $proc.ExitCode -ne 3010) {
        throw "Installation de Visual Studio Build Tools echouee (code $($proc.ExitCode))."
    }
    Update-SessionPath
}

function Ensure-Vcpkg {
    $existing = [System.Environment]::GetEnvironmentVariable('VCPKG_ROOT')
    if ($existing -and (Test-Path (Join-Path $existing 'scripts\buildsystems\vcpkg.cmake'))) {
        $env:VCPKG_ROOT = $existing
        return
    }
    $target = Join-Path $HOME '.local\vcpkg'
    if (-not (Test-Path (Join-Path $target 'scripts\buildsystems\vcpkg.cmake'))) {
        Write-Host "== vcpkg introuvable : clonage et bootstrap dans $target ==" -ForegroundColor Cyan
        if (Test-Path $target) {
            Remove-Item -Recurse -Force $target
        }
        git clone https://github.com/microsoft/vcpkg.git $target
        & "$target\bootstrap-vcpkg.bat" -disableMetrics
        if ($LASTEXITCODE -ne 0) {
            throw "Bootstrap de vcpkg echoue."
        }
    }
    [System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', $target, 'User')
    $env:VCPKG_ROOT = $target
}

function Ensure-Qt {
    $existing = [System.Environment]::GetEnvironmentVariable('QT_ROOT')
    if ($existing -and (Test-Path (Join-Path $existing 'bin\qmake.exe'))) {
        $env:QT_ROOT = $existing
        return
    }
    # Emplacements courants d'une installation Qt deja faite a la main.
    $candidates = @(
        (Join-Path $HOME "Qt\$QtVersion\msvc2022_64"),
        "C:\Qt\$QtVersion\msvc2022_64"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate 'bin\qmake.exe')) {
            [System.Environment]::SetEnvironmentVariable('QT_ROOT', $candidate, 'User')
            $env:QT_ROOT = $candidate
            return
        }
    }
    Write-Host "== Qt $QtVersion introuvable : installation via aqtinstall (pas de compte requis) ==" -ForegroundColor Cyan
    if (-not (Test-CommandExists 'python')) {
        throw "Python est requis pour installer Qt automatiquement (aqtinstall). Installez Python (winget install Python.Python.3.12) puis relancez, ou installez Qt vous-meme et definissez QT_ROOT."
    }
    python -m pip install --quiet --upgrade aqtinstall
    if ($LASTEXITCODE -ne 0) {
        throw "Installation d'aqtinstall (pip) echouee."
    }
    $qtBase = Join-Path $HOME 'Qt'
    python -m aqt install-qt windows desktop $QtVersion $QtArch -O $qtBase
    if ($LASTEXITCODE -ne 0) {
        throw "Installation de Qt via aqtinstall echouee."
    }
    $installed = Join-Path $qtBase "$QtVersion\msvc2022_64"
    [System.Environment]::SetEnvironmentVariable('QT_ROOT', $installed, 'User')
    $env:QT_ROOT = $installed
}

function Assert-EnvVar {
    param([string]$Name)
    $value = [System.Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "$Name n'est pas defini. Voir README.md (section Compilation) : `$env:$Name = 'chemin'."
    }
    if (-not (Test-Path $value)) {
        throw "$Name pointe vers un chemin inexistant : $value"
    }
}

# Annonce et confirmation : le bootstrap telecharge plusieurs Go et ecrit des
# variables d'environnement utilisateur permanentes -- jamais en silence.
function Confirm-Bootstrap {
    $missing = @()
    if (-not (Test-CommandExists 'cmake')) { $missing += 'CMake (winget, Kitware.CMake)' }
    if (-not (Test-VisualStudio)) { $missing += 'Visual Studio Build Tools + workload C++ (telechargement ~5 Go, elevation possible)' }
    if (-not (Test-Vcpkg)) { $missing += "vcpkg (clone dans $HOME\.local\vcpkg ; ecrit VCPKG_ROOT dans l'environnement utilisateur)" }
    if (-not (Test-Qt)) { $missing += "Qt $QtVersion via aqtinstall (dans $HOME\Qt, necessite Python ; ecrit QT_ROOT dans l'environnement utilisateur)" }
    if ($missing.Count -eq 0) {
        Write-Host "== Chaine d'outils complete : rien a installer ==" -ForegroundColor Green
        return
    }
    Write-Host "== Outils manquants : ce script va les INSTALLER ==" -ForegroundColor Yellow
    foreach ($m in $missing) { Write-Host "   - $m" -ForegroundColor Yellow }
    Write-Host "   (voie manuelle equivalente : docs/source/installation.md ; -SkipBootstrap pour ne rien installer)" -ForegroundColor DarkGray
    if ($Yes) {
        return
    }
    if (-not [Environment]::UserInteractive -or [Console]::IsInputRedirected) {
        throw "Installation des outils manquants non confirmee (console non interactive). Relancez avec -Yes pour accepter, ou avec -SkipBootstrap apres avoir installe les outils vous-meme."
    }
    $answer = Read-Host "Installer maintenant ? (o/N)"
    if ($answer -notmatch '^(o|oui|y|yes)$') {
        throw "Installation refusee : rien n'a ete modifie. Installez les outils vous-meme (docs/source/installation.md) puis relancez avec -SkipBootstrap."
    }
}

if (-not $SkipBootstrap) {
    Write-Host "== Verification de la chaine d'outils (CMake, Visual Studio, vcpkg, Qt) ==" -ForegroundColor Cyan
    Confirm-Bootstrap
    Ensure-CMake
    Ensure-VisualStudioBuildTools
    Ensure-Vcpkg
    Ensure-Qt
}

Assert-EnvVar -Name 'VCPKG_ROOT'
Assert-EnvVar -Name 'QT_ROOT'

if ($Clean -and (Test-Path 'build\msvc')) {
    Write-Host "== Suppression de build\msvc ==" -ForegroundColor Yellow
    Remove-Item -Recurse -Force 'build\msvc'
}

Write-Host "== Configuration (cmake --preset msvc) ==" -ForegroundColor Cyan
cmake --preset msvc
if ($LASTEXITCODE -ne 0) { throw "cmake --preset msvc a echoue (code $LASTEXITCODE)." }

$configs = if ($Configuration -eq 'Both') { @('Debug', 'Release') } else { @($Configuration) }

foreach ($cfg in $configs) {
    $preset = "msvc-$($cfg.ToLower())"

    # Sans --target : construit TOUT (cible par defaut du generateur), pas
    # seulement l'executable desktop -- comprend aussi CLI, libs et tests.
    Write-Host "== Compilation ($preset) : tout (aucune cible restreinte) ==" -ForegroundColor Cyan
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "La compilation ($preset) a echoue (code $LASTEXITCODE)." }

    if ($Test) {
        Write-Host "== Tests ($preset) ==" -ForegroundColor Cyan
        ctest --preset $preset --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Des tests ont echoue ($preset, code $LASTEXITCODE)." }
    }
}

# Recapitulatif explicite des executables desktop produits : la confusion
# vecue en pratique n'etait pas un defaut de build, mais de savoir QUEL
# .exe (parmi Debug/Release, ou un autre repertoire build\* jamais touche
# par ce script) un raccourci lance reellement. Afficher chemin + date de
# derniere ecriture ici rend la fraicheur immediatement verifiable, sans
# devoir comparer manuellement des horodatages dans l'explorateur.
Write-Host "== Executables desktop produits ==" -ForegroundColor Cyan
foreach ($cfg in $configs) {
    $exePath = "build\msvc\apps\desktop\$cfg\openstitch.exe"
    if (Test-Path $exePath) {
        $item = Get-Item $exePath
        Write-Host "  $exePath  (modifie le $($item.LastWriteTime))" -ForegroundColor Gray
    } else {
        Write-Host "  $exePath  -- INTROUVABLE (echec de compilation ?)" -ForegroundColor Red
    }
}
Write-Host "  Astuce : verifiez la cible reelle de vos raccourcis (clic droit > Proprietes)" -ForegroundColor DarkGray
Write-Host "  pour confirmer qu'ils pointent bien vers l'un des chemins ci-dessus." -ForegroundColor DarkGray

Write-Host "== Termine ($Configuration) ==" -ForegroundColor Green
