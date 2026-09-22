# SPDX-License-Identifier: Apache-2.0
# Critères d'acceptation de l'audit « marine plein cadre » (lots A à G) :
# numérise l'image d'exemple avec openstitch-cli digitize, relit le DST avec
# openstitch-cli stats, et vérifie chaque critère. Trop lent (~2 min en
# Release) pour ctest : à lancer à la main.
#
#   .\scripts\acceptance-sample.ps1 -Image sample\image.png -Dpi 146.8
param(
    [string]$Image = "sample\0215a7e5-97b1-44cb-8b5a-2b5a67e9efbe.png",
    [double]$Dpi = 146.8,
    [string]$Cli = "build\msvc\apps\cli\Release\openstitch-cli.exe",
    [int]$MovesBefore = 2916
)
$ErrorActionPreference = "Stop"
$dst = Join-Path $env:TEMP "openstitch-acceptance.dst"

$digitize = & $Cli digitize $Image $dst --dpi $Dpi 2>$null
if ($LASTEXITCODE -ne 0) { Write-Error "digitize a échoué"; exit 1 }
$stats = & $Cli stats $dst
if ($LASTEXITCODE -ne 0) { Write-Error "stats a échoué"; exit 1 }
$digitize | Where-Object { $_ -notmatch '^\s+!' } | Write-Host
Write-Host "---"
$stats | Write-Host
Write-Host "---"

function Grab([string[]]$lines, [string]$pattern) {
    foreach ($l in $lines) {
        if ($l -match $pattern) { return [double]($Matches[1] -replace ',', '.') }
    }
    throw "Ligne introuvable : $pattern"
}

$uncovered = Grab $digitize 'Surface non couverte.*:\s*([\d.,]+)\s*%'
$angles = Grab $digitize 'Angles de remplissage \((\d+) distincts\)'
$small = Grab $digitize 'Objets brodés < \d+ mm² :\s*(\d+)'
$moves = Grab $stats '^Déplacements\s*:\s*(\d+)'
$longNoTrim = Grab $stats 'sans coupe :\s*(\d+)'
$shortPct = Grab $stats 'Points < [\d.]+ mm\s*:\s*\d+ \(([\d.,]+) %\)'

$checks = @(
    @{ Name = "Surface non couverte hors fond < 2 %"; Ok = $uncovered -lt 2; Value = "$uncovered %" },
    @{ Name = "Au moins 4 angles de remplissage"; Ok = $angles -ge 4; Value = $angles },
    @{ Name = "Aucun déplacement > 3 mm sans coupe"; Ok = $longNoTrim -eq 0; Value = $longNoTrim },
    @{ Name = "Déplacements divisés par 3 (avant : $MovesBefore)"; Ok = $moves -le [math]::Floor($MovesBefore / 3); Value = $moves },
    @{ Name = "Points < 0,5 mm hors verrous < 1 %"; Ok = $shortPct -lt 1; Value = "$shortPct %" },
    @{ Name = "Aucun objet brodé < 3 mm²"; Ok = $small -eq 0; Value = $small }
)
$failed = 0
foreach ($c in $checks) {
    $mark = if ($c.Ok) { "OK  " } else { $failed++; "ÉCHEC" }
    Write-Host ("[{0}] {1} : {2}" -f $mark, $c.Name, $c.Value)
}
exit $failed
