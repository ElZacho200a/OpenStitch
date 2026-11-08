# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [ValidateRange(1, 30)][int]$Processes = 3,
    [ValidateRange(2, 1000)][int]$Repetitions = 11,
    [ValidateRange(0, 64)][int]$Threads = 0,
    [ValidateSet('logo', 'text', 'tentabrode', 'marine', 'marine-complex')]
    [string[]]$Cases = @('logo', 'text', 'tentabrode', 'marine'),
    [switch]$NoInstrument
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function New-TextBitmap {
    param([string]$Pattern, [string]$Destination)
    $glyphRows = @(Get-Content -LiteralPath $Pattern)
    $scale = 12
    $width = ($glyphRows[0].Length + 2) * $scale
    $height = ($glyphRows.Count + 2) * $scale
    $stride = ($width * 3 + 3) -band -4
    $pixelBytes = $stride * $height
    [byte[]]$white = @(255) * ($width * 3) + @(0) * ($stride - $width * 3)
    $bitmap = [IO.BinaryWriter]::new([IO.File]::Create($Destination))
    try {
        $bitmap.Write([uint16]0x4d42)
        $bitmap.Write([uint32](54 + $pixelBytes))
        $bitmap.Write([uint32]0) # Reserved
        $bitmap.Write([uint32]54)
        $bitmap.Write([uint32]40) # BITMAPINFOHEADER size
        $bitmap.Write([int32]$width)
        $bitmap.Write([int32](-$height)) # Top-down scanlines
        $bitmap.Write([uint16]1)
        $bitmap.Write([uint16]24)
        $bitmap.Write([uint32]0) # BI_RGB
        $bitmap.Write([uint32]$pixelBytes)
        for ($i = 0; $i -lt 4; $i++) { $bitmap.Write([uint32]0) }

        foreach ($glyphRow in (@('') + $glyphRows + @(''))) {
            if ($glyphRow -and ($glyphRow.Length -ne $glyphRows[0].Length -or $glyphRow -notmatch '^[.#]+$')) {
                throw "Invalid text fixture row in $Pattern"
            }
            [byte[]]$row = $white.Clone()
            for ($column = 0; $column -lt $glyphRow.Length; $column++) {
                if ($glyphRow[$column] -eq '#') {
                    [Array]::Clear($row, ($column + 1) * $scale * 3, $scale * 3)
                }
            }
            for ($i = 0; $i -lt $scale; $i++) { $bitmap.Write($row) }
        }
    } finally {
        $bitmap.Dispose()
    }
    $width
}

function Get-Median {
    param([double[]]$Sorted)
    $lower = [int][math]::Floor(($Sorted.Count - 1) / 2)
    $upper = [int][math]::Floor($Sorted.Count / 2)
    ($Sorted[$lower] + $Sorted[$upper]) / 2
}

function Get-Distribution {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $median = Get-Median $sorted
    $deviations = @($sorted | ForEach-Object { [math]::Abs($_ - $median) } | Sort-Object)
    [ordered]@{
        n = $sorted.Count
        min = $sorted[0]
        p50 = $median
        p95 = $sorted[[int][math]::Ceiling(0.95 * $sorted.Count) - 1]
        max = $sorted[-1]
        mad = Get-Median $deviations
    }
}

function Get-PhaseSummary {
    param([object[]]$Samples)
    $metrics = [ordered]@{}
    foreach ($metric in @('wall_ms', 'cpu_ms', 'rss_bytes', 'peak_rss_bytes',
                          'digitize_ms', 'sequence_ms', 'preview_ms', 'fabric_ms')) {
        $values = @($Samples | ForEach-Object { $_.$metric } | Where-Object { $null -ne $_ })
        $metrics[$metric] = Get-Distribution $values
    }
    if ($null -eq $Samples[0].stages) {
        $metrics['stages'] = $null
        return $metrics
    }
    $stages = [ordered]@{}
    foreach ($stage in $Samples[0].stages.PSObject.Properties.Name) {
        $measurements = @($Samples | ForEach-Object { $_.stages.$stage })
        $summary = [ordered]@{}
        foreach ($metric in @('task_ms', 'calls', 'upload_bytes', 'download_bytes', 'buffers')) {
            $summary[$metric] = Get-Distribution @($measurements | ForEach-Object { $_.$metric })
        }
        $stages[$stage] = $summary
    }
    $metrics['stages'] = $stages
    $metrics
}

function Get-MachineInfo {
    $cpu = $env:PROCESSOR_IDENTIFIER
    $processors = @(Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue)
    if ($processors.Count -gt 0) {
        $cpu = ($processors.Name -join ', ')
    } else {
        $registryCpu = Get-ItemPropertyValue -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' `
            -Name ProcessorNameString -ErrorAction SilentlyContinue
        if ($registryCpu) { $cpu = $registryCpu.Trim() }
    }
    $adapters = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
                  Select-Object Name, DriverVersion)
    $nvidia = @()
    if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) {
        $inventory = @(& nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>$null)
        if ($LASTEXITCODE -eq 0) { $nvidia = $inventory }
    }
    [ordered]@{
        cpu = $cpu
        available_logical_processors = [Environment]::ProcessorCount
        os = [Environment]::OSVersion.VersionString
        powershell = $PSVersionTable.PSVersion.ToString()
        video_controllers = $adapters
        nvidia_smi = $nvidia
    }
}

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (-not $Executable) {
    $Executable = Join-Path $root 'build/msvc/tests/bench/Release/openstitch-profile-engine.exe'
}
if (-not $OutputDirectory) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $OutputDirectory = Join-Path $root "build/engine-profile/$stamp"
}
$benchPath = (Resolve-Path -LiteralPath $Executable).Path
if ((Test-Path -LiteralPath $OutputDirectory) -and
    @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count -ne 0) {
    throw "Choose an empty output directory. Found existing files in $OutputDirectory."
}
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$encoding = [Text.UTF8Encoding]::new($false)
$gitRoot = $root.Replace('\', '/')
$revision = & git -c "safe.directory=$gitRoot" -C $root rev-parse HEAD 2>$null
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source commit.' }
$dirty = @(& git -c "safe.directory=$gitRoot" -C $root status --porcelain 2>$null)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source worktree status.' }

$corpus = @{
    logo = @{ Image = 'tests/fixtures/cli/logo_small.png'; Dpi = 30.48; Options = @() }
    text = @{ Image = 'tests/fixtures/performance/text-240mm.txt'; Options = @('--satin-bands') }
    tentabrode = @{ Image = 'tests/fixtures/tentabrode.png'; Dpi = 119.2106667; Options = @('--satin-bands') }
    marine = @{
        Image = 'tests/fixtures/ai-benchmark/marine-synth-01/source.png'
        Dpi = 50.8
        Options = @('--dark-lines', '--linen')
    }
    'marine-complex' = @{
        Image = 'sample/0215a7e5-97b1-44cb-8b5a-2b5a67e9efbe.png'
        Dpi = 122.3433333
        Options = @('--satin-bands', '--dark-lines')
    }
}

$metadata = [ordered]@{
    schema_version = 1
    started_utc = [DateTime]::UtcNow.ToString('o')
    source_commit = $revision
    source_status = $dirty
    executable_sha256 = (Get-FileHash -LiteralPath $benchPath).Hash
    machine = Get-MachineInfo
    threads = $Threads
    instrumented = -not $NoInstrument
    processes_per_case = $Processes
    repetitions_per_process = $Repetitions
    cold = 'First pipeline run in a fresh process; the OS filesystem cache is not flushed.'
    warm = 'Remaining runs in the same process.'
    statistics = 'Midpoint median, nearest-rank p95, and median absolute deviation (MAD).'
    cases = @($Cases | Select-Object -Unique)
}
[IO.File]::WriteAllText((Join-Path $outputPath 'metadata.json'),
                        ($metadata | ConvertTo-Json -Depth 8), $encoding)

$summaries = @()
$previousThreads = $env:OPENSTITCH_THREADS
try {
    if ($Threads -eq 0) {
        Remove-Item Env:OPENSTITCH_THREADS -ErrorAction SilentlyContinue
    } else {
        $env:OPENSTITCH_THREADS = $Threads.ToString([Globalization.CultureInfo]::InvariantCulture)
    }
    foreach ($caseName in $metadata.cases) {
        $case = $corpus[$caseName]
        $source = Join-Path $root $case.Image
        $fixture = $source
        if ($caseName -eq 'text') {
            $fixture = Join-Path $outputPath 'text-240mm.bmp'
            $width = New-TextBitmap $source $fixture
            $case.Dpi = $width * 25.4 / 240.0
        }
        $options = @($case.Options)
        if ($NoInstrument) { $options += '--no-instrument' }
        $dpi = $case.Dpi.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
        $cold = [Collections.Generic.List[object]]::new()
        $warm = [Collections.Generic.List[object]]::new()
        $rawFiles = @()
        $reference = $null
        $referencePreviewHash = $null
        for ($process = 1; $process -le $Processes; $process++) {
            Write-Host "Profilage $caseName ($process/$Processes)"
            $file = '{0}.{1:00}.jsonl' -f $caseName, $process
            $rawFiles += $file
            $rawPath = Join-Path $outputPath $file
            $lines = @(& $benchPath $fixture $dpi $Repetitions @options)
            $exitCode = $LASTEXITCODE
            [IO.File]::WriteAllLines($rawPath, [string[]]$lines, $encoding)
            if ($exitCode -ne 0) { throw "Benchmark failed for $file (exit $exitCode)." }
            if ($lines.Count -ne $Repetitions) {
                throw "Expected $Repetitions samples in $file, received $($lines.Count)."
            }
            for ($i = 0; $i -lt $Repetitions; $i++) {
                $sample = ConvertFrom-Json -InputObject $lines[$i]
                if ($sample.run -ne $i) { throw "Unexpected sample index in $file at run $i." }
                if (-not $sample.sequence_hash -or -not $sample.label_hash) {
                    throw "Missing fingerprints in $file at run $i."
                }
                $previewHash = $null
                $previewProperty = $sample.PSObject.Properties['preview_hash']
                if ($null -ne $previewProperty) { $previewHash = $previewProperty.Value }
                if ($null -eq $reference) {
                    $reference = $sample
                    $referencePreviewHash = $previewHash
                }
                if ($sample.sequence_hash -cne $reference.sequence_hash -or
                    $sample.label_hash -cne $reference.label_hash -or
                    $previewHash -cne $referencePreviewHash) {
                    throw "Unstable fingerprints for $caseName in $file at run $i."
                }
                if ($i -eq 0) { $cold.Add($sample) } else { $warm.Add($sample) }
            }
        }

        $summaries += [ordered]@{
            case = $caseName
            source = $case.Image
            source_sha256 = (Get-FileHash -LiteralPath $source).Hash
            fixture_sha256 = (Get-FileHash -LiteralPath $fixture).Hash
            dpi = $case.Dpi
            options = $options
            image_width = $reference.image_width
            image_height = $reference.image_height
            image_bytes = $reference.image_bytes
            preview_bytes = $reference.preview_bytes
            regions = $reference.regions
            objects = $reference.objects
            stitches = $reference.stitches
            jumps = $reference.jumps
            trims = $reference.trims
            trims_removed = $reference.trims_removed
            sequence_hash = $reference.sequence_hash
            label_hash = $reference.label_hash
            preview_hash = $referencePreviewHash
            raw_files = $rawFiles
            cold = Get-PhaseSummary $cold.ToArray()
            warm = Get-PhaseSummary $warm.ToArray()
        }
        Write-Host ('  froid p50 {0:N2} ms | chaud p50 {1:N2} ms, p95 {2:N2} ms' -f
                    $summaries[-1].cold.wall_ms.p50, $summaries[-1].warm.wall_ms.p50,
                    $summaries[-1].warm.wall_ms.p95)
    }
} finally {
    if ($null -eq $previousThreads) {
        Remove-Item Env:OPENSTITCH_THREADS -ErrorAction SilentlyContinue
    } else {
        $env:OPENSTITCH_THREADS = $previousThreads
    }
}

[IO.File]::WriteAllText((Join-Path $outputPath 'summary.json'),
                        (ConvertTo-Json -InputObject $summaries -Depth 12), $encoding)
Write-Output "Raw samples, summary and machine metadata: $outputPath"
