set(classes
  vtkSHYXDataSetToPartitionedCollection)
vtk_module_add_module(vtkSHYXDataSetToPartitionedCollection
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXDataSetToPartitionedCollection PRIVATE /utf-8)
endif()
