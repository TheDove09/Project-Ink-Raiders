param([int] $Jobs = 8)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$workspace = (Resolve-Path (Join-Path $repo '..\..')).Path
$python = Join-Path $workspace 'work\toolchain\python313-embed\python.exe'
$pythonPth = Join-Path (Split-Path -Parent $python) 'python313._pth'
$recomp = Join-Path $repo 'recomp'
$exefs = Join-Path $workspace 'work\raiders\game\dump\exefs'
if (-not (Test-Path -LiteralPath $python)) { throw "Portable Python is missing: $python" }
if (-not (Test-Path -LiteralPath (Join-Path $exefs 'main'))) { throw "Raiders ExeFS is missing: $exefs" }
$pythonPaths = @(Get-Content -LiteralPath $pythonPth)
if ($pythonPaths -notcontains $recomp) {
    $pythonPaths = @($pythonPaths | Where-Object { $_ -ne 'import site' }) + $recomp + 'import site'
    Set-Content -LiteralPath $pythonPth -Value $pythonPaths -Encoding ASCII
}

$env:NX_PYTHON = $python
$env:NX_BUILD = Join-Path $workspace 'work\raiders\build\modules-x64'
$env:NX_JOBS = [Math]::Max(1, [Math]::Min($Jobs, 12))
$command = "call `"$(Join-Path $repo 'recomp\scripts\build_game.cmd')`" sra"
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw "Raiders module build failed with exit code $LASTEXITCODE." }
