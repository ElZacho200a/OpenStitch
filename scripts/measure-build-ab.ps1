<#
.SYNOPSIS
    Mesure temps mur et pic de RAM (somme des cl.exe/link.exe/MSBuild) d'un build de cibles (BUILD-OPT-01.6).
.EXAMPLE
    scripts\measure-build-ab.ps1 -BuildDir build\ab-base -Config Release -Target openstitch_geometry -Jobs 2
.NOTES
    Nettoyer la cible avant (--target X --clean-first fait par le script). Le pic est echantillonne toutes les 500 ms.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$Config = 'Release',
    [Parameter(Mandatory)][string[]]$Target,
    [int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$abs = (Resolve-Path $BuildDir).Path
$args2 = @('--build', $BuildDir, '--config', $Config, '--target') + $Target + @('--clean-first', '--parallel', $Jobs, '--', "/p:CL_MPCount=$Jobs", '/nologo', '/v:q')
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process cmake -ArgumentList $args2 -PassThru -NoNewWindow -RedirectStandardOutput "$BuildDir\measure.out.txt" -RedirectStandardError "$BuildDir\measure.err.txt"
$peak = 0
while (-not $p.HasExited) {
    # Arbre des descendants du build lance ici (les builds des autres sessions sont exclus).
    $all = Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, WorkingSetSize
    $ids = [System.Collections.Generic.HashSet[uint32]]::new(); [void]$ids.Add([uint32]$p.Id)
    do { $n = $ids.Count; foreach ($q in $all) { if ($ids.Contains([uint32]$q.ParentProcessId)) { [void]$ids.Add([uint32]$q.ProcessId) } } } while ($ids.Count -ne $n)
    $ws = ($all | Where-Object { $ids.Contains([uint32]$_.ProcessId) } | Measure-Object WorkingSetSize -Sum).Sum
    if ($ws -gt $peak) { $peak = $ws }
    Start-Sleep -Milliseconds 500
}
$p.WaitForExit(); $sw.Stop()
[pscustomobject]@{ BuildDir = $BuildDir; Exit = $p.ExitCode; Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); PeakGB = [math]::Round($peak / 1GB, 2) } | ConvertTo-Json -Compress
