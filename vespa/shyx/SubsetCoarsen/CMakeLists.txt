set(classes
  vtkSHYXSubsetCoarsen)
vtk_module_add_module(vtkSHYXSubsetCoarsen
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXSubsetCoarsen PRIVATE CGAL::Eigen3_support)

vespa_plugin_xml(SHYXSubsetCoarsen.xml)
