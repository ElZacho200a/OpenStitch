# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Project,
    [string]$Executable = 'build/msvc/tests/bench/Release/openstitch-studio-bench.exe',
    [string]$OutputDirectory = 'build/studio-bench',
    [ValidateRange(2, 100000)][int]$Frames = 240,
    [ValidateRange(0, 10000)][int]$Warmup = 30
)
$ErrorActionPreference = 'Stop'
$benchPath = (Resolve-Path -LiteralPath $Executable).Path
$projectPath = (Resolve-Path -LiteralPath $Project).Path
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
if ($env:QT_QPA_PLATFORM -in @('offscreen', 'minimal')) {
    throw 'Use the native Windows platform for presented-frame measurements.'
}

# Each scene/backend uses a fresh process. Keep these windows unobscured and run on AC power;
# this is not a CI speed threshold or a hardware-independent claim.
$reports = @()
foreach ($scene in @('1000', '10000', '50000', '100000', 'real')) {
    foreach ($backend in @('quick', 'widgets')) {
        $jsonPath = Join-Path $outputPath "$scene-$backend.json"
        & $benchPath $projectPath --backend $backend --scene $scene --frames $Frames --warmup $Warmup --json $jsonPath
        if ($LASTEXITCODE -ne 0) {
            throw "Benchmark failed for $scene/$backend (exit $LASTEXITCODE)."
        }
        $reports += Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    }
}

$summary = @()
foreach ($scene in @('1000', '10000', '50000', '100000', 'real')) {
    $rows = @($reports | ForEach-Object { $_.results } | Where-Object { $_.scene -eq $scene })
    if ($rows.Count -ne 2 -or $rows[0].segments_hash -ne $rows[1].segments_hash -or
        $rows[0].actual_stitches -ne $rows[1].actual_stitches) {
        throw "The two backends did not receive equal stitch scenes: $scene."
    }
    foreach ($row in $rows) {
        $summary += [pscustomobject]@{
            Scene = $scene
            Stitches = $row.actual_stitches
            Backend = $row.backend
            CompletionHz = [math]::Round($row.completion_fps, 2)
            FrameMedianMs = [math]::Round($row.frame_interval_ms.median, 3)
            FrameP95Ms = [math]::Round($row.frame_interval_ms.p95, 3)
            LocalLatencyP95Ms = [math]::Round($row.local_interaction_to_completion_ms.p95, 3)
            SceneBuildMs = [math]::Round($row.scene_build_ms, 3)
            RasterMs = $row.preview_raster_ms
            InstallToFirstMs = [math]::Round($row.install_to_first_completion_ms, 3)
            GeometryBuilds = $row.geometry_build_count
            ProcessWorkingSetMb = $row.process_working_set_mb
        }
    }
}
$metadata = [ordered]@{
    project_sha256 = (Get-FileHash -LiteralPath $projectPath -Algorithm SHA256).Hash
    executable_sha256 = (Get-FileHash -LiteralPath $benchPath -Algorithm SHA256).Hash
    qsg_rhi_backend_requested = $env:QSG_RHI_BACKEND
    qsg_render_loop_requested = $env:QSG_RENDER_LOOP
    qt_scale_factor_requested = $env:QT_SCALE_FACTOR
    frames = $Frames
    warmup = $Warmup
    cpu = @(Get-CimInstance Win32_Processor | Select-Object Name)
    video_controllers = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion)
    note = 'GPU controller inventory does not prove which adapter Qt used. Quick frameSwapped and Widgets paintEvent use different completion boundaries.'
}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'machine.json') -Encoding utf8
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'summary.json') -Encoding utf8
$summary | Format-Table -AutoSize
Write-Output "Raw samples and machine metadata: $outputPath"
