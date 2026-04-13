set(classes
  vtkSHYXDisconnectedRegionFuse)
vtk_module_add_module(vtkSHYXDisconnectedRegionFuse
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXDisconnectedRegionFuse PRIVATE /utf-8)
endif()
if (VESPA_USE_SMP)
  target_compile_definitions(vtkSHYXDisconnectedRegionFuse PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkSHYXDisconnectedRegionFuse: SMP optimization ENABLED")
else()
  message(STATUS "vtkSHYXDisconnectedRegionFuse: SMP optimization disabled")
endif()
