set(classes
  vtkSHYXBidirectionalStreamlineMerge)
vtk_module_add_module(vtkSHYXBidirectionalStreamlineMerge
  FORCE_STATIC
  CLASSES ${classes})

if (VESPA_USE_SMP)
  target_compile_definitions(vtkSHYXBidirectionalStreamlineMerge PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkSHYXBidirectionalStreamlineMerge: SMP optimization ENABLED")
else()
  message(STATUS "vtkSHYXBidirectionalStreamlineMerge: SMP optimization disabled")
endif()
