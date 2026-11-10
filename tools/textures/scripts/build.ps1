# Configure + build astc2bc7 (library, CLI, astcenc decoder DLLs) with CMake + Ninja + clang-cl.
#   pwsh -File tools\textures\scripts\build.ps1 [-Clean]
# Needs third_party\ (see third_party\VERSIONS.md): astc-encoder, bc7enc_rdo, ispc. Output: tools\textures\build\release\
param([switch]$Clean, [string]$BuildDir)
$ErrorActionPreference = 'Stop'
$root  = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repo = [IO.Path]::GetFullPath((Join-Path $root '..\..'))
$workspace = [IO.Path]::GetFullPath((Join-Path $repo '..\..'))
$build = if ($BuildDir) { $BuildDir } else { Join-Path $workspace 'work\raiders\build\astc-textures' }
$envCmd = Join-Path $repo 'runtime\scripts\env.cmd'
$toolchain = Join-Path $repo 'recomp\cmake\clang-cl.cmake'
if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
$configure = 'cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE:FILEPATH="{2}"' -f $root, $build, $toolchain
$compile = 'cmake --build "{0}"' -f $build
& cmd.exe /d /c ('call "{0}" && {1} && {2}' -f $envCmd, $configure, $compile)
if ($LASTEXITCODE) { throw "ASTC texture tool build failed with exit code $LASTEXITCODE" }
Write-Output "built: $build\astc2bc7.exe"
