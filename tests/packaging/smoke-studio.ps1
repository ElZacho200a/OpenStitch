# SPDX-License-Identifier: Apache-2.0
# Native window, real OSP load and actual capture, with developer Qt paths removed.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BundleDir,
    [Parameter(Mandatory)][string]$Project,
    [Parameter(Mandatory)][string]$OutputDir
)
$ErrorActionPreference = 'Stop'
$bundle = (Resolve-Path -LiteralPath $BundleDir).Path
$projectPath = (Resolve-Path -LiteralPath $Project).Path
$exe = Join-Path $bundle 'openstitch_v2_studio.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing Studio executable: $exe" }
$out = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $out -Force | Out-Null
$keys = @('PATH','QT_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORM','QT_QUICK_BACKEND','QT_QPA_FONTDIR','OPENSTITCH_FONTS_DIR','QSG_RHI_BACKEND','QSG_RHI_PREFER_SOFTWARE_RENDERER')
$saved = @{}
foreach ($key in $keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key,'Process') }
$results = @()
try {
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key,$null,'Process') }
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    foreach ($mode in @('native','software')) {
        $png = Join-Path $out "$mode.png"
        if (Test-Path -LiteralPath $png) { throw "Refusing stale screenshot: $png" }
        $arguments = @('"' + $projectPath + '"','--screenshot','"' + $png + '"')
        if ($mode -eq 'software') { $arguments += '--software' }
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $out -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $out "$mode.stdout.log") -RedirectStandardError (Join-Path $out "$mode.stderr.log")
        $watch.Stop()
        if ($process.ExitCode -ne 0) { throw "Studio $mode failed with code $($process.ExitCode); see $out" }
        if (-not (Test-Path -LiteralPath $png -PathType Leaf)) { throw "Studio $mode returned no screenshot" }
        $header = [IO.File]::ReadAllBytes($png)
        if ($header.Length -lt 24 -or [BitConverter]::ToString($header[0..7]) -ne '89-50-4E-47-0D-0A-1A-0A') { throw "Invalid actual PNG: $png" }
        $results += [ordered]@{ mode=$mode; exit_code=$process.ExitCode; process_ms=$watch.Elapsed.TotalMilliseconds; png=$png; png_sha256=(Get-FileHash -LiteralPath $png).Hash }
    }
    [ordered]@{ executable=$exe; executable_sha256=(Get-FileHash -LiteralPath $exe).Hash; project=$projectPath; project_sha256=(Get-FileHash -LiteralPath $projectPath).Hash; path=$env:PATH; results=$results } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'smoke.json') -Encoding utf8
    Write-Host 'Studio native and software smoke tests passed with Qt absent from PATH.'
}
finally {
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key,$saved[$key],'Process') }
}
