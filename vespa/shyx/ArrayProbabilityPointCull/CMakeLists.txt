set(classes
  vtkSHYXArrayProbabilityPointCull)
vtk_module_add_module(vtkSHYXArrayProbabilityPointCull
  FORCE_STATIC
  CLASSES ${classes})

if (VESPA_USE_SMP)
  target_compile_definitions(vtkSHYXArrayProbabilityPointCull PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkSHYXArrayProbabilityPointCull: SMP optimization ENABLED")
else()
  message(STATUS "vtkSHYXArrayProbabilityPointCull: SMP optimization disabled")
endif()
