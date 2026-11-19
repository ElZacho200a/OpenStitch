<#
.SYNOPSIS
    Mesure reproductible des couts de build locaux (configure, build, link, tests, pic de RAM)
    et des temps CI GitHub Actions (lecture seule via gh). BUILD-OPT-01.1.
.DESCRIPTION
    Protocole complet : docs/source/build-baseline.md. Le script NE MODIFIE JAMAIS le contenu des
    sources : les scenarios "touch-*" ne changent que la date de modification (ce que MSBuild
    regarde), puis la restaurent a la fin. Il n'ecrit que dans le repertoire de build et dans
    le CSV de sortie ; il ne pousse rien.

    Chaque ligne du CSV = une mesure (une etape d'un run) :
      timestamp, sha, host, scenario, config, run, step, seconds, peak_tree_mb, peak_sys_used_mb,
      cpu_before_pct, foreign_compilers, exit, msbuild_cl_s, msbuild_link_s, msbuild_exec_s
    - peak_tree_mb   : pic de la somme des working sets de l'arbre de processus lance (cmake,
                       MSBuild, cl, link, windeployqt...), echantillonne toutes les 500 ms.
    - peak_sys_used_mb : pic de memoire physique utilisee sur TOUTE la machine (bruit inclus).
    - cpu_before_pct / foreign_compilers : charge CPU moyenne (2 s) et nombre de cl.exe/link.exe
                       hors de l'arbre mesure juste avant l'etape = indicateur de bruit.
    - msbuild_*_s    : temps CUMULES des taches CL / Link / Exec (PerformanceSummary MSBuild,
                       somme sur les noeuds paralleles, donc differents du temps mur).
.PARAMETER Scenario
    configure-fresh | configure-noop | build-cold | build-noop | touch-qml | touch-studio-cpp |
    touch-core-cpp | touch-core-hpp | build-tests (construit -Target, a donner : les cibles de test) |
    tests | ci. Plusieurs scenarios : -Command "& ./scripts/measure-build.ps1 -Scenario a,b" (pas -File).
.PARAMETER Configuration
    Debug ou Release (defaut Release).
.PARAMETER Runs
    Nombre de repetitions par scenario (defaut 3).
.PARAMETER Target
    Cible(s) CMake construite(s) (defaut openstitch_v2_studio, la boucle de dev de Studio).
    Passer 'ALL_BUILD' pour tout construire comme build.ps1 / la CI.
.PARAMETER Parallel
    Budget de parallelisme (defaut 2 : machine partagee). Memes proprietes MSBuild que build.ps1.
.PARAMETER BuildDir
    Repertoire de build (defaut build/msvc, celui du preset). Les scenarios *-cold/*-fresh le SUPPRIMENT.
.PARAMETER VcpkgInstalledDir
    Si fourni : -DVCPKG_INSTALLED_DIR=<dir> a la configuration (evite de reinstaller vcpkg ;
    n'est jamais copie ni modifie par ce script).
.PARAMETER TestRegex
    Filtre ctest du scenario 'tests' (defaut ^test_core$ : rapide ; '.' = toute la suite).
.PARAMETER OutCsv
    CSV de sortie (ajout). Defaut build/measure/results.csv.
.PARAMETER CiRuns
    Scenario 'ci' : nombre de runs ci.yml recents (succes) a analyser (defaut 5).
.EXAMPLE
    .\scripts\measure-build.ps1 -Scenario configure-noop,build-noop -Configuration Release -Runs 3
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('configure-fresh', 'configure-noop', 'build-cold', 'build-noop', 'touch-qml',
        'touch-studio-cpp', 'touch-core-cpp', 'touch-core-hpp', 'build-tests', 'tests', 'ci')]
    [string[]]$Scenario,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 20)]
    [int]$Runs = 3,
    [string[]]$Target = @('openstitch_v2_studio'),
    [ValidateRange(1, 64)]
    [int]$Parallel = 2,
    [string]$BuildDir = 'build/msvc',
    [string]$VcpkgInstalledDir = '',
    [string]$TestRegex = '^test_core$',
    [string]$OutCsv = 'build/measure/results.csv',
    [ValidateRange(1, 30)]
    [int]$CiRuns = 5
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
$sha = (git rev-parse --short HEAD).Trim()
$hostName = $env:COMPUTERNAME
$cnt = [Globalization.CultureInfo]::InvariantCulture

$touchFiles = @{
    'touch-qml'        = 'apps/desktop_v2/studio/qml/ActionPalette.qml'
    'touch-studio-cpp' = 'apps/desktop_v2/studio/edit_controller.cpp'
    'touch-core-cpp'   = 'libs/core/src/log.cpp'
    'touch-core-hpp'   = 'libs/core/include/openstitch/core/units.hpp'
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutCsv) | Out-Null
if (-not (Test-Path $OutCsv)) {
    'timestamp,sha,host,scenario,config,run,step,seconds,peak_tree_mb,peak_sys_used_mb,cpu_before_pct,foreign_compilers,exit,msbuild_cl_s,msbuild_link_s,msbuild_exec_s' |
        Set-Content -Encoding utf8 $OutCsv
}

function Get-TreeIds([int]$rootId, $procs) {
    $ids = [System.Collections.Generic.HashSet[int]]::new()
    [void]$ids.Add($rootId)
    $grew = $true
    while ($grew) {
        $grew = $false
        foreach ($p in $procs) {
            if ($ids.Contains([int]$p.ParentProcessId) -and $ids.Add([int]$p.ProcessId)) { $grew = $true }
        }
    }
    return ,$ids
}

function Get-NoiseSnapshot {
    $cpu = 0
    try { [void](Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"); Start-Sleep -Seconds 2; $cpu = (Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'").PercentProcessorTime } catch { }
    $foreign = @(Get-Process -Name cl, link -ErrorAction SilentlyContinue).Count
    return @{ Cpu = $cpu; Foreign = $foreign }
}

# Execute "exe args" en mesurant temps mur et pic de RAM de l'arbre ; renvoie un objet resultat.
function Invoke-Measured([string]$step, [string]$exe, [string[]]$exeArgs, [string]$logFile) {
    $noise = Get-NoiseSnapshot
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = ($exeArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.StandardOutputEncoding = [Text.Encoding]::UTF8
    $total = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1MB
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = [System.Diagnostics.Process]::Start($psi)
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    $peakTree = 0.0
    $peakSys = 0.0
    while (-not $proc.HasExited) {
        $procs = Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, WorkingSetSize
        $ids = Get-TreeIds $proc.Id $procs
        $sum = 0.0
        foreach ($p in $procs) { if ($ids.Contains([int]$p.ProcessId)) { $sum += [double]$p.WorkingSetSize } }
        $sum = $sum / 1MB
        if ($sum -gt $peakTree) { $peakTree = $sum }
        $os = Get-CimInstance Win32_OperatingSystem -Property FreePhysicalMemory
        $used = $total - ($os.FreePhysicalMemory / 1024)
        if ($used -gt $peakSys) { $peakSys = $used }
        [void]$proc.WaitForExit(500)
    }
    $proc.WaitForExit()
    $sw.Stop()
    $text = $outTask.Result + $errTask.Result
    Set-Content -Encoding utf8 -Path $logFile -Value $text
    # PerformanceSummary MSBuild : "   12345 ms  CL   3 appel(s)" (locale-independant sur ms + nom)
    $sum = @{ CL = 0.0; Link = 0.0; Exec = 0.0 }
    # Les cibles ET les taches sont listees : on garde le max par nom (pas de double compte).
    # Avec UseMultiToolTask, le temps du compilateur apparait sous "MultiToolTask".
    foreach ($m in [regex]::Matches($text, '(?m)^\s*([\d\s.,]+?)\s*ms\s+(CL|MultiToolTask|Link|Exec)\s+\d+')) {
        $v = [double]::Parse(($m.Groups[1].Value -replace '[\s.]', '' -replace ',', '.'), $cnt) / 1000.0
        $k = if ($m.Groups[2].Value -eq 'MultiToolTask') { 'CL' } else { $m.Groups[2].Value }
        if ($v -gt $sum[$k]) { $sum[$k] = $v }
    }
    return [pscustomobject]@{
        Step = $step; Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        PeakTree = [math]::Round($peakTree, 0); PeakSys = [math]::Round($peakSys, 0)
        Cpu = $noise.Cpu; Foreign = $noise.Foreign; Exit = $proc.ExitCode
        Cl = [math]::Round($sum.CL, 1); Link = [math]::Round($sum.Link, 1); Exec = [math]::Round($sum.Exec, 1)
    }
}

function Write-Row([string]$scn, [int]$run, $r) {
    $line = @(
        (Get-Date -Format 's'), $sha, $hostName, $scn, $Configuration, $run, $r.Step,
        $r.Seconds.ToString($cnt), $r.PeakTree, $r.PeakSys, $r.Cpu, $r.Foreign, $r.Exit,
        $r.Cl.ToString($cnt), $r.Link.ToString($cnt), $r.Exec.ToString($cnt)
    ) -join ','
    Add-Content -Encoding utf8 -Path $OutCsv -Value $line
    Write-Host ("  [{0}] run {1} {2,-10} {3,8:N1} s  RAM arbre {4} Mo  (cpu avant {5}%, cl/link tiers {6}, exit {7})" -f $scn, $run, $r.Step, $r.Seconds, $r.PeakTree, $r.Cpu, $r.Foreign, $r.Exit)
    if ($r.Exit -ne 0) { throw "Etape '$($r.Step)' echouee (code $($r.Exit)) : voir le journal du dossier build/measure/logs." }
}

$logDir = Join-Path (Split-Path -Parent $OutCsv) 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Invoke-Configure([string]$scn, [int]$run) {
    $a = @('--preset', 'msvc')
    if ($VcpkgInstalledDir) { $a += "-DVCPKG_INSTALLED_DIR=$VcpkgInstalledDir" }
    $r = Invoke-Measured 'configure' 'cmake' $a (Join-Path $logDir "$scn-$Configuration-$run-configure.log")
    Write-Row $scn $run $r
}

function Invoke-Build([string]$scn, [int]$run) {
    $a = @('--build', '--preset', "msvc-$($Configuration.ToLower())", '--parallel', "$Parallel", '--target') + $Target + @(
        '--', '/p:UseMultiToolTask=true', '/p:EnforceProcessCountAcrossBuilds=true',
        "/p:MultiProcMaxCount=$Parallel", "/p:CL_MPCount=$Parallel", '/clp:PerformanceSummary;Summary', '/v:m')
    $r = Invoke-Measured 'build' 'cmake' $a (Join-Path $logDir "$scn-$Configuration-$run-build.log")
    Write-Row $scn $run $r
}

function Invoke-Ctest([string]$scn, [int]$run) {
    $a = @('--preset', "msvc-$($Configuration.ToLower())", '-R', $TestRegex)
    $r = Invoke-Measured 'ctest' 'ctest' $a (Join-Path $logDir "$scn-$Configuration-$run-ctest.log")
    Write-Row $scn $run $r
}

function Invoke-CiScenario {
    # Lecture seule : gh run list / gh run view. Sortie : docs/source/build-baseline-ci.csv-like sur stdout + CSV.
    [Console]::OutputEncoding = [Text.Encoding]::UTF8
    $repo = (git config --get remote.origin.url) -replace '.*github.com[:/]', '' -replace '\.git$', ''
    $runsJson = gh run list --repo $repo --workflow ci.yml --status success --limit $CiRuns --json databaseId,headSha,event,createdAt,updatedAt | ConvertFrom-Json
    $ciCsv = Join-Path (Split-Path -Parent $OutCsv) 'ci-results.csv'
    'run_id,sha,event,created,run_total_s,job,job_s,step,step_s' | Set-Content -Encoding utf8 $ciCsv
    foreach ($run in $runsJson) {
        $total = ([datetime]$run.updatedAt - [datetime]$run.createdAt).TotalSeconds
        $jobs = (gh run view $run.databaseId --repo $repo --json jobs | ConvertFrom-Json).jobs
        foreach ($job in $jobs) {
            if (-not $job.completedAt -or -not $job.startedAt) { continue }
            $js = ([datetime]$job.completedAt - [datetime]$job.startedAt).TotalSeconds
            foreach ($st in $job.steps) {
                if (-not $st.completedAt -or -not $st.startedAt) { continue }
                $ss = ([datetime]$st.completedAt - [datetime]$st.startedAt).TotalSeconds
                '{0},{1},{2},{3},{4},"{5}",{6},"{7}",{8}' -f $run.databaseId, $run.headSha.Substring(0, 8), $run.event,
                    $run.createdAt, [math]::Round($total), $job.name, [math]::Round($js), $st.name, [math]::Round($ss) |
                    Add-Content -Encoding utf8 $ciCsv
            }
        }
    }
    Write-Host "CI : $ciCsv"
}

# Environnement (une fois, sur stdout) : a copier dans le document.
Write-Host "== Environnement ==" -ForegroundColor Cyan
Write-Host ("SHA {0} | {1} | CPU {2} | RAM {3:N1} Go | OS {4}" -f $sha, $hostName,
    (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim(),
    ((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB),
    (Get-CimInstance Win32_OperatingSystem).Caption)
Write-Host ("Qt {0} | cmake {1}" -f (Split-Path (Split-Path $env:QT_ROOT -Parent) -Leaf), ((cmake --version | Select-Object -First 1)))
$ccm = Get-ChildItem (Join-Path $BuildDir 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($ccm) { Write-Host ("MSVC " + ((Select-String -Path $ccm.FullName -Pattern 'CMAKE_CXX_COMPILER_VERSION "' | Select-Object -First 1).Line)) }

$restore = @{}
try {
    foreach ($scn in $Scenario) {
        Write-Host "== $scn ($Configuration, $Runs run(s), parallel $Parallel) ==" -ForegroundColor Cyan
        if ($scn -eq 'ci') { Invoke-CiScenario; continue }
        for ($run = 1; $run -le $Runs; $run++) {
            switch ($scn) {
                'configure-fresh' {
                    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
                    Invoke-Configure $scn $run
                }
                'configure-noop' { Invoke-Configure $scn $run }
                'build-cold' {
                    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
                    Invoke-Configure $scn $run
                    Invoke-Build $scn $run
                }
                'build-noop' { Invoke-Build $scn $run }
                'build-tests' { Invoke-Build $scn $run }
                'tests' { Invoke-Ctest $scn $run }
                default {
                    $f = Resolve-Path $touchFiles[$scn]
                    if (-not $restore.ContainsKey($f.Path)) { $restore[$f.Path] = (Get-Item $f).LastWriteTime }
                    (Get-Item $f).LastWriteTime = Get-Date
                    Invoke-Build $scn $run
                }
            }
        }
    }
}
finally {
    foreach ($k in $restore.Keys) { (Get-Item $k).LastWriteTime = $restore[$k] }
}
