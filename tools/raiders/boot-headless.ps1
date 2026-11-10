# Rebuild InkRaiders.exe (unless -NoBuild), run one bounded headless boot with the local game files, and print the first failure.
#   & tools\raiders\boot-headless.ps1 -Tag mytest [-Seconds 60] [-NoBuild]
# Logs: work\raiders\headless-<Tag>.log and headless-<Tag>-stderr.log. Exit code of InkRaiders.exe is printed (124 = deadline reached).
param([Parameter(Mandatory)] [string] $Tag, [int] $Seconds = 60, [switch] $NoBuild, [int] $Jobs = 12)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$workspace = (Resolve-Path (Join-Path $repo '..\..')).Path
$raiders = Join-Path $workspace 'work\raiders'
# the build's env.cmd locates Visual Studio with vswhere, which lives here and is not always on PATH
$vsInstaller = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
if ((Test-Path $vsInstaller) -and ($env:PATH -notlike "*$vsInstaller*")) { $env:PATH = "$vsInstaller;$env:PATH" }

if (-not $NoBuild) {
    $out = & (Join-Path $PSScriptRoot 'build-native.ps1') -Jobs $Jobs 2>&1
    $out | Select-String -Pattern ' error[ :]|FAILED' | Select-Object -First 10 | ForEach-Object { "$_" }
}
$exe = Join-Path $raiders 'build\native-x64-v2\InkRaiders.exe'
$log = Join-Path $raiders "headless-$Tag.log"
$err = Join-Path $raiders "headless-$Tag-stderr.log"
$p = Start-Process -FilePath $exe -WorkingDirectory $workspace -NoNewWindow -PassThru -Wait `
    -RedirectStandardError $err -RedirectStandardOutput (Join-Path $raiders "headless-$Tag-stdout.log") `
    -ArgumentList @('--game', $raiders, '--exefs', (Join-Path $raiders 'exefs-1.1.0'), '--ini', (Join-Path $raiders 'raiders-init.ini'),
                    '--log', $log, '--headless', '--max-seconds', "$Seconds")
"exit=$($p.ExitCode) log=$log"
Get-Content $log | Select-String -Pattern 'GUEST TRAP|not implemented|svcBreak reason|crash: HOST|crash:   in |reading address|writing address|crash: guest frame chain|FREEZE|guest call stack|frame times|nvn back end at the deadline|max_seconds' |
    Select-Object -First 16 | ForEach-Object { $_.Line }
