<#
.SYNOPSIS
    Calcule le nombre de taches de compilation sures selon la RAM libre (BUILD-OPT-01.6).
.DESCRIPTION
    MSVC cl.exe consomme ~1,5 Go par unite de traduction lourde (Qt, Clipper2,
    OpenCV ; ~2,5 Go avec Unity Build). Sur une machine 16 Go partagee,
    CL_MPCount = nombre de coeurs provoque du swap. Regle :
        jobs = clamp( floor((RAM_libre_Go - reserve) / Go_par_tache), 1, coeurs )
    -Reserve (defaut 2 Go) laisse de la place a l'OS, a l'IDE et a l'editeur de
    liens. Variable OPENSTITCH_JOBS : force la valeur (CI, mesures reproductibles).
    Sans -Quiet, la sortie est l'entier seul sur stdout (utilisable par
    `-Jobs (& scripts\build-jobs.ps1)`), le detail part sur stderr.
.PARAMETER GoPerJob
    Go de RAM supposes par tache (defaut 1.5 ; 2.5 si Unity/PCH).
.PARAMETER Reserve
    Go laisses libres (defaut 2).
.PARAMETER Cores
    Plafond (defaut : processeurs logiques).
#>
[CmdletBinding()]
param(
    [double]$GoPerJob = 1.5,
    [double]$Reserve = 2.0,
    [int]$Cores = [Environment]::ProcessorCount
)
$ErrorActionPreference = 'Stop'

if ($env:OPENSTITCH_JOBS -match '^\d+$' -and [int]$env:OPENSTITCH_JOBS -ge 1) {
    [Console]::Error.WriteLine("build-jobs: OPENSTITCH_JOBS=$($env:OPENSTITCH_JOBS) (force)")
    Write-Output ([int]$env:OPENSTITCH_JOBS)
    return
}

$os = Get-CimInstance Win32_OperatingSystem
$freeGb = $os.FreePhysicalMemory / 1MB          # Ko -> Go
$jobs = [int][math]::Floor(($freeGb - $Reserve) / $GoPerJob)
$jobs = [math]::Max(1, [math]::Min($jobs, $Cores))
[Console]::Error.WriteLine(("build-jobs: RAM libre {0:N1} Go, reserve {1} Go, {2} Go/tache, {3} coeurs -> {4} taches" -f $freeGb, $Reserve, $GoPerJob, $Cores, $jobs))
Write-Output $jobs
