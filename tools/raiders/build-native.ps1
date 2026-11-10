# -Release: the player package binary (GUI exe, ./exefs and ./romfs next to it, no local paths) in its own build folder; see package-release.ps1.
param([int] $Jobs = 8, [string] $BuildDir, [switch] $Release, [string] $Version = '0.1.0')

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$workspace = (Resolve-Path (Join-Path $repo '..\..')).Path
$runtime = Join-Path $repo 'runtime'
$source = Join-Path $repo 'game-build'
$manifest = Join-Path $repo 'recomp\games\sra\game.toml'
$toolchain = Join-Path $repo 'recomp\cmake\clang-cl.cmake'
$libs = Join-Path $workspace 'work\raiders\build\modules-x64'
$a2b = Join-Path $workspace 'work\raiders\build\astc-textures'
$exefs = Join-Path $workspace 'work\raiders\game\dump\exefs'
$envCmd = Join-Path $runtime 'scripts\env.cmd'
if (-not $BuildDir) { $BuildDir = Join-Path $workspace ($(if ($Release) { 'work\raiders\build\release-x64' } else { 'work\raiders\build\native-x64-v2' })) }
if (-not (Test-Path -LiteralPath (Join-Path $libs 'sra_main.lib'))) { throw "Generated module libraries are missing: $libs" }
if (-not (Test-Path -LiteralPath (Join-Path $a2b 'astc2bc7.lib'))) { throw "ASTC transcoder library is missing: $a2b" }
if (-not (Test-Path -LiteralPath (Join-Path $exefs 'main'))) { throw "Raiders ExeFS is missing: $exefs" }

$configure = 'cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE:FILEPATH="{2}" -DGAME_MANIFEST="{3}" -DGAME_RUNTIME_DIR="{4}" -DGAME_LIB_DIR="{5}" -DGAME_EXEFS_DEFAULT="{6}" -DNXRT_A2B_DIR="{7}" -DNXRT_BUILD_TESTS=OFF -DNXRT_BUILD_LIFT_TESTS=OFF -DNXRT_BUILD_LAUNCHER=OFF' -f $source, $BuildDir, $toolchain, $manifest, $runtime, $libs, $exefs, $a2b
if ($Release) { $configure += ' -DGAME_RELEASE=ON -DGAME_VERSION={0}' -f $Version }
$build = 'cmake --build "{0}" --target game_exe --parallel {1}' -f $BuildDir, [Math]::Max(1, [Math]::Min($Jobs, 12))
$command = 'call "{0}" && {1} && {2}' -f $envCmd, $configure, $build
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw "Raiders native Windows executable build failed with exit code $LASTEXITCODE." }
Write-Output (Join-Path $BuildDir 'InkRaiders.exe')
