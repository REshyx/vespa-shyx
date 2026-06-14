# METIS backend (multilevel k-way partitioning). Hardcoded install prefixes per dev machine.
set(_vespa_metis_prefix "C:/Users/18490/Documents/Github/METIS/install")
set(_vespa_gklib_prefix "C:/Users/18490/Documents/Github/GKlib/install")

find_package(GKlib CONFIG REQUIRED
  PATHS "${_vespa_gklib_prefix}"
  NO_DEFAULT_PATH)

# METIS's METISConfig.cmake calls find_dependency(GKlib HINTS "C:\Users\...") with
# unescaped backslashes, which CMake rejects (Invalid escape '\U'). GKlib is already
# found above; load the imported METIS target directly from the install tree.
include("${_vespa_metis_prefix}/lib/cmake/METIS/METISTargets.cmake")

if (NOT TARGET METIS::metis)
  message(FATAL_ERROR
    "METIS::metis target missing after including METISTargets.cmake. "
    "Check ${_vespa_metis_prefix}/lib/cmake/METIS and that lib/metis.lib exists.")
endif ()

set(classes
  vtkSHYXTetMeshRegionPartition)
vtk_module_add_module(vtkSHYXTetMeshRegionPartition
  FORCE_STATIC
  CLASSES ${classes}
)

vtk_module_link(vtkSHYXTetMeshRegionPartition PRIVATE METIS::metis)
target_compile_definitions(vtkSHYXTetMeshRegionPartition PRIVATE VESPA_USE_METIS)
message(STATUS "vtkSHYXTetMeshRegionPartition: METIS backend ENABLED (${_vespa_metis_prefix})")

if(MSVC)
  target_compile_options(vtkSHYXTetMeshRegionPartition PRIVATE /utf-8)
endif()

if (VESPA_USE_SMP)
  target_compile_definitions(vtkSHYXTetMeshRegionPartition PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkSHYXTetMeshRegionPartition: SMP optimization ENABLED")
else()
  message(STATUS "vtkSHYXTetMeshRegionPartition: SMP optimization disabled")
endif()
