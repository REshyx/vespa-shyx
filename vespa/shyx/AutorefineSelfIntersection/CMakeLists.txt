set(classes
  vtkSHYXAutorefineSelfIntersectionFilter)
vtk_module_add_module(vtkSHYXAutorefineSelfIntersectionFilter
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXAutorefineSelfIntersectionFilter PRIVATE CGAL::Eigen3_support)
