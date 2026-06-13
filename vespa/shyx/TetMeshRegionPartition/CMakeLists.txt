set(classes
  vtkSHYXTetMeshRegionPartition)
vtk_module_add_module(vtkSHYXTetMeshRegionPartition
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXTetMeshRegionPartition PRIVATE /utf-8)
endif()
