set(classes
  vtkCGALDensityBasedSampler)
vtk_module_add_module(vtkSHYXDensityBasedSampler
  FORCE_STATIC
  CLASSES ${classes})

if (VESPA_USE_SMP)
  target_compile_definitions(vtkSHYXDensityBasedSampler PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkSHYXDensityBasedSampler: SMP optimization ENABLED")
else()
  message(STATUS "vtkSHYXDensityBasedSampler: SMP optimization disabled")
endif()
