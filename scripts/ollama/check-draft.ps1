<#
.SYNOPSIS
  Contrôle automatique d'un brouillon écrit par Ollama (openstitch-coder), AVANT de l'appliquer.

.DESCRIPTION
  Le modèle de 9 B ignore certaines règles même quand elles sont explicites (ex. noms de TEST_CASE
  en ASCII pur). Ce script les vérifie de façon déterministe et, avec -Extract, écrit les fichiers
  du brouillon dans un dossier (jamais dans le dépôt directement).

  Contrôles : réponse non vide ; BLOQUÉ détecté ; section HYPOTHÈSES présente ; chaque fichier est
  dans la liste blanche ; pas de chemin absolu ni de « .. » ; pas d'élision (« ... », « reste inchangé ») ;
  noms de TEST_CASE en ASCII pur ; pas de M_PI ni de `generate_sequence(` ; aucun test supprimé n'est
  détectable ici (comparer avec git diff après application).

  Code de sortie : 0 = conforme, 1 = non conforme, 2 = le modèle a répondu BLOQUÉ.

.EXAMPLE
  .\scripts\ollama\check-draft.ps1 -ResponseFile reponse.txt -Whitelist tests/unit/core/test_x.cpp -Extract C:\w\draft
#>
param(
    [Parameter(Mandatory = $true)][string]$ResponseFile,
    [Parameter(Mandatory = $true)][string[]]$Whitelist,
    [string]$Extract,
    # Fichier de la tâche : les TEST_CASE qu'il contient (exemple, contexte) existent déjà ailleurs,
    # le brouillon ne doit pas les redéfinir (doublon de nom = erreur Catch2).
    [string]$TaskFile
)

[Console]::OutputEncoding = [Text.Encoding]::UTF8
$text = Get-Content -LiteralPath $ResponseFile -Raw -Encoding UTF8
$problems = New-Object System.Collections.Generic.List[string]

if ([string]::IsNullOrWhiteSpace($text)) {
    Write-Output 'ECHEC : réponse vide.'
    exit 1
}
if ($text.TrimStart() -match '^BLOQU[ÉE]\s*:') {
    Write-Output ('BLOQUÉ par le modèle : ' + ($text.TrimStart() -split "`n")[0])
    exit 2
}
if ($text -notmatch 'HYPOTH[ÈE]SES\s*:') {
    $problems.Add('section HYPOTHÈSES absente')
}

# Blocs « ### FICHIER: chemin ». Le modèle n'entoure pas toujours le code de balises ``` : le corps va
# jusqu'au prochain fichier ou à la section HYPOTHÈSES, balises éventuelles retirées plus bas.
$pattern = '(?ms)^###\s*FICHIER:\s*(?<path>[^\r\n]+)\r?\n(?<body>.*?)(?=^###\s*FICHIER:|^\s*HYPOTH[ÈE]SES\s*:|\z)'
$blocks = [regex]::Matches($text, $pattern)
if ($blocks.Count -eq 0) {
    $problems.Add('aucun bloc « ### FICHIER: ... » exploitable')
}

$taskNames = @()
if ($TaskFile) {
    $taskText = Get-Content -LiteralPath $TaskFile -Raw -Encoding UTF8
    $taskNames = @([regex]::Matches($taskText, 'TEST_CASE\("(?<n>[^"]*)"') | ForEach-Object { $_.Groups['n'].Value })
}
$norm = { param($p) ($p.Trim() -replace '\\', '/').TrimStart('./') }
$allowed = $Whitelist | ForEach-Object { & $norm $_ }

foreach ($b in $blocks) {
    $path = & $norm $b.Groups['path'].Value
    $body = $b.Groups['body'].Value
    $body = ($body -replace '(?s)^\s*```[a-zA-Z+]*\r?\n', '') -replace '(?s)\r?\n```\s*$', ''
    $body = $body.TrimEnd()
    if ($allowed -notcontains $path) { $problems.Add("fichier hors liste blanche : $path") }
    if ($path -match '^[A-Za-z]:' -or $path.StartsWith('/') -or $path -match '(^|/)\.\.(/|$)') {
        $problems.Add("chemin interdit : $path")
    }
    if ($body -match '(?m)^\s*(//|#)?\s*\.\.\.\s*$' -or $body -match '(?i)reste inchang|unchanged|rest of the file') {
        $problems.Add("élision détectée dans $path (le fichier doit être complet)")
    }
    foreach ($m in [regex]::Matches($body, 'TEST_CASE\("(?<n>[^"]*)"')) {
        $name = $m.Groups['n'].Value
        if ($name -cnotmatch '^[\x20-\x7E]+$') { $problems.Add("TEST_CASE non ASCII : « $name »") }
        if ($taskNames -contains $name) { $problems.Add("TEST_CASE déjà fourni dans la tâche (ne pas recopier l'exemple) : « $name »") }
    }
    if ($body -match '\bM_PI\b') { $problems.Add("M_PI interdit dans $path (std::numbers::pi)") }
    if ($body -match 'generate_sequence\s*\(') { $problems.Add("generate_sequence interdit dans $path (effective_sequence)") }
    if ($Extract -and $problems.Count -eq 0) {
        $dest = Join-Path $Extract $path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
        [IO.File]::WriteAllText($dest, $body + "`n", (New-Object Text.UTF8Encoding($false)))
    }
}

if ($problems.Count -gt 0) {
    Write-Output ("NON CONFORME ({0} problème(s)) :" -f $problems.Count)
    $problems | ForEach-Object { Write-Output "  - $_" }
    Write-Output 'Action : un seul retour au modèle avec ces lignes, sinon jeter le brouillon.'
    exit 1
}
Write-Output ("CONFORME : {0} fichier(s) dans la liste blanche, HYPOTHÈSES présentes." -f $blocks.Count)
if ($Extract) { Write-Output "Fichiers écrits sous : $Extract" }
exit 0
