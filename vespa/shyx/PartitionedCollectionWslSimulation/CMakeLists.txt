set(classes
  vtkSHYXPartitionedCollectionWslSimulation)
vtk_module_add_module(vtkSHYXPartitionedCollectionWslSimulation
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXPartitionedCollectionWslSimulation PRIVATE /utf-8)
endif()
