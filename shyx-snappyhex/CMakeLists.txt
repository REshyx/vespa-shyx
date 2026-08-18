cmake_minimum_required(VERSION 3.20)
project(SHYXSnappyHex VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FoamLibrary.cmake")

set(FOAM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openfoam-v2412"
    CACHE PATH "Pristine OpenFOAM source tree (src/wmake/etc). Not patched; not vendored.")
set(SHYX_OPENFOAM_VERSION "2412" CACHE STRING "OpenFOAM version baked into WM macros (2412, ...)")

option(SHYX_BUILD_OPENFOAM "Compile OpenFOAM meshing static libs and link snappyHexMesh" ON)
option(SHYX_BUILD_CLI "Build snappy_cli" ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

if(MSVC)
  add_compile_options(/bigobj /EHsc /fp:precise /wd4244 /wd4267 /wd4996 /wd4800 /wd4018 /wd4305)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  add_compile_options("/FImsvc_foam_compat.h")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(
      /clang:-Wno-deprecated-declarations
      /clang:-Wno-unused-parameter
      /clang:-Wno-unused-variable
      /clang:-Wno-unused-function
      /clang:-Wno-extra-semi
      /clang:-Wno-invalid-offsetof
    )
  else()
    add_compile_options(
      -Wno-deprecated-declarations
      -Wno-unused-parameter
      -Wno-unused-variable
      -Wno-unused-function
      -Wno-extra-semi
      -Wno-invalid-offsetof
    )
  endif()
endif()

# ---------------------------------------------------------------------------
# OpenFOAM static libraries (meshing closure only)
# ---------------------------------------------------------------------------
set(FOAM_LIBS)
set(SHYX_HAS_OPENFOAM 0)

if(SHYX_BUILD_OPENFOAM)
  if(NOT EXISTS "${FOAM_SOURCE_DIR}/src/OpenFOAM/Make/files")
    message(WARNING "OpenFOAM sources not found at ${FOAM_SOURCE_DIR}. "
      "Set FOAM_SOURCE_DIR to a pristine OpenFOAM-${SHYX_OPENFOAM_VERSION} tree "
      "(src/wmake/etc). Building case-writer only (SHYX_HAS_OPENFOAM=0).")
    set(SHYX_BUILD_OPENFOAM OFF)
  endif()
endif()

if(SHYX_BUILD_OPENFOAM)
  set(FOAM_LNINCLUDE_ROOT "${CMAKE_BINARY_DIR}/lnInclude")
  file(MAKE_DIRECTORY "${FOAM_LNINCLUDE_ROOT}")
  if(WIN32)
    execute_process(COMMAND fsutil.exe file setCaseSensitiveInfo "${FOAM_LNINCLUDE_ROOT}" enable
      OUTPUT_QUIET ERROR_QUIET)
  endif()
  set(SHYX_HAS_OPENFOAM 1)

  if(WIN32)
    set(FOAM_OSSPECIFIC "${FOAM_SOURCE_DIR}/src/OSspecific/MSwindows")
    set(FOAM_ARCH_DEFS WIN64 WM_ARCH=Win64)
  else()
    set(FOAM_OSSPECIFIC "${FOAM_SOURCE_DIR}/src/OSspecific/POSIX")
    set(FOAM_ARCH_DEFS linux64 WM_ARCH=linux64)
  endif()

  set(FOAM_COMMON_DEFS
    WM_LABEL_SIZE=32
    WM_PRECISION_OPTION=DP
    WM_DP
    WM_ARCH_OPTION=64
    WM_LITTLE_ENDIAN
    NoRepository
    OPENFOAM=${SHYX_OPENFOAM_VERSION}
    Foam_no_besselFunc
    ${FOAM_ARCH_DEFS}
  )

  foam_add_static_library(OSspecific "${FOAM_OSSPECIFIC}")
  foam_add_static_library(Pstream "${FOAM_SOURCE_DIR}/src/Pstream/dummy")
  foam_add_static_library(OpenFOAM "${FOAM_SOURCE_DIR}/src/OpenFOAM" DEPS OSspecific Pstream)
  # globals.C #includes debug.C / error.C / IOerror.C from lnInclude.
  foam_overlay_lninclude("${FOAM_LNINCLUDE_ROOT}/OpenFOAM"
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/foam_debug.cxx" debug.C)
  foam_overlay_lninclude("${FOAM_LNINCLUDE_ROOT}/OpenFOAM"
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/foam_error.cxx" error.C)
  foam_overlay_lninclude("${FOAM_LNINCLUDE_ROOT}/OpenFOAM"
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/foam_IOerror.cxx" IOerror.C)
  target_sources(OpenFOAM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter/foam_IOobject.cxx")
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/adapter/foam_IOobject.cxx" PROPERTIES LANGUAGE CXX)
  set(_foam_cfg "${CMAKE_BINARY_DIR}/generated/foamConfig.C")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
  file(READ "${FOAM_SOURCE_DIR}/src/OpenFOAM/global/foamConfig.Cver" _cfg)
  string(REPLACE "@PATCH@" "0" _cfg "${_cfg}")
  string(REPLACE "@BUILD@" "shyx-snappyhex" _cfg "${_cfg}")
  string(REPLACE "@VERSION@" "${SHYX_OPENFOAM_VERSION}" _cfg "${_cfg}")
  file(WRITE "${_foam_cfg}" "${_cfg}")
  target_sources(OpenFOAM PRIVATE "${_foam_cfg}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/foam_eval_stubs.cxx")
  set_source_files_properties("${_foam_cfg}" PROPERTIES LANGUAGE CXX)
  foam_add_static_library(fileFormats "${FOAM_SOURCE_DIR}/src/fileFormats" DEPS OpenFOAM)
  foam_add_static_library(surfMesh "${FOAM_SOURCE_DIR}/src/surfMesh" DEPS fileFormats)
  foam_add_static_library(meshTools "${FOAM_SOURCE_DIR}/src/meshTools" DEPS surfMesh)
  foam_add_static_library(blockMesh "${FOAM_SOURCE_DIR}/src/mesh/blockMesh" DEPS meshTools)
  foam_add_static_library(extrudeModel "${FOAM_SOURCE_DIR}/src/mesh/extrudeModel" DEPS meshTools blockMesh)
  foam_add_static_library(finiteVolume "${FOAM_SOURCE_DIR}/src/finiteVolume" DEPS meshTools)
  foam_add_static_library(dynamicMesh "${FOAM_SOURCE_DIR}/src/dynamicMesh" DEPS finiteVolume extrudeModel)
  foam_add_static_library(dynamicFvMesh "${FOAM_SOURCE_DIR}/src/dynamicFvMesh" DEPS dynamicMesh)
  foam_add_static_library(lagrangian "${FOAM_SOURCE_DIR}/src/lagrangian/basic" DEPS meshTools)
  foam_add_static_library(sampling "${FOAM_SOURCE_DIR}/src/sampling" DEPS finiteVolume lagrangian)
  foam_add_static_library(fvMotionSolver "${FOAM_SOURCE_DIR}/src/fvMotionSolver" DEPS finiteVolume dynamicMesh)
  foam_add_static_library(decompositionMethods
    "${FOAM_SOURCE_DIR}/src/parallel/decompose/decompositionMethods" DEPS finiteVolume dynamicMesh)
  foam_add_static_library(decompose
    "${FOAM_SOURCE_DIR}/src/parallel/decompose/decompose" DEPS finiteVolume decompositionMethods lagrangian)
  foam_add_static_library(distributed
    "${FOAM_SOURCE_DIR}/src/parallel/distributed" DEPS meshTools decompositionMethods)
  foam_add_static_library(reconstruct
    "${FOAM_SOURCE_DIR}/src/parallel/reconstruct/reconstruct" DEPS finiteVolume lagrangian)
  foam_add_static_library(overset "${FOAM_SOURCE_DIR}/src/overset" DEPS finiteVolume dynamicMesh dynamicFvMesh sampling
    decompositionMethods blockMesh)
  foam_add_static_library(snappyHexMesh "${FOAM_SOURCE_DIR}/src/mesh/snappyHexMesh"
    DEPS finiteVolume fileFormats surfMesh meshTools dynamicMesh sampling lagrangian
         fvMotionSolver overset distributed decompositionMethods decompose reconstruct)

  set(FOAM_LIBS
    snappyHexMesh overset fvMotionSolver lagrangian sampling finiteVolume
    dynamicFvMesh dynamicMesh extrudeModel blockMesh meshTools surfMesh fileFormats
    OpenFOAM Pstream OSspecific distributed decompositionMethods decompose reconstruct)

  foreach(_t IN LISTS FOAM_LIBS)
    # PRIVATE: do not leak NoRepository / WM_* into vespa VTK TUs.
    target_compile_definitions(${_t} PRIVATE ${FOAM_COMMON_DEFS})
  endforeach()
  # OSspecific / Pstream compile against OpenFOAM headers (wmake -I OpenFOAM/lnInclude).
  foam_quote_include(OSspecific "${FOAM_LNINCLUDE_ROOT}/OpenFOAM")
  foam_quote_include(Pstream "${FOAM_LNINCLUDE_ROOT}/OpenFOAM")
  foam_quote_include(Pstream "${FOAM_LNINCLUDE_ROOT}/OSspecific")

  target_sources(fileFormats PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/foam_stl_flex_stub.cxx")
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/foam_stl_flex_stub.cxx" PROPERTIES LANGUAGE CXX)

  if(UNIX)
    foreach(_t IN LISTS FOAM_LIBS)
      target_link_libraries(${_t} PUBLIC dl m)
    endforeach()
  endif()
  if(WIN32)
    target_link_libraries(OSspecific PUBLIC ws2_32 psapi advapi32 shell32 ole32 user32)
  endif()
endif()

# ---------------------------------------------------------------------------
# Public SHYXSnappyHex static library (C API)
# ---------------------------------------------------------------------------
add_library(SHYXSnappyHex STATIC
  src/background_hex.cxx
  src/case_writer.cxx
  src/shyx_snappy.cxx
  src/snappy_spawn_win.cxx
  src/foam_env_early.cxx
  src/foam_force_link.cxx
)
target_include_directories(SHYXSnappyHex PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
if(SHYX_BUILD_OPENFOAM AND EXISTS "${FOAM_SOURCE_DIR}/etc/cellModels")
  file(READ "${FOAM_SOURCE_DIR}/etc/cellModels" SHYX_EMBEDDED_CELL_MODELS)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shyx_embedded_etc.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shyx_embedded_etc.h"
    @ONLY)
  target_include_directories(SHYXSnappyHex PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(SHYXSnappyHex PRIVATE SHYX_HAS_EMBEDDED_CELL_MODELS=1)
endif()
target_compile_definitions(SHYXSnappyHex PUBLIC SHYX_HAS_OPENFOAM=${SHYX_HAS_OPENFOAM})
if(SHYX_BUILD_OPENFOAM)
  target_compile_definitions(SHYXSnappyHex PRIVATE
    SHYX_HAS_OPENFOAM=1
    ${FOAM_COMMON_DEFS})
    set(_shm "${FOAM_SOURCE_DIR}/applications/utilities/mesh/generation/snappyHexMesh/snappyHexMesh.C")
  if(EXISTS "${_shm}")
    target_sources(SHYXSnappyHex PRIVATE src/snappyHexMesh_main.cxx)
    set_source_files_properties(src/snappyHexMesh_main.cxx PROPERTIES
      LANGUAGE CXX
      COMPILE_DEFINITIONS "SHYX_SNAPPYHEXMESH_C=\"${_shm}\"")
    # RTS-only TUs are dropped from static Foam archives unless compiled here.
    set(_of "${FOAM_SOURCE_DIR}/src/OpenFOAM")
    set(_rts
      "${FOAM_SOURCE_DIR}/src/finiteVolume/fvMesh/fvPatches/derived/wall/wallFvPatch.C"
      "${_of}/meshes/pointMesh/pointPatches/derived/wall/wallPointPatch.C"
      "${FOAM_SOURCE_DIR}/src/parallel/decompose/decompositionMethods/noDecomp/noDecomp.C"
      "${_of}/fields/pointPatchFields/derived/slip/slipPointPatchFields.C"
      "${_of}/fields/pointPatchFields/derived/fixedNormalSlip/fixedNormalSlipPointPatchFields.C"
      "${_of}/fields/pointPatchFields/basic/calculated/calculatedPointPatchFields.C"
      "${_of}/fields/pointPatchFields/basic/value/valuePointPatchFields.C"
      "${_of}/fields/pointPatchFields/basic/fixedValue/fixedValuePointPatchFields.C"
      "${_of}/fields/pointPatchFields/basic/zeroGradient/zeroGradientPointPatchFields.C"
      "${_of}/fields/pointPatchFields/constraint/cyclicSlip/cyclicSlipPointPatchFields.C"
      "${FOAM_SOURCE_DIR}/src/mesh/snappyHexMesh/externalDisplacementMeshMover/zeroFixedValue/zeroFixedValuePointPatchFields.C"
      "${FOAM_SOURCE_DIR}/src/finiteVolume/fields/fvPatchFields/derived/slip/slipFvPatchFields.C"
      "${FOAM_SOURCE_DIR}/src/finiteVolume/fields/fvPatchFields/basic/calculated/calculatedFvPatchFields.C"
      "${FOAM_SOURCE_DIR}/src/finiteVolume/fields/fvPatchFields/basic/fixedValue/fixedValueFvPatchFields.C"
      "${FOAM_SOURCE_DIR}/src/finiteVolume/fields/fvPatchFields/basic/zeroGradient/zeroGradientFvPatchFields.C"
      "${FOAM_SOURCE_DIR}/src/finiteVolume/fields/fvsPatchFields/basic/calculated/calculatedFvsPatchFields.C"
      "${FOAM_SOURCE_DIR}/src/finiteVolume/fields/fvsPatchFields/basic/fixedValue/fixedValueFvsPatchFields.C"
    )
    target_sources(SHYXSnappyHex PRIVATE ${_rts})
    set_source_files_properties(${_rts} PROPERTIES LANGUAGE CXX)
  endif()
  # PRIVATE: Foam iquote / WM_* stay on this archive. CMake still propagates
  # the static Foam .libs to the final EXE/DLL as LINK_ONLY.
  target_link_libraries(SHYXSnappyHex PRIVATE ${FOAM_LIBS})
endif()

add_library(SHYXSnappyHex::SHYXSnappyHex ALIAS SHYXSnappyHex)

if(SHYX_BUILD_CLI)
  add_executable(snappy_cli apps/snappy_cli.cxx)
  target_link_libraries(snappy_cli PRIVATE SHYXSnappyHex)
  if(MSVC)
    # Driver + foam_force_link live in this archive; lld-link drops unreferenced members.
    target_link_options(snappy_cli PRIVATE
      "/WHOLEARCHIVE:$<TARGET_FILE:SHYXSnappyHex>"
      "/OPT:NOICF")
  endif()
endif()

# ---------------------------------------------------------------------------
# Install / CMake package
# ---------------------------------------------------------------------------
install(TARGETS SHYXSnappyHex ${FOAM_LIBS}
  EXPORT SHYXSnappyHexTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(SHYX_BUILD_CLI)
  install(TARGETS snappy_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES include/shyx_snappy.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT SHYXSnappyHexTargets
  FILE SHYXSnappyHexTargets.cmake
  NAMESPACE SHYXSnappyHex::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SHYXSnappyHex)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/SHYXSnappyHexConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/SHYXSnappyHexConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SHYXSnappyHex)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/SHYXSnappyHexConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/SHYXSnappyHexConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/SHYXSnappyHexConfigVersion.cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SHYXSnappyHexWholeArchive.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SHYXSnappyHex)
