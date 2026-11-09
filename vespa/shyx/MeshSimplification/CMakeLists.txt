set(classes
  vtkSHYXMeshSimplification)
vtk_module_add_module(vtkSHYXMeshSimplification
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXMeshSimplification PRIVATE CGAL::Eigen3_support)

vespa_plugin_xml(SHYXMeshSimplification.xml)
