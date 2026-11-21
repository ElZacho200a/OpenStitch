<#
.SYNOPSIS
    Amorce le build d'un nouveau worktree avec le build déjà compilé d'une autre copie du dépôt.

.DESCRIPTION
    Copier build/msvc tel quel ne suffit pas : CMakeCache.txt, les .vcxproj et les journaux
    MSBuild (.tlog, UTF-16) contiennent des chemins absolus, et les sources fraîchement extraites
    par `git worktree add` sont plus récentes que les .obj copiés (tout serait recompilé).
    Ce script :
      1. copie <Source>\build\<Preset> vers <Target>\build\<Preset> (robocopy, dates conservées) ;
      2. réécrit la racine de la source en racine de la cible dans les fichiers texte du build
         (CMake, projets Visual Studio, .tlog) ;
      3. redonne aux fichiers suivis identiques (même blob git, non modifiés dans la source) la date
         de la source : seuls les fichiers qui diffèrent sont recompilés ;
      4. relance `cmake --preset <Preset>` dans la cible ;
      5. renomme les dossiers de suivi MSBuild `<nom>.<GUID8>.tlog` : leur suffixe est le début du
         ProjectGuid, que CMake dérive du chemin du projet. Sans ce renommage, MSBuild ne retrouve
         pas l'historique incrémental et recompile chaque projet en entier.
    Le build suivant (ciblé ou complet) ne recompile que ce qui diffère de la source.

.EXAMPLE
    # Depuis le nouveau worktree (la source par défaut est le dépôt principal) :
    pwsh scripts/worktree-seed-build.ps1
    powershell -File scripts/worktree-seed-build.ps1 -Source C:\...\Embrodeur -Target C:\...\Embrodeur\worktrees\x
#>
param(
    [string]$Source = "",
    [string]$Target = (Get-Location).Path,
    [string]$Preset = "msvc",
    [switch]$SkipConfigure,
    [switch]$IfNeeded
)
$ErrorActionPreference = "Stop"

function Resolve-Root([string]$Path) {
    $root = (& git -C $Path rev-parse --show-toplevel).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Pas un dépôt git : $Path" }
    return [System.IO.Path]::GetFullPath($root)
}

$Target = Resolve-Root $Target
if (-not $Source) {
    # Premier worktree listé = dépôt principal.
    $first = (& git -C $Target worktree list --porcelain | Select-String '^worktree ' | Select-Object -First 1)
    $Source = $first.Line.Substring(9)
}
$Source = Resolve-Root $Source
if ($Source -ieq $Target) { throw "Source et cible identiques : $Source" }

$srcBuild = Join-Path $Source "build\$Preset"
$dstBuild = Join-Path $Target "build\$Preset"
if ($IfNeeded -and (Test-Path (Join-Path $dstBuild "CMakeCache.txt"))) {
    Write-Host "[seed] build deja configure dans $dstBuild : reutilisation, aucune copie"
    exit 0
}
if (-not (Test-Path (Join-Path $srcBuild "CMakeCache.txt"))) { throw "Aucun build configuré dans $srcBuild" }
if (Test-Path (Join-Path $dstBuild "CMakeCache.txt")) { throw "La cible a déjà un build : $dstBuild (supprimer d'abord)" }
if ($Preset -ne "msvc") {
    throw "Ce script reutilise les traces MSBuild du preset msvc uniquement ; ne pas copier un build Ninja/dev-* avec ce mecanisme."
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
Write-Host "[seed] copie $srcBuild -> $dstBuild"
& robocopy $srcBuild $dstBuild /E /MT:16 /COPY:DAT /DCOPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy a échoué (code $LASTEXITCODE)" }
Write-Host ("[seed] copie terminée en {0:N0} s" -f $sw.Elapsed.TotalSeconds)

# 2. Réécriture des chemins absolus (insensible à la casse : les .tlog sont en majuscules).
$variants = @(
    @{ From = $Source.Replace('\', '/'); To = $Target.Replace('\', '/') },
    @{ From = $Source; To = $Target }
)
$patterns = @('*.txt', '*.cmake', '*.vcxproj', '*.vcxproj.filters', '*.sln', '*.slnx', '*.tlog',
              '*.props', '*.targets', '*.rule', '*.json', '*.ninja', '*.recipe', '*.lastbuildstate')
$rewritten = 0
$TargetUpper = $Target.ToUpperInvariant()
$quoteTarget = ($Target -match '[^A-Za-z0-9\\:._]') -and -not ($Source -match '[^A-Za-z0-9\\:._]')
Get-ChildItem $dstBuild -Recurse -File -Include $patterns -ErrorAction SilentlyContinue |
    ForEach-Object {
        $file = $_
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $utf16 = $bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE
        $enc = if ($utf16) { [System.Text.Encoding]::Unicode } else { New-Object System.Text.UTF8Encoding($false) }
        $text = $enc.GetString($bytes)
        $new = $text
        foreach ($v in $variants) {
            # Les .tlog notent les chemins en MAJUSCULES et MSBuild les compare tels quels :
            # remplacer en majuscules ce qui l'était, sinon tout est recompilé.
            $to = $v.To
            $evaluator = [System.Text.RegularExpressions.MatchEvaluator] {
                param($m)
                if ($m.Value -ceq $m.Value.ToUpperInvariant()) { $to.ToUpperInvariant() } else { $to }
            }
            $new = [regex]::Replace($new, [regex]::Escape($v.From), $evaluator,
                                    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        }
        # MSBuild met entre guillemets un chemin de /I contenant un caractère hors de
        # [A-Za-z0-9\:._] (un tiret, typiquement). Si la cible en a besoin et pas la source,
        # la ligne de commande enregistrée diffère et tout le projet est recompilé.
        if ($quoteTarget -and $file.Name -like '*.command.*.tlog') {
            $new = [regex]::Replace($new, '(?<=[\s^]|^)(/[A-Za-z:]+)(' + [regex]::Escape($TargetUpper) + '[^\s"]*)',
                                    '$1"$2"')
        }
        if ($new -ne $text) {
            $time = $file.LastWriteTimeUtc
            $out = $enc.GetBytes($new)
            if ($utf16 -and -not ($out.Length -ge 2 -and $out[0] -eq 0xFF)) {
                $out = [byte[]](0xFF, 0xFE) + $out
            }
            [System.IO.File]::WriteAllBytes($file.FullName, $out)
            $file.LastWriteTimeUtc = $time
            $script:rewritten++
        }
    }
Write-Host "[seed] chemins réécrits dans $rewritten fichiers"

# 3. Dates des sources identiques : ne JAMAIS antidater un fichier modifie dans
#    la source OU la cible : sinon MSBuild pourrait ne pas recompiler le code edite.
$modified = @{}
foreach ($root in @($Source, $Target)) {
    foreach ($p in (& git -C $root diff --name-only HEAD)) { $modified[$p] = $true }
    foreach ($p in (& git -C $root ls-files --others --exclude-standard)) { $modified[$p] = $true }
}
$srcBlobs = @{}
foreach ($line in (& git -C $Source ls-files -s)) {
    $parts = $line -split "\s+", 4
    $srcBlobs[$parts[3]] = $parts[1]
}
$synced = 0
$differ = 0
foreach ($line in (& git -C $Target ls-files -s)) {
    $parts = $line -split "\s+", 4
    $path = $parts[3]
    if ($modified.ContainsKey($path) -or $srcBlobs[$path] -ne $parts[1]) { $differ++; continue }
    $src = Join-Path $Source $path
    $dst = Join-Path $Target $path
    if ((Test-Path -LiteralPath $src -PathType Leaf) -and (Test-Path -LiteralPath $dst -PathType Leaf)) {
        (Get-Item -LiteralPath $dst).LastWriteTimeUtc = (Get-Item -LiteralPath $src).LastWriteTimeUtc
        $synced++
    }
}
Write-Host "[seed] dates alignées sur la source : $synced fichiers ; $differ différents (recompilés)"

# GUID (8 premiers caractères) de chaque projet avant reconfiguration : nom des dossiers .tlog.
function Get-ProjectGuids([string]$Dir) {
    $map = @{}
    Get-ChildItem $Dir -Recurse -File -Filter *.vcxproj -ErrorAction SilentlyContinue | ForEach-Object {
        $m = Select-String -LiteralPath $_.FullName -Pattern '<ProjectGuid>\{([0-9A-Fa-f]{8})' |
            Select-Object -First 1
        if ($m) { $map[$_.FullName] = $m.Matches[0].Groups[1].Value.ToUpperInvariant() }
    }
    return $map
}
$oldGuids = Get-ProjectGuids $dstBuild

# 4. Reconfiguration (régénère les projets avec les nouveaux chemins).
if (-not $SkipConfigure) {
    Push-Location $Target
    try {
        & cmake --preset $Preset *> (Join-Path $dstBuild "seed-configure.log")
        if ($LASTEXITCODE -ne 0) { throw "cmake --preset $Preset a échoué (voir $dstBuild\seed-configure.log)" }
    } finally { Pop-Location }
}
# 5. Dossiers de suivi MSBuild renommés vers le nouveau GUID (sinon tout est recompilé).
if (-not $SkipConfigure) {
    $newGuids = Get-ProjectGuids $dstBuild
    # Correspondance ancien -> nouveau GUID8, puis un seul parcours des dossiers .tlog.
    $guidMap = @{}
    foreach ($proj in $newGuids.Keys) {
        $old = $oldGuids[$proj]
        if ($old -and $old -ne $newGuids[$proj]) { $guidMap[$old] = $newGuids[$proj] }
    }
    $renamed = 0
    if ($guidMap.Count -gt 0) {
        Get-ChildItem $dstBuild -Recurse -Directory -Filter *.tlog -ErrorAction SilentlyContinue |
            ForEach-Object {
                if ($_.Name -match '^(.+)\.([0-9A-F]{8})((_MD)?\.tlog)$' -and $guidMap.ContainsKey($Matches[2])) {
                    $leaf = $Matches[1] + '.' + $guidMap[$Matches[2]] + $Matches[3]
                    if (-not (Test-Path -LiteralPath (Join-Path $_.Parent.FullName $leaf))) {
                        Rename-Item -LiteralPath $_.FullName -NewName $leaf
                        $renamed++
                    }
                }
            }
    }
    Write-Host "[seed] dossiers .tlog renommés vers le nouveau GUID : $renamed"
}
Write-Host ("[seed] terminé en {0:N0} s ; lancer ensuite le build ciblé habituel" -f $sw.Elapsed.TotalSeconds)
