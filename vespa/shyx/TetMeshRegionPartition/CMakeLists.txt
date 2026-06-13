set(classes
  vtkSHYXTetMeshRegionPartition)
vtk_module_add_module(vtkSHYXTetMeshRegionPartition
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXTetMeshRegionPartition PRIVATE /utf-8)
endif()

if (VESPA_USE_SMP)
  target_compile_definitions(vtkSHYXTetMeshRegionPartition PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkSHYXTetMeshRegionPartition: SMP optimization ENABLED")
else()
  message(STATUS "vtkSHYXTetMeshRegionPartition: SMP optimization disabled")
endif()
