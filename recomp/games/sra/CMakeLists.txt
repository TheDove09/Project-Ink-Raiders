# SPDX-License-Identifier: GPL-3.0-or-later
# Static-recompile the locally supplied Raiders NSO modules using the shared nxrecomp lifter.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/GameManifest.cmake")
game_manifest_load("${CMAKE_CURRENT_LIST_DIR}/game.toml")
project(${GM_ID}_lift C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NxRecomp.cmake")

string(TOUPPER "${GM_ID}" _G)
get_filename_component(_WORKSPACE_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/../../../../../" ABSOLUTE)
set(SRA_WORK_ROOT "${_WORKSPACE_DEFAULT}/work/raiders" CACHE PATH "local Raiders work folder (game dump and generated build files)")
set(${_G}_EXEFS "${SRA_WORK_ROOT}/game/dump/exefs" CACHE PATH "folder with the Raiders NSOs")
set(${_G}_GEN "${SRA_WORK_ROOT}/work/lift" CACHE PATH "where generated C++ sources go")

math(EXPR _last "${GM_MODULE_COUNT} - 1")
foreach(_i RANGE ${_last})
    set(_m "${GM_module_${_i}_name}")
    string(TOUPPER "${_m}" _M)
    set(${_G}_SHARDS_${_M} ${GM_module_${_i}_units} CACHE STRING "translation units for ${_m}")
    set(_sites "")
    if(DEFINED GM_module_${_i}_sites)
        set(${_G}_SITES "${CMAKE_CURRENT_LIST_DIR}/${GM_module_${_i}_sites}" CACHE FILEPATH "hook sites for ${_m}")
        set(_sites SITES "${${_G}_SITES}")
    endif()
    nxrecomp_add_module(${GM_ID}_${_m} INPUT "${${_G}_EXEFS}/${GM_module_${_i}_file}" NAME ${_m} INDEX ${GM_module_${_i}_index}
                        BASE ${GM_module_${_i}_base} GAME ${GM_ID} SHARDS ${${_G}_SHARDS_${_M}}
                        ${_sites} OUTDIR "${${_G}_GEN}/${_m}")
endforeach()
add_custom_target(${GM_ID}_modules DEPENDS ${GM_MODULE_LIBS})
