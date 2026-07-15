set(classes
  vtkSHYXPartitionedCollectionBoundaryFields)
vtk_module_add_module(vtkSHYXPartitionedCollectionBoundaryFields
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXPartitionedCollectionBoundaryFields PRIVATE /utf-8)
endif()
