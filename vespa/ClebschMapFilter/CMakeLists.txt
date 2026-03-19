set(classes
  vtkSHYXClebschMapFilter)
vtk_module_add_module(vtkSHYXClebschMapFilter
  FORCE_STATIC
  CLASSES ${classes}
)

if(MSVC)
  target_compile_options(vtkSHYXClebschMapFilter PRIVATE /utf-8)
endif()
if (VESPA_USE_SMP)
  target_compile_definitions(vtkSHYXClebschMapFilter PRIVATE VESPA_USE_SMP)
  message(STATUS "vtkSHYXClebschMapFilter: SMP optimization ENABLED")
else()
  message(STATUS "vtkSHYXClebschMapFilter: SMP optimization disabled")
endif()
if (VESPA_USE_MKL)
  target_compile_definitions(vtkSHYXClebschMapFilter PRIVATE VESPA_USE_MKL EIGEN_USE_MKL_ALL)
  target_link_libraries(vtkSHYXClebschMapFilter PRIVATE MKL::MKL)
  message(STATUS "vtkSHYXClebschMapFilter: MKL acceleration ENABLED")
else()
  message(STATUS "vtkSHYXClebschMapFilter: MKL acceleration disabled")
endif()
vtk_module_link(vtkSHYXClebschMapFilter PRIVATE Eigen3::Eigen)
