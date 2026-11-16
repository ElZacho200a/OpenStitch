<#
.SYNOPSIS
  Envoie une tâche (gabarit rempli) à openstitch-coder, contrôle la réponse, un seul retour si besoin.

.DESCRIPTION
  1. appelle Ollama avec think=false et keep_alive ;
  2. lance scripts/ollama/check-draft.ps1 sur la réponse ;
  3. si NON CONFORME : UN seul retour au modèle avec les problèmes listés, puis re-contrôle ;
  4. s'arrête là : conforme (exit 0), non conforme (exit 1) ou BLOQUÉ (exit 2).
  Rien n'est écrit dans le dépôt : avec -Extract les fichiers vont dans un dossier de brouillon.
  Après un `ollama create`, faire d'abord `ollama stop openstitch-coder` (le modèle chargé reste l'ancien).

.EXAMPLE
  .\scripts\ollama\ask.ps1 -TaskFile tache.txt -Whitelist tests/unit/core/test_x.cpp -Extract C:\w\draft
#>
param(
    [Parameter(Mandatory = $true)][string]$TaskFile,
    [Parameter(Mandatory = $true)][string[]]$Whitelist,
    [string]$Extract,
    [string]$Model = 'openstitch-coder',
    [string]$OutDir = (Join-Path $env:TEMP 'ollama-drafts')
)

[Console]::OutputEncoding = [Text.Encoding]::UTF8
$check = Join-Path $PSScriptRoot 'check-draft.ps1'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

function Invoke-Model([string]$prompt) {
    $body = @{ model = $Model; prompt = $prompt; stream = $false; think = $false; keep_alive = '30m' } | ConvertTo-Json
    $bytes = [Text.Encoding]::UTF8.GetBytes($body)
    $r = Invoke-RestMethod -Uri http://localhost:11434/api/generate -Method Post -Body $bytes `
        -ContentType 'application/json; charset=utf-8' -TimeoutSec 600
    return $r.response
}

try { $null = Invoke-RestMethod -Uri http://localhost:11434/api/tags -TimeoutSec 5 }
catch { Write-Output 'Ollama injoignable : lancer `ollama serve`.'; exit 1 }

$task = Get-Content -LiteralPath $TaskFile -Raw -Encoding UTF8
$attempt = 1
$response = Invoke-Model $task
$file = Join-Path $OutDir "$stamp-essai1.txt"
[IO.File]::WriteAllText($file, $response, (New-Object Text.UTF8Encoding($false)))
$report = & $check -ResponseFile $file -Whitelist $Whitelist -TaskFile $TaskFile
$code = $LASTEXITCODE
Write-Output "--- essai 1 : code $code"; $report

if ($code -eq 1) {
    $attempt = 2
    $retry = $task + "`n`nTA RÉPONSE PRÉCÉDENTE A ÉTÉ REJETÉE PAR UN CONTRÔLE AUTOMATIQUE :`n" + ($report -join "`n") +
             "`nCorrige ces points ET respecte toujours ces deux règles : (1) noms de TEST_CASE sans AUCUN caractère accentué ; " +
             "(2) n'écris QUE les nouveaux cas demandés, ne recopie JAMAIS un TEST_CASE présent dans la tâche. " +
             "Renvoie la réponse complète au même format."
    $response = Invoke-Model $retry
    $file = Join-Path $OutDir "$stamp-essai2.txt"
    [IO.File]::WriteAllText($file, $response, (New-Object Text.UTF8Encoding($false)))
    $report = & $check -ResponseFile $file -Whitelist $Whitelist -TaskFile $TaskFile
    $code = $LASTEXITCODE
    Write-Output "--- essai 2 : code $code"; $report
}

if ($code -eq 0 -and $Extract) { $null = & $check -ResponseFile $file -Whitelist $Whitelist -TaskFile $TaskFile -Extract $Extract }
Write-Output "Réponse brute conservée : $file"
if ($code -ne 0) { Write-Output 'Brouillon NON accepté : ne pas l''appliquer tel quel.' }
exit $code
