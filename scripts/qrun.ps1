<#
.SYNOPSIS
  Lance une commande longue (cmake, ctest, clang-format...) avec une sortie courte
  dans le contexte de Claude, et le journal complet dans un fichier.

.DESCRIPTION
  Succès  : une ligne de statut + les lignes de bilan (« tests passed », « Build succeeded »,
            nombre d'avertissements). Le journal complet reste dans build/qrun-logs/<label>.log.
  Échec   : les lignes d'erreur dédupliquées (max 60) + les 15 dernières lignes + le chemin du
            journal. Lire ensuite UNIQUEMENT la plage utile du journal (Grep -n, puis Read offset/limit).
  Code de sortie = celui de la commande.

.EXAMPLE
  .\scripts\qrun.ps1 build-stitch cmake --build --preset msvc-debug --target openstitch_stitch_generation test_stitch
  .\scripts\qrun.ps1 ctest-stitch ctest --preset msvc-debug -R stitch --output-on-failure
#>
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Label,
    [Parameter(Mandatory = $true, Position = 1, ValueFromRemainingArguments = $true)][string[]]$Command
)

$root = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $root 'build/qrun-logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir (($Label -replace '[^\w.-]', '_') + '.log')

$exe = $Command[0]
$rest = if ($Command.Count -gt 1) { $Command[1..($Command.Count - 1)] } else { @() }

$sw = [Diagnostics.Stopwatch]::StartNew()
$lines = & $exe @rest 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$sw.Stop()
$lines | Set-Content -Encoding utf8 $log

$errPattern = 'error|FAILED|\*\*\*Failed|fatal|LNK\d{4}|C\d{4}:|Assertion|REQUIRE|CHECK|Exception|Timeout|not found'
$sumPattern = 'tests? passed|tests? failed|Build succeeded|Total Test time|Warning\(s\)|Error\(s\)|All tests passed|test cases?:|assertions?:'
$secs = [math]::Round($sw.Elapsed.TotalSeconds, 1)
$warn = @($lines | Select-String -Pattern 'warning [A-Z]*\d+' ).Count

if ($code -eq 0) {
    Write-Output "[qrun] OK  $Label  ($secs s, $($lines.Count) lignes, $warn avertissement(s))  log: $log"
    if ($lines.Count -le 12) { $lines } else {
        $lines | Select-String -Pattern $sumPattern | Select-Object -Last 6 | ForEach-Object { $_.Line }
    }
} else {
    Write-Output "[qrun] ECHEC (code $code)  $Label  ($secs s, $($lines.Count) lignes)  log: $log"
    $lines | Select-String -Pattern $errPattern | ForEach-Object { $_.Line.Trim() } |
        Select-Object -Unique | Select-Object -First 60
    Write-Output '--- fin du journal ---'
    $lines | Select-Object -Last 15
}
exit $code
