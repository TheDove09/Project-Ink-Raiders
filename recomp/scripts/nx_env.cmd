@echo off
rem Configure MSVC x64 + LLVM clang-cl + CMake + Ninja + sccache for local builds.
rem Usage: scripts\nx_env.cmd cmake --build build\x
rem        scripts\nx_env.cmd (prints the tool versions)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if defined VSROOT call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if not defined VSROOT if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=%VSROOT%\VC\Tools\Llvm\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%ProgramFiles%\LLVM\bin;%ProgramFiles%\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;%~dp0..\..\tools\.venv\Scripts;%PATH%"
if "%~1"=="" (
  clang-cl --version & cmake --version & ninja --version & sccache --version
  exit /b 0
)
%*
