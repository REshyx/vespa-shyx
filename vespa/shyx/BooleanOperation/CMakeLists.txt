set(classes
  vtkSHYXBooleanOperationFilter)
vtk_module_add_module(vtkSHYXBooleanOperationFilter
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXBooleanOperationFilter PRIVATE CGAL::Eigen3_support)

vespa_plugin_xml(SHYXBooleanOperationFilter.xml)
