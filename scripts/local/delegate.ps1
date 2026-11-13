<#
.SYNOPSIS
  Point d'entree unique "local first" : delegue une mission a Ollama (niveau 1) ou a OpenCode+Ollama (niveaux 2-3).
  Tout est ASCII a dessein (PowerShell 5.1 lit les scripts sans BOM en ANSI).

.DESCRIPTION
  Niveau 0 : pas de LLM -> lancer directement le script/commande (hors de cet outil).
  Niveau 1 : -Level 1  appel direct /api/chat (resume, explication, revue d'erreur). Reponse tronquee a -MaxChars.
  Niveau 2 : -Level 2  OpenCode autonome sur une tache (perimetre -Scope, verification -Verify).
  Niveau 3 : -Level 3  OpenCode autonome sur un chantier (contexte plus grand, budget plus long).
  Niveau 4 : enchainer plusieurs appels (analyse -> impl -> relecture) en ecrivant chaque mission ; jamais 2 ecrivains en parallele.
  Niveau 5 : escalade a Claude : -LogClaude "raison" (ne lance rien, compte l'intervention).

  Sortie : un rapport compact (SUCCESS/PARTIAL/BLOCKED/FAILED). Details dans build/local-first/<id>/ (hors commits).
  Le statut est calcule MECANIQUEMENT (fichiers changes, perimetre, commande de verification), jamais d'apres l'auto-declaration du modele.
  Garde-fous : modele force ollama/* ; cout rapporte par OpenCode doit etre 0 ; fichiers hors perimetre restaures ;
  jamais de commit/push/merge par l'agent (interdit dans le prompt et dans scripts/local/opencode.local-first.json).

.EXAMPLE
  scripts/local/delegate.ps1 -Id fix-x -Level 2 -Mission "Corriger ..." -Scope libs/core/src/x.cpp,tests/unit/core/test_x.cpp -Verify "scripts/qrun.ps1 ..."
  scripts/local/delegate.ps1 -Level 1 -Mission "Resume ce fichier" -File libs/core/src/x.cpp
  scripts/local/delegate.ps1 -Stats
#>
param(
    [string]$Id = ('m' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$Mission,
    [string]$MissionFile,
    [ValidateSet(1, 2, 3)][int]$Level = 2,
    [string[]]$Scope = @(),
    [string]$Verify,
    [string[]]$File = @(),
    [int]$MaxAttempts = 2,
    [int]$TimeoutMin = 0,
    [int]$MaxChars = 1500,
    [string]$Model = 'ollama/openstitch-agent:latest',
    [switch]$Stats,
    [string]$LogClaude
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$root = (git rev-parse --show-toplevel 2>$null)
if (-not $root) { $root = (Get-Location).Path }
$root = (Resolve-Path $root).Path
$base = Join-Path $root 'build/local-first'
New-Item -ItemType Directory -Force -Path $base | Out-Null
$statsFile = Join-Path $base 'stats.jsonl'

function Add-Stat($obj) {
    ($obj | ConvertTo-Json -Compress -Depth 5) | Add-Content -LiteralPath $statsFile -Encoding UTF8
}

if ($Stats) {
    if (-not (Test-Path $statsFile)) { Write-Output 'aucune statistique'; exit 0 }
    $rows = Get-Content $statsFile -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json }
    $loc = @($rows | Where-Object { $_.kind -eq 'local' })
    $cl = @($rows | Where-Object { $_.kind -in @('claude-escalation', 'claude-direct') })
    Write-Output ("appels locaux : {0} (SUCCESS {1}, PARTIAL {2}, BLOCKED {3}, FAILED {4})" -f $loc.Count,
        @($loc | ? { $_.status -eq 'SUCCESS' }).Count, @($loc | ? { $_.status -eq 'PARTIAL' }).Count,
        @($loc | ? { $_.status -eq 'BLOCKED' }).Count, @($loc | ? { $_.status -eq 'FAILED' }).Count)
    Write-Output ("interventions Claude : {0} (escalades {1}, edits directs signales {2})" -f $cl.Count,
        @($cl | ? { $_.kind -eq 'claude-escalation' }).Count, @($cl | ? { $_.kind -eq 'claude-direct' }).Count)
    exit 0
}

if ($LogClaude) {
    Add-Stat @{ kind = 'claude-escalation'; ts = (Get-Date -Format o); reason = $LogClaude }
    Write-Output "escalade enregistree : $LogClaude"; exit 0
}

if ($MissionFile) { $Mission = Get-Content -LiteralPath $MissionFile -Raw -Encoding UTF8 }
if (-not $Mission) { Write-Output 'BLOCKED: -Mission ou -MissionFile requis'; exit 2 }
if ($Model -notmatch '^ollama(-oai)?/') { Write-Output "BLOCKED: modele '$Model' refuse (ollama/* uniquement)"; exit 2 }
$modelTag = $Model -replace '^ollama(-oai)?/', ''

# --- preflight ---------------------------------------------------------------
try { $tags = Invoke-RestMethod -Uri http://127.0.0.1:11434/api/tags -TimeoutSec 5 }
catch { Write-Output 'BLOCKED: Ollama injoignable (ollama serve)'; exit 2 }
if (-not ($tags.models | Where-Object { $_.name -eq $modelTag -or $_.name -eq "${modelTag}:latest" })) { Write-Output "BLOCKED: modele $modelTag absent (ollama list)"; exit 2 }

$dir = Join-Path $base $Id
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$sw = [Diagnostics.Stopwatch]::StartNew()

function Write-Utf8($path, $text) { [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false))) }

# --- niveau 1 : appel direct -------------------------------------------------
if ($Level -eq 1) {
    $ctxText = ''
    foreach ($f in $File) {
        $p = Join-Path $root $f
        if (Test-Path $p) { $ctxText += "`n### $f`n" + (Get-Content -LiteralPath $p -Raw -Encoding UTF8) }
    }
    $body = @{ model = $modelTag; stream = $false; think = $false; keep_alive = '30m'
        options = @{ num_ctx = 16384; temperature = 0.1 }
        messages = @(
            @{ role = 'system'; content = 'Reponds en francais, de facon concise et factuelle. Si une information manque, dis-le.' },
            @{ role = 'user'; content = ($Mission + $ctxText) }) } | ConvertTo-Json -Depth 6
    try {
        $r = Invoke-RestMethod -Uri http://127.0.0.1:11434/api/chat -Method Post -TimeoutSec 600 `
            -Body ([Text.Encoding]::UTF8.GetBytes($body)) -ContentType 'application/json; charset=utf-8'
        $ans = $r.message.content
        Write-Utf8 (Join-Path $dir 'answer.txt') $ans
        $status = if ($ans) { 'SUCCESS' } else { 'FAILED' }
    } catch { $ans = "erreur: $($_.Exception.Message)"; $status = 'FAILED' }
    Add-Stat @{ kind = 'local'; ts = (Get-Date -Format o); id = $Id; level = 1; status = $status; secs = [int]$sw.Elapsed.TotalSeconds; provider = 'ollama' }
    $short = if ($ans.Length -gt $MaxChars) { $ans.Substring(0, $MaxChars) + "`n[... tronque, complet : build/local-first/$Id/answer.txt]" } else { $ans }
    Write-Output "[$Id] niveau 1 : $status"; Write-Output $short
    exit $(if ($status -eq 'SUCCESS') { 0 } else { 1 })
}

# --- niveaux 2-3 : OpenCode ---------------------------------------------------
if ($TimeoutMin -le 0) { $TimeoutMin = if ($Level -eq 3) { 60 } else { 20 } }
$ocCfg = Join-Path $PSScriptRoot 'opencode.local-first.json'

function Get-DirtyMap {
    $map = @{}
    $lines = git -C $root status --porcelain --untracked-files=all 2>$null
    foreach ($l in $lines) {
        if ($l.Length -lt 4) { continue }
        $p = $l.Substring(3).Trim('"'); if ($p -match ' -> ') { $p = ($p -split ' -> ')[-1] }
        $full = Join-Path $root $p
        $map[$p] = if (Test-Path -LiteralPath $full -PathType Leaf) { (Get-FileHash -LiteralPath $full -Algorithm SHA1).Hash } else { 'deleted' }
    }
    return $map
}

function Test-InScope($path) {
    if ($Scope.Count -eq 0) { return $true }
    foreach ($s in $Scope) {
        $s2 = $s.Replace('\', '/').TrimEnd('/')
        if ($path -eq $s2 -or $path -like "$s2/*" -or $path -like $s2) { return $true }
    }
    return $false
}

$before = Get-DirtyMap
$scopeTxt = if ($Scope.Count) { ($Scope | ForEach-Object { "- $_" }) -join "`n" } else { '- (aucune restriction declaree : reste minimal)' }
$verTxt = if ($Verify) { "Commande de verification a faire passer (lance-la toi-meme) :`n$Verify" } else { 'Aucune commande de verification imposee : verifie par compilation/tests cibles si possible.' }
$basePrompt = @"
MISSION $Id
$Mission

PERIMETRE (tu ne modifies QUE ces chemins) :
$scopeTxt

CONTRAINTES : lis AGENTS.md/CLAUDE.md du depot ; ne fais JAMAIS git commit/push/merge/reset/checkout ; ne supprime aucun travail non sauvegarde ;
aucun secret dans les fichiers ; recherche les symboles avant de lire ; lis par plages ; ne touche pas aux fichiers deja modifies hors perimetre.
$verTxt

UTILISE REELLEMENT les outils d'edition pour modifier les fichiers (ne te contente pas de decrire). Termine par une ligne :
RAPPORT: <resume en 3 phrases max, risques, blocage eventuel>
"@

$attempt = 0; $status = 'FAILED'; $changed = @(); $violations = @(); $verifyCode = $null; $verifyTail = ''; $cost = 0.0; $providers = @(); $lastText = ''
$prompt = $basePrompt
while ($attempt -lt $MaxAttempts) {
    $attempt++
    $pf = Join-Path $dir "prompt$attempt.txt"; Write-Utf8 $pf $prompt
    $out = Join-Path $dir "events$attempt.jsonl"; $err = Join-Path $dir "stderr$attempt.txt"
    $env:OPENCODE_CONFIG = $ocCfg
    # sortie ecrite EN DIRECT dans events<N>.jsonl (suivi possible pendant le run)
    $oc = Join-Path $env:APPDATA 'npm/opencode.cmd'
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'
    $psi.WorkingDirectory = $root
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.Arguments = "/d /c `"`"$oc`" run --auto --format json --model $Model --title $Id-$attempt -f `"$pf`" `"Execute la mission decrite dans le fichier joint.`" > `"$out`" 2> `"$err`"`""
    $proc = [Diagnostics.Process]::Start($psi)
    if (-not $proc.WaitForExit($TimeoutMin * 60000)) { & taskkill /PID $proc.Id /T /F | Out-Null; $timedOut = $true } else { $timedOut = $false }
    $evText = if (Test-Path $out) { [IO.File]::ReadAllText($out, [Text.Encoding]::UTF8) } else { '' }

    # analyse des evenements : texte final, cout, fournisseur
    foreach ($line in ($evText -split "`n")) {
        if (-not $line.Trim().StartsWith('{')) { continue }
        try { $ev = $line | ConvertFrom-Json } catch { continue }
        if ($ev.type -eq 'text' -and $ev.part.text) { $lastText = $ev.part.text }
        if ($ev.part.cost) { $cost += [double]$ev.part.cost }
        if ($ev.part.providerID) { $providers += $ev.part.providerID }
    }

    # fichiers changes par l'agent = difference de hash avant/apres
    $after = Get-DirtyMap
    $changed = @(); foreach ($k in $after.Keys) { if (-not $before.ContainsKey($k) -or $before[$k] -ne $after[$k]) { $changed += $k } }
    foreach ($k in $before.Keys) { if (-not $after.ContainsKey($k)) { $changed += $k } }
    $changed = @($changed | Sort-Object -Unique)
    $violations = @($changed | Where-Object { -not (Test-InScope $_) })
    foreach ($v in $violations) {   # restauration des seuls fichiers touches hors perimetre
        $full = Join-Path $root $v
        if (-not $before.ContainsKey($v)) {
            $tracked = git -C $root ls-files --error-unmatch -- $v 2>$null
            if ($tracked) { git -C $root checkout -- $v 2>$null | Out-Null } elseif (Test-Path $full) { Remove-Item -LiteralPath $full -Force }
        }
    }
    $changed = @($changed | Where-Object { Test-InScope $_ })

    $verifyCode = $null
    if ($Verify -and $changed.Count -gt 0) {
        $vo = & powershell -NoProfile -Command $Verify 2>&1 | Out-String
        $verifyCode = $LASTEXITCODE
        Write-Utf8 (Join-Path $dir "verify$attempt.txt") $vo
        $verifyTail = (($vo -split "`n") | Select-Object -Last 40) -join "`n"
    }

    $okVerify = (-not $Verify) -or ($verifyCode -eq 0)
    if ($changed.Count -gt 0 -and $okVerify -and $violations.Count -eq 0 -and -not $timedOut) { $status = 'SUCCESS'; break }
    if ($changed.Count -gt 0 -and $okVerify) { $status = 'PARTIAL' }
    elseif ($lastText -match 'BLOQU|BLOCKED') { $status = 'BLOCKED' }
    else { $status = 'FAILED' }

    # reprise : prompt reformule avec la cause locale, perimetre inchange
    $why = if ($timedOut) { "delai de $TimeoutMin min depasse" } elseif ($changed.Count -eq 0) { 'AUCUN fichier du perimetre n a ete modifie : tu dois appeler l outil d edition' } elseif (-not $okVerify) { "la verification a echoue (code $verifyCode). Fin de sortie :`n$verifyTail" } else { 'fichiers hors perimetre modifies puis restaures : reste dans le perimetre' }
    $prompt = $basePrompt + "`n`nTENTATIVE PRECEDENTE ECHOUEE : $why`nCorrige cela de facon minimale."
    $before = Get-DirtyMap
}

# preuve positive : le modele local est charge dans Ollama apres le run (l'inference est passee par lui)
$loaded = $false
try { $ps = Invoke-RestMethod -Uri http://127.0.0.1:11434/api/ps -TimeoutSec 5; $loaded = [bool]($ps.models | Where-Object { $_.name -eq $modelTag -or $_.name -eq "${modelTag}:latest" }) } catch {}
$providerOk = $loaded -and ($providers | Where-Object { $_ -and $_ -notin @('ollama','ollama-oai') }).Count -eq 0 -and $cost -eq 0
if (-not $providerOk) { $status = 'FAILED' }
$report = [ordered]@{
    id = $Id; level = $Level; status = $status; attempts = $attempt; seconds = [int]$sw.Elapsed.TotalSeconds
    model = $Model; providers = @($providers | Sort-Object -Unique); cost = $cost; provider_ok = $providerOk
    changed = $changed; scope_violations_restored = $violations; verify_cmd = $Verify; verify_exit = $verifyCode
    agent_note = ($lastText -split "`n" | Where-Object { $_ -match '^RAPPORT:' } | Select-Object -Last 1)
    next = if ($status -eq 'SUCCESS') { 'relire le diff (git diff -- <fichiers>) et committer' } elseif ($status -eq 'BLOCKED') { 'reduire le perimetre ou fournir le contexte manquant' } else { 'relancer localement avec perimetre reduit ; sinon -LogClaude et escalade' }
}
Write-Utf8 (Join-Path $dir 'report.json') ($report | ConvertTo-Json -Depth 5)
Add-Stat @{ kind = 'local'; ts = (Get-Date -Format o); id = $Id; level = $Level; status = $status; attempts = $attempt; secs = $report.seconds; provider = 'ollama'; cost = $cost; files = $changed.Count }

Write-Output ("[{0}] {1} | niveau {2} | essais {3} | {4}s | fournisseur ok: {5}" -f $Id, $status, $Level, $attempt, $report.seconds, $providerOk)
Write-Output ("fichiers: " + ($(if ($changed.Count) { $changed -join ', ' } else { '(aucun)' })))
if ($Verify) { Write-Output "verification: exit $verifyCode" }
if ($violations.Count) { Write-Output ("hors perimetre (restaures): " + ($violations -join ', ')) }
if ($report.agent_note) { Write-Output $report.agent_note }
Write-Output "suite: $($report.next) | details: build/local-first/$Id/"
exit $(if ($status -eq 'SUCCESS') { 0 } elseif ($status -eq 'BLOCKED') { 2 } else { 1 })
