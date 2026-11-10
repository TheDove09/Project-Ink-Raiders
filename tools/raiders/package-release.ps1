# Assembles the player package from a release build (build-native.ps1 -Release): the exe and what it loads at run time, the default
# settings and the readme. Nothing from the game's files goes in: no exefs/romfs, no shader or texture caches, no saves, no logs.
param([string] $BuildDir, [string] $OutDir, [string] $Version = '0.1.0', [switch] $Zip)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$workspace = (Resolve-Path (Join-Path $repo '..\..')).Path
if (-not $BuildDir) { $BuildDir = Join-Path $workspace 'work\raiders\build\release-x64' }
if (-not $OutDir) { $OutDir = Join-Path $workspace 'work\release\ProjectInkRaiders' }
$exe = Join-Path $BuildDir 'InkRaiders.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "No release build at $exe (run build-native.ps1 -Release first)" }

# The shader translator (a self-contained .NET program) is built once into the dev build's tools folder.
$harness = Join-Path $BuildDir 'tools\spirv_harness'
if (-not (Test-Path -LiteralPath (Join-Path $harness 'spirv_harness.exe'))) { $harness = Join-Path $workspace 'work\raiders\build\native-x64-v2\tools\spirv_harness' }
if (-not (Test-Path -LiteralPath (Join-Path $harness 'spirv_harness.exe'))) { throw "spirv_harness.exe not found" }

$running = Get-Process InkRaiders -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($OutDir, [StringComparison]::OrdinalIgnoreCase) }
if ($running) { throw "The game is running from $OutDir (pid $($running.Id -join ', ')): close it before packaging" }
# A package folder someone has played from holds their extracted game, saves and caches: never wipe those, package elsewhere.
foreach ($own in 'exefs', 'romfs', 'shader_cache', 'save', 'saves') {
    if (Test-Path -LiteralPath (Join-Path $OutDir $own)) { throw "$OutDir has a player's $own folder: pass -OutDir with a fresh folder" }
}
if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $OutDir 'tools') | Out-Null
Copy-Item -LiteralPath $exe -Destination $OutDir
Get-ChildItem -LiteralPath $BuildDir -Filter 'astcdec-*-shared.dll' | Copy-Item -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $BuildDir 'fonts') -Destination $OutDir -Recurse
Copy-Item -LiteralPath (Join-Path $BuildDir 'tools\extract-exefs.mjs') -Destination (Join-Path $OutDir 'tools')
Copy-Item -LiteralPath $harness -Destination (Join-Path $OutDir 'tools\spirv_harness') -Recurse
Get-ChildItem -LiteralPath (Join-Path $OutDir 'tools\spirv_harness') -Filter '*.pdb' | Remove-Item
if (Test-Path -LiteralPath (Join-Path $BuildDir 'assets\kbm_icons')) {
    Copy-Item -LiteralPath (Join-Path $BuildDir 'assets') -Destination $OutDir -Recurse
}
# Node.js for the start menu's .nup extractor: the official win-x64 build (node.exe + its LICENSE), unpacked to work\release\node.
$nodeDir = Join-Path $workspace 'work\release\node'
if (Test-Path -LiteralPath (Join-Path $nodeDir 'node.exe')) {
    New-Item -ItemType Directory -Force (Join-Path $OutDir 'tools\node') | Out-Null
    Copy-Item -LiteralPath (Join-Path $nodeDir 'node.exe') -Destination (Join-Path $OutDir 'tools\node')
    Copy-Item -LiteralPath (Join-Path $nodeDir 'LICENSE') -Destination (Join-Path $OutDir 'tools\node\LICENSE.txt')
} else { Write-Warning "No official node.exe in ${nodeDir}: players will need Node.js installed to read a .nup" }
Copy-Item -LiteralPath (Join-Path $repo 'game-build\res\release\sra.ini') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $repo 'game-build\res\release\README.txt') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $OutDir 'LICENSE.txt')

$bytes = (Get-ChildItem -LiteralPath $OutDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Output ('{0}: {1:N0} MB' -f $OutDir, ($bytes / 1MB))
if ($Zip) {
    $zipPath = Join-Path (Split-Path $OutDir) ("ProjectInkRaiders-{0}-win64.zip" -f $Version)
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath }
    Compress-Archive -Path (Join-Path $OutDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Output ('{0}: {1:N0} MB' -f $zipPath, ((Get-Item $zipPath).Length / 1MB))
}
