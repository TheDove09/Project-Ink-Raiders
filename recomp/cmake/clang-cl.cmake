# SPDX-License-Identifier: GPL-3.0-or-later
# Toolchain file: LLVM clang-cl with the MSVC x64 environment (run through scripts/nx_env.cmd, or from a shell where
# VsDevCmd.bat has run), Ninja, optional sccache. Compiler binaries may come from the VS workload or standalone LLVM.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
find_program(_NX_CLANG_CL NAMES clang-cl.exe REQUIRED)
find_program(_NX_LLD_LINK NAMES lld-link.exe REQUIRED)
find_program(_NX_LLVM_RC NAMES llvm-rc.exe REQUIRED)
find_program(_NX_LLVM_MT NAMES llvm-mt.exe REQUIRED)
set(CMAKE_C_COMPILER "${_NX_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${_NX_CLANG_CL}")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER "${_NX_LLD_LINK}")
set(CMAKE_RC_COMPILER "${_NX_LLVM_RC}")
set(CMAKE_MT "${_NX_LLVM_MT}")
# static MSVC runtime (/MT): the lifted libraries and the runtime that links them must agree, and players then need no
# Visual C++ Redistributable (CMake needs policy CMP0091 NEW, i.e. cmake_minimum_required 3.15 or later, for this to apply)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# the debug info format of an exe built here is not useful to the oracles: keep objects small
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "")
if(NOT DEFINED ENV{NX_NO_SCCACHE})
  find_program(NX_SCCACHE sccache HINTS "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Links")
  if(NX_SCCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${NX_SCCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${NX_SCCACHE}")
  endif()
endif()
