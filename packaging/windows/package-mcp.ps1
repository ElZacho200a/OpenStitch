# SPDX-License-Identifier: Apache-2.0
# Package a portable Windows x64 MCP server with adjacent vcpkg DLLs and a SHA-256 file.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BinaryDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $BinaryDir).Path
$exe = Join-Path $source 'openstitch-mcp.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Missing executable: $exe"
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDir).Path
$name = 'openstitch-mcp-windows-x64.zip'
$zip = Join-Path $out $name
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ('openstitch-mcp-' + [guid]::NewGuid().ToString('N'))
$bundle = Join-Path $stage 'openstitch-mcp-windows-x64'
try {
    New-Item -ItemType Directory -Path $bundle -Force | Out-Null
    Copy-Item -LiteralPath $exe -Destination $bundle
    Get-ChildItem -LiteralPath $source -File -Filter '*.dll' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $bundle
    }
    foreach ($license in @('LICENSE', 'THIRD_PARTY_LICENSES.md')) {
        if (Test-Path -LiteralPath $license) {
            Copy-Item -LiteralPath $license -Destination $bundle
        }
    }
    @'
OpenStitch MCP server - Windows x64
Keep the DLLs next to openstitch-mcp.exe.
Test using openstitch-mcp.exe --version and openstitch-mcp.exe --doctor.
MCP uses stdio (JSON-RPC), no network port.
Docs: https://github.com/ElZacho200a/OpenStitch/blob/main/docs/source/mcp.md
'@ | Set-Content -LiteralPath (Join-Path $bundle 'README.txt') -Encoding utf8
    Compress-Archive -LiteralPath $bundle -DestinationPath $zip -CompressionLevel Optimal -Force
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText(
        "$zip.sha256", "$hash  $name`n", [System.Text.UTF8Encoding]::new($false))
    Write-Host "Built $zip (SHA-256 $hash)"
}
finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
