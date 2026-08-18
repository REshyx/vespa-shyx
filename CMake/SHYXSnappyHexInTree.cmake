# In-tree SHYXSnappyHex: clang-cl + Ninja ExternalProject (vespa itself is MSVC/VS).
# OpenFOAM sources stay outside the repo (FOAM_SOURCE_DIR). Adapter lives in shyx-snappyhex/.

set(SHYX_SNAPPYHEX_READY FALSE)

set(_vespa_foam_default "")
foreach(_cand IN ITEMS
    "${CMAKE_SOURCE_DIR}/../OpenFOAM-v2412"
    "C:/Users/18490/Documents/Github/OpenFOAM-v2412"
    "C:/Users/18490/Documents/Github/shyx-snappyhex/third_party/openfoam-v2412"
    "${CMAKE_SOURCE_DIR}/../shyx-snappyhex/third_party/openfoam-v2412")
  if(EXISTS "${_cand}/src/OpenFOAM/Make/files")
    set(_vespa_foam_default "${_cand}")
    break()
  endif()
endforeach()

set(FOAM_SOURCE_DIR "${_vespa_foam_default}" CACHE PATH
  "Pristine OpenFOAM source (src/wmake/etc). Not vendored; not patched.")
set(SHYX_OPENFOAM_VERSION "2412" CACHE STRING "OpenFOAM version for WM macros")
unset(_vespa_foam_default)
unset(_cand)

if(NOT EXISTS "${FOAM_SOURCE_DIR}/src/OpenFOAM/Make/files")
  message(WARNING "VESPA_USE_SNAPPYHEXMESH=ON but FOAM_SOURCE_DIR is missing "
    "(${FOAM_SOURCE_DIR}). Skipping SnappyHexMesh.")
  return()
endif()

set(_shyx_src "${CMAKE_SOURCE_DIR}/shyx-snappyhex")
if(NOT EXISTS "${_shyx_src}/CMakeLists.txt")
  message(WARNING "shyx-snappyhex/ missing next to vespa CMakeLists.txt")
  return()
endif()

set(_shyx_bin "${CMAKE_BINARY_DIR}/shyx-snappyhex-build")
set(_shyx_prefix "${CMAKE_BINARY_DIR}/shyx-snappyhex-install")
file(MAKE_DIRECTORY "${_shyx_bin}")
if(WIN32)
  execute_process(
    COMMAND fsutil.exe file setCaseSensitiveInfo "${_shyx_bin}" enable
    OUTPUT_QUIET ERROR_QUIET)
endif()

set(_shyx_ep_ps1 "${_shyx_src}/scripts/vespa-ep.ps1")
set(_shyx_pwsh)
find_program(_shyx_pwsh NAMES pwsh powershell)
if(NOT _shyx_pwsh)
  message(WARNING "powershell not found; cannot build in-tree SHYXSnappyHex")
  return()
endif()

set(_shyx_foam_lib_names
  snappyHexMesh overset fvMotionSolver lagrangian sampling finiteVolume
  dynamicFvMesh dynamicMesh extrudeModel blockMesh meshTools surfMesh fileFormats
  OpenFOAM Pstream OSspecific distributed decompositionMethods decompose reconstruct)

set(_shyx_byproducts "${_shyx_prefix}/lib/SHYXSnappyHex.lib")
foreach(_n IN LISTS _shyx_foam_lib_names)
  list(APPEND _shyx_byproducts "${_shyx_prefix}/lib/${_n}.lib")
endforeach()

include(ExternalProject)
ExternalProject_Add(shyx_snappyhex_ep
  SOURCE_DIR "${_shyx_src}"
  BINARY_DIR "${_shyx_bin}"
  INSTALL_DIR "${_shyx_prefix}"
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND
    "${_shyx_pwsh}" -NoProfile -ExecutionPolicy Bypass
      -File "${_shyx_ep_ps1}"
      -Step configure
      -Source "<SOURCE_DIR>"
      -Binary "<BINARY_DIR>"
      -Prefix "<INSTALL_DIR>"
      -FoamDir "${FOAM_SOURCE_DIR}"
      -Version "${SHYX_OPENFOAM_VERSION}"
  BUILD_COMMAND
    "${_shyx_pwsh}" -NoProfile -ExecutionPolicy Bypass
      -File "${_shyx_ep_ps1}"
      -Step build
      -Binary "<BINARY_DIR>"
  INSTALL_COMMAND
    "${_shyx_pwsh}" -NoProfile -ExecutionPolicy Bypass
      -File "${_shyx_ep_ps1}"
      -Step install
      -Binary "<BINARY_DIR>"
      -Prefix "<INSTALL_DIR>"
  BUILD_BYPRODUCTS ${_shyx_byproducts}
)

add_library(SHYXSnappyHex::SHYXSnappyHex STATIC IMPORTED GLOBAL)
set_target_properties(SHYXSnappyHex::SHYXSnappyHex PROPERTIES
  IMPORTED_LOCATION "${_shyx_prefix}/lib/SHYXSnappyHex.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${_shyx_src}/include")
set(_shyx_iface)
foreach(_n IN LISTS _shyx_foam_lib_names)
  list(APPEND _shyx_iface "${_shyx_prefix}/lib/${_n}.lib")
endforeach()
if(WIN32)
  list(APPEND _shyx_iface ws2_32 psapi advapi32 shell32 ole32 user32)
endif()
set_property(TARGET SHYXSnappyHex::SHYXSnappyHex PROPERTY
  INTERFACE_LINK_LIBRARIES ${_shyx_iface})

include("${_shyx_src}/cmake/SHYXSnappyHexWholeArchive.cmake")

set(SHYX_SNAPPYHEX_READY TRUE)
message(STATUS "VESPA: SnappyHexMesh in-tree "
  "(FOAM_SOURCE_DIR=${FOAM_SOURCE_DIR}, OpenFOAM=${SHYX_OPENFOAM_VERSION})")
