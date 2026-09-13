set(classes
  vtkSHYXAlphaWrapping)
vtk_module_add_module(vtkSHYXAlphaWrapping
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXAlphaWrapping PRIVATE CGAL::Eigen3_support)

vespa_plugin_xml(SHYXAlphaWrapping.xml)
