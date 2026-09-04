set(classes
  vtkSHYXSkeletonEndClipper)
vtk_module_add_module(vtkSHYXSkeletonEndClipper
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXSkeletonEndClipper PRIVATE CGAL::Eigen3_support)

vespa_plugin_xml(SHYXSkeletonEndClipper.xml)
