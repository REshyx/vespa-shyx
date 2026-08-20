set(classes
  vtkSHYXAdaptiveIsotropicRemesher
  vtkSHYXRemeshWithEndpoint)
vtk_module_add_module(vtkSHYXAdaptiveIsotropicRemesher
  FORCE_STATIC
  CLASSES ${classes}
  SOURCES vtkSHYXRemeshProgressState.cxx
  PRIVATE_HEADERS vtkSHYXRemeshProgressState.h)

vtk_module_link(vtkSHYXAdaptiveIsotropicRemesher PRIVATE CGAL::Eigen3_support)
if (VESPA_BUILD_PV_PLUGIN)
  vtk_module_link(vtkSHYXAdaptiveIsotropicRemesher PRIVATE ParaView::RemotingServerManager)
  target_compile_definitions(vtkSHYXAdaptiveIsotropicRemesher PRIVATE VESPA_HAVE_PARAVIEW_SM)
endif()

vespa_plugin_xml(
  SHYXAdaptiveIsotropicRemesher.xml
  SHYXRemeshWithEndpoint.xml)
