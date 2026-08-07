set(classes
  vtkSHYXPartitionedCollectionBoundaryAssignment)
vtk_module_add_module(vtkSHYXPartitionedCollectionBoundaryAssignment
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXPartitionedCollectionBoundaryAssignment PRIVATE /utf-8)
endif()
