set(classes
  vtkSHYXEdgeCollapse)
vtk_module_add_module(vtkSHYXEdgeCollapse
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXEdgeCollapse PRIVATE CGAL::Eigen3_support)
