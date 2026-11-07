# SPDX-License-Identifier: Apache-2.0
# Stage Studio without renaming it or depending on the legacy desktop.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BinaryDir,
    [Parameter(Mandatory)][string]$OutputDir,
    [string]$QtRoot = $env:QT_ROOT
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = (Resolve-Path -LiteralPath $BinaryDir).Path
$exe = Join-Path $source 'openstitch_v2_studio.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Studio executable missing: $exe" }
$deploy = Join-Path $QtRoot 'bin/windeployqt.exe'
if (-not (Test-Path -LiteralPath $deploy -PathType Leaf)) { throw "windeployqt missing: $deploy" }
$out = [IO.Path]::GetFullPath($OutputDir)
$bundle = Join-Path $out 'openstitch-studio-windows-x64'
if (Test-Path -LiteralPath $bundle) { throw "Choose a new output directory; bundle already exists: $bundle" }
New-Item -ItemType Directory -Path $bundle -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination $bundle
Get-ChildItem -LiteralPath $source -File -Filter '*.dll' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $bundle
}
# Include transitive Qt Quick Controls and Dialogs imports, plugins and compiler runtime.
& $deploy --release --no-translations --compiler-runtime --qmldir (Join-Path $repo 'apps/desktop_v2/studio/qml') (Join-Path $bundle 'openstitch_v2_studio.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
$resources = Join-Path $bundle 'resources'
New-Item -ItemType Directory -Path $resources -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'resources/fonts') -Destination $resources -Recurse
foreach ($license in @('LICENSE','THIRD_PARTY_LICENSES.md')) {
    if (Test-Path -LiteralPath (Join-Path $repo $license)) {
        Copy-Item -LiteralPath (Join-Path $repo $license) -Destination $bundle
    }
}
"[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml" | Set-Content -LiteralPath (Join-Path $bundle 'qt.conf') -Encoding utf8
"OpenStitch Studio - Windows x64`nRun openstitch_v2_studio.exe [project.osp]. Keep all adjacent DLLs, plugins, QML and resources.`nGraphics driver fallback: openstitch_v2_studio.exe --software [project.osp]." | Set-Content -LiteralPath (Join-Path $bundle 'README.txt') -Encoding utf8
foreach ($required in @('Qt6Core.dll','Qt6Quick.dll','platforms/qwindows.dll','qml/QtQuick/Controls','qml/QtQuick/Dialogs','resources/fonts/VeraBd.ttf')) {
    if (-not (Test-Path -LiteralPath (Join-Path $bundle $required))) { throw "Incomplete Studio deployment: $required" }
}
$name = 'openstitch-studio-windows-x64.zip'
$zip = Join-Path $out $name
Compress-Archive -LiteralPath $bundle -DestinationPath $zip -CompressionLevel Optimal -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name`n", [Text.UTF8Encoding]::new($false))
Write-Host "Studio bundle: $bundle"
Write-Host "Archive: $zip (SHA-256 $hash)"
